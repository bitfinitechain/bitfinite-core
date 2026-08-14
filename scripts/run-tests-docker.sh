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
  # miner_tests — NOT re-mineable, do not try. Its blockinfo[] nonce table was
  #   mined against upstream's chain; our genesis differs, so every
  #   hashPrevBlock differs and the nonces no longer satisfy PoW. Blocks are
  #   rejected and the later bad-txns-inputs-missingorspent is fallout from
  #   spending coinbases of blocks that were never accepted. BLOCK ASSEMBLY IS
  #   FINE — this is stale data, not a defect.
  #   Regenerating the table is INFEASIBLE: TestingSetup defaults to
  #   CBaseChainParams::MAIN, and our ASERT anchor puts mainnet at ~70,000
  #   difficulty from block 2 — 3.0e14 hashes per block against difficulty-1's
  #   4.3e9, i.e. 70,001x harder. 110 blocks is ~3.3e16 hashes. Upstream could
  #   precompute theirs because BCH mainnet started at difficulty 1; the same
  #   choice that stops BFX being trivially mineable stops this working.
  #   The real fix is to port the suite to REGTEST (fPowNoRetargeting, powLimit
  #   7fffffff…, so PoW is free). That is a test DESIGN change, not a data
  #   refresh, and needs its own reviewed commit.
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
  transaction_tests  # address/script vectors carrying upstream constants
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
