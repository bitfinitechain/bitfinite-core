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
# review. This list is a work queue, not a settled state, and each line below
# should be deleted as its suite is fixed. miner_tests and checkpoints_tests both came off it on 2026-08-23.
#
# Measured 2026-08-14 on 1264e3222d. Full details in doc/consensus-diff.md.
EXCLUDED=(
  # --- Aborters. These SIGABRT/throw, and because Boost.Test runs everything in
  # one process, an abort takes every later fixture with it via
  # ECC_Start(secp256k1_context_sign == nullptr). Two of these were hiding the
  # state of the entire corpus: the raw count was 463 failures, of which 449
  # were collateral. Fix these before the plain failures — an abort destroys
  # information, a failed check only reports one.
  # pow_tests — 6 of its 10 cases pass. Measured per case on 2026-08-23, because
  #   "pow_tests fails" hid four different situations:
  #
  #     PASS  get_next_work, get_next_work_pow_limit,
  #           get_next_work_lower_limit_actual, get_next_work_upper_limit_actual,
  #           GetBlockProofEquivalentTime_test
  #     PASS  calculate_asert_test — fixed 2026-08-23 by deriving its constants
  #           from spacing and half-life instead of inheriting BCH's 600s/2-day
  #           literals. This is the one that matters: it tests the arithmetic of
  #           the difficulty algorithm this chain actually runs.
  #
  #     FAIL  retargeting_test      — legacy DAA
  #     ABORT cash_difficulty_test  — legacy DAA (cw144)
  #     ABORT asert_activation_anchor_test — the cw144 -> ASERT transition
  #     FAIL  asert_difficulty_test — real ASERT, ~27 inherited constants
  #
  #   The first three exercise DEAD CODE and cannot be made meaningful.
  #   GetNextWorkRequired takes the ASERT branch whenever IsAxionEnabled, and our
  #   axionActivationTime predates our genesis, so GetNextCashWorkRequired and
  #   GetNextEDAWorkRequired are unreachable on every BitFinite network. The
  #   anchor test is worse than unreachable: it exercises a transition FROM cw144
  #   TO ASERT that never happened here, because ASERT is anchored at genesis.
  #   Both aborts are the same assert, nHeight >= DifficultyAdjustmentInterval(),
  #   which is 4032 for us (14d/300s) against BCH's 2016 (14d/600s) — the tests
  #   build ~2050 blocks. Building 4032 would only let dead code run.
  #
  #   Only asert_difficulty_test is worth further work. It is mechanical but
  #   needs per-site review, NOT substitution: one of the nine `2*24*3600`
  #   occurrences is a vector SIZE, not a duration.
  #
  #   The real decision here is not about the tests. If GetNextCashWorkRequired
  #   and GetNextEDAWorkRequired can never execute, the honest move is to delete
  #   them and their cases rather than build fixtures for them. That is a
  #   consensus-surface change and wants its own reviewed commit.
  pow_tests

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
