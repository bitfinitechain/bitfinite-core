#!/usr/bin/env bash
#
# BitFinite Core — build and run the C++ unit tests in the pinned container.
#
# The release script (build-core-docker.sh) builds binaries only, so for a long
# time nothing built test_bitcoin at all. A backport then landed tests for an
# API that was only half-present and the suite stopped compiling for two days
# without anyone noticing, because the node binaries were unaffected. This
# script exists so running the tests is one command, locally and in CI.
#
# Usage:
#   scripts/run-tests-docker.sh                 # the default suite selection
#   scripts/run-tests-docker.sh pow_tests       # one suite
#   scripts/run-tests-docker.sh 'logging_tests,util_tests'
#   BFX_TEST_FILTER='!pow_tests' scripts/run-tests-docker.sh
#
# Uses the SAME image, depends tree, toolchain file and build directory as
# `NO_QT=1 scripts/build-core-docker.sh linux`, so a developer who has already
# run that pays no reconfigure cost — and the tests cannot silently drift onto a
# different toolchain from the one that produces releases.
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="${BFX_CORE_IMAGE:-bitfinite-core-build}-linux"
BASE=ubuntu:22.04
HOST=x86_64-linux-gnu
PLAT=Linux64

# Suites excluded from the default run, so that "green" means something.
#
# EVERY entry carries its reason. An unexplained exclusion is indistinguishable
# from hiding a bug, and this suite is meant to be evidence for an external
# review. This list is a work queue, not a settled state: 105 of 111 suites pass
# today, and each line below should be deleted as its suite is fixed.
#
# Measured 2026-08-14 on 1264e3222d. Full details in doc/consensus-diff.md.
EXCLUDED=(
  # --- Aborters. These SIGABRT/throw, and because Boost.Test runs everything in
  # one process, an abort takes every later fixture with it via
  # ECC_Start(secp256k1_context_sign == nullptr). Two of these were hiding the
  # state of the entire corpus: the raw count was 463 failures, of which 449
  # were collateral. Fix these before the plain failures — an abort destroys
  # information, a failed check only reports one.
  # miner_tests — PORTED TO REGTEST 2026-08-22, down from a SIGABRT and 111
  #   failures to 3. Still excluded for those 3, all in TestPackageSelection.
  #
  #   The old note here said the nonce table was the problem. It was not, and
  #   the correction matters because it points at the wrong class of fix. The
  #   real cause is that OUR chains activate every upgrade at height 0, while
  #   upstream activates Magnetic Anomaly at mainnet height 556766 and Upgrade9
  #   later still. This test mines 110 blocks, so upstream runs it entirely in a
  #   PRE-2018 rule regime that we do not have and cannot have. Three separate
  #   consensus rules therefore bite us and never bite upstream:
  #     * minimum transaction size — 65 bytes under Upgrade9, 100 before it.
  #       The test's coinbase was ~62 bytes and every transaction it builds was
  #       ~62. Everything was rejected bad-txns-undersize.
  #     * SCRIPT_VERIFY_SIGPUSHONLY — the "block size > limit" case padded a
  #       scriptSig with OP_DROP, which is not a push.
  #     * SCRIPT_VERIFY_CLEANSTACK — that same scriptSig left 19 stack items.
  #   Fixed by padding to the size floor and moving the bulk into an
  #   unspendable output. PoW is now ground at run time on regtest rather than
  #   read from a table, which is what made the port possible at all.
  #
  #   WHAT IS LEFT: TestPackageSelection expects medium-fee before a high-fee
  #   transaction with a low-fee parent. All three are 69 bytes, so the package
  #   scores 51000/138 = 369 sat/byte against medium's 10000/69 = 145 and the
  #   package is selected first. No equal-sized arrangement can satisfy the
  #   expectation. The scenario needs a parent that is physically larger, so its
  #   FEERATE is low rather than just its fee. That is an input change and wants
  #   its own commit — do NOT edit the expected txids to match our output.
  miner_tests
  # pow_tests — LOWEST VALUE, do last. assert(nHeight >= 4032) where the test
  #   builds 2049 (upstream's 2016+34) and blocks(3000) is too small anyway. It
  #   exercises GetNextCashWorkRequired, which is DEAD CODE here: IsAxionEnabled
  #   is nHeight >= 0 with the anchor at genesis, so GetNextWorkRequired always
  #   takes the ASERT branch. The ASERT cases in the same file hardcode
  #   2*24*3600 (BCH's 2-day half-life) where ours is 6h; those are EXPECTATIONS
  #   and must come from the ASERT formula, never from our own output.
  pow_tests

  # --- Stale test vectors. The implementation is correct in each of these; the
  # EXPECTED values are upstream's and were never regenerated for the fork.
  # Careful work: a vector "corrected" to match buggy behaviour bakes the bug in
  # permanently, so each needs deriving from first principles, not from output.
  checkpoints_tests  # expects upstream's populated checkpoint heights; ours pins genesis only
)

# Boost.Test filter syntax: colon-separated, ! negates.
DEFAULT_FILTER="$(printf '!%s:' "${EXCLUDED[@]}")"; DEFAULT_FILTER="${DEFAULT_FILTER%:}"

FILTER="${1:-${BFX_TEST_FILTER:-$DEFAULT_FILTER}}"

echo ">> toolchain image ($IMAGE)"
docker build -q -f Dockerfile.build --build-arg BASE="$BASE" -t "$IMAGE" "$REPO" >/dev/null

echo ">> depends + cmake + ninja test_bitcoin"
docker run --rm -v "$REPO":/work -w /work \
  -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" \
  -e HOST="$HOST" -e PLAT="$PLAT" -e FILTER="$FILTER" \
  "$IMAGE" bash -euo pipefail -c '
    git config --global --add safe.directory /work
    make -C depends -j"$(nproc)" HOST="$HOST" NO_QT=1
    # Flags identical to `NO_QT=1 build-core-docker.sh linux` so the two share
    # build-linux/ without forcing each other to reconfigure.
    cmake -GNinja -B build-linux -S . \
      -DCMAKE_TOOLCHAIN_FILE="cmake/platforms/$PLAT.cmake" \
      -DENABLE_MAN=OFF -DCLIENT_VERSION_IS_RELEASE=ON \
      -DBUILD_BITCOIN_SEEDER=ON -DENABLE_GLIBC_BACK_COMPAT=ON -DBUILD_BITCOIN_QT=OFF
    ninja -C build-linux test_bitcoin

    echo ">> running: --run_test=$FILTER"
    set +e
    ./build-linux/src/test/test_bitcoin --run_test="$FILTER" --log_level=test_suite 2>&1 | tail -40
    rc=${PIPESTATUS[0]}
    set -e
    # Hand ownership back before exiting on failure, or the next host-side
    # command hits root-owned files from this run.
    chown -R "$HOST_UID:$HOST_GID" build-linux depends 2>/dev/null || true
    exit "$rc"
  '
