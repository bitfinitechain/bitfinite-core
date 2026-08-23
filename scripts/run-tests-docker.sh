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
# Suites excluded from the default run. EMPTY as of 2026-08-23 — every suite in
# the tree passes.
#
# It has not always been so, and the reason is worth keeping: almost every
# inherited failure came from ONE difference, not from stale data. BitFinite
# activates every upgrade at height 0, while upstream activates them at real
# historical heights far above where these tests reach. Tests that mine a
# hundred-odd blocks therefore run upstream in a pre-2018 rule regime we do not
# have. Minimum transaction size, SIGPUSHONLY, CLEANSTACK and CTOR each broke a
# suite for exactly that reason.
#
# If a suite is ever added back here, say which of those it is, or say plainly
# that it is something new.
EXCLUDED=()

# Boost.Test filter syntax: colon-separated, ! negates.
# An empty EXCLUDED must mean "run everything", not "!" — printf over an empty
# array still emits one '!' and Boost rejects it with exit 200.
if [ ${#EXCLUDED[@]} -eq 0 ]; then
  DEFAULT_FILTER='*'
else
  DEFAULT_FILTER="$(printf '!%s:' "${EXCLUDED[@]}")"; DEFAULT_FILTER="${DEFAULT_FILTER%:}"
fi

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
