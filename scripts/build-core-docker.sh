#!/usr/bin/env bash
#
# BitFinite Core — Dockerized cross-compile (Linux + Windows).
#
# Uses the BCHN `depends` system inside a container so you never install host
# toolchains/deps. Produces stripped, packaged, checksummed release artifacts in
# dist/.
#
# Usage:
#   scripts/build-core-docker.sh                 # both linux + win
#   scripts/build-core-docker.sh linux           # just Linux
#   scripts/build-core-docker.sh win             # just Windows
#   NO_QT=1 scripts/build-core-docker.sh linux    # fast: daemon/cli only (no GUI) — for validation
#
# First run per target is SLOW (depends cross-builds Qt/Boost/etc.); the
# depends/ output is cached for subsequent runs.
#
# THIS SCRIPT DOES NOT PUBLISH, AND ITS OUTPUT MUST NOT BE PUBLISHED.
# dist/ is for local validation. Releases are built and attached by CI on a v*
# tag push (.github/workflows/build-core.yaml). Uploading dist/ by hand races
# CI for the same assets and the loser's checksums end up pinned — which is how
# the whole fleet became unupgradeable during v3.1.0. See doc/releasing.md.

set -euo pipefail
cd "$(dirname "$0")/.."
REPO="$(pwd)"
# Image name is completed per target below (…-linux / …-win): the two targets
# need different base distros and must not share one image.
IMAGE_BASE="${BFX_CORE_IMAGE:-bitfinite-core-build}"
# Derive the package version from the one the binaries actually compile in, so
# the tin cannot disagree with its contents. A hardcoded default here is how you
# ship bitfinite-v3.1.0-*.tar.gz containing a binary that reports v3.0.2 --
# which is the exact defect v3.0.2 was released to fix.
CMAKE_VERSION_STR="$(sed -n 's/^[[:space:]]*VERSION[[:space:]]\{1,\}\([0-9][0-9.]*\).*/\1/p' CMakeLists.txt | head -1)"
[ -n "$CMAKE_VERSION_STR" ] || { echo "cannot read project VERSION from CMakeLists.txt" >&2; exit 1; }
VERSION="${VERSION:-$CMAKE_VERSION_STR}"
if [ "$VERSION" != "$CMAKE_VERSION_STR" ]; then
  echo "ERROR: VERSION=$VERSION but CMakeLists.txt declares $CMAKE_VERSION_STR." >&2
  echo "       The binaries would report $CMAKE_VERSION_STR. Bump the project() version instead." >&2
  exit 1
fi
NO_QT="${NO_QT:-}"
TARGETS=("$@"); [ ${#TARGETS[@]} -eq 0 ] && TARGETS=(linux win)

mkdir -p dist

# Start a FRESH checksum file for this run. It used to be appended to across runs,
# so dist/SHA256SUMS accumulated entries from every previous build and kept stale
# ones forever. That shipped: the v3.0.2 release carried a SHA256SUMS listing only
# v3.0.0.1 files, so `sha256sum -c SHA256SUMS` verified nothing a user had just
# downloaded — silently, which is worse than failing.
: > dist/SHA256SUMS

grep -q "^dist/" .gitignore 2>/dev/null || echo "dist/" >> .gitignore
grep -q "^build-linux/" .gitignore 2>/dev/null || printf "build-linux/\nbuild-win/\ndepends/x86_64-*/\ndepends/work/\ndepends/sources/\n" >> .gitignore

for target in "${TARGETS[@]}"; do
  case "$target" in
    # BASE differs per target on purpose — see the comment block in
    # Dockerfile.build. 22.04 pins the glibc floor for the Linux binaries our
    # own seeds must run; 24.04 supplies a mingw new enough to have
    # <source_location>, which 22.04's GCC 10 lacks.
    linux) HOST=x86_64-linux-gnu;    PLAT=Linux64; EXT="";     OS=linux;   PKGEXT=tar.gz; BASE=ubuntu:22.04 ;;
    win)   HOST=x86_64-w64-mingw32;  PLAT=Win64;   EXT=".exe"; OS=windows; PKGEXT=zip;    BASE=ubuntu:24.04 ;;
    *) echo "unknown target: $target (use linux|win)"; exit 1 ;;
  esac
  IMAGE="${IMAGE_BASE}-${target}"
  echo ">> Building toolchain image ($IMAGE, base $BASE)…"
  docker build -f Dockerfile.build --build-arg BASE="$BASE" -t "$IMAGE" "$REPO"
  echo ">> [$target] depends + cmake + ninja  (HOST=$HOST, NO_QT=${NO_QT:-0})"

  # Pass BUILD_BITCOIN_QT explicitly (ON/OFF) — relying on the default lets a
  # cached OFF from a prior NO_QT configure of the same build dir silently win.
  QT_TARGET="bitfinite-qt"; QT_CMAKE="-DBUILD_BITCOIN_QT=ON"
  [ -n "$NO_QT" ] && { QT_TARGET=""; QT_CMAKE="-DBUILD_BITCOIN_QT=OFF"; }
  # DNS seeder is a Linux-only target (not built for the mingw/Windows host).
  GLIBC_FLAG=""; SEEDER_CMAKE="-DBUILD_BITCOIN_SEEDER=OFF"; SEEDER_TARGET=""
  if [ "$target" = linux ]; then
    GLIBC_FLAG="-DENABLE_GLIBC_BACK_COMPAT=ON"
    SEEDER_CMAKE="-DBUILD_BITCOIN_SEEDER=ON"; SEEDER_TARGET="bitcoin-seeder"
  fi

  docker run --rm -v "$REPO":/work -w /work \
    -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" \
    -e HOST="$HOST" -e PLAT="$PLAT" -e TARGET="$target" \
    -e NO_QT="$NO_QT" -e QT_TARGET="$QT_TARGET" -e QT_CMAKE="$QT_CMAKE" -e GLIBC_FLAG="$GLIBC_FLAG" \
    -e SEEDER_CMAKE="$SEEDER_CMAKE" -e SEEDER_TARGET="$SEEDER_TARGET" -e EXT="$EXT" \
    "$IMAGE" bash -euxo pipefail -c '
      git config --global --add safe.directory /work
      # Reconfigure from scratch when a configuration input is newer than the
      # cache. CMake will not re-run find_package for something already in
      # CMakeCache.txt, so editing a toolchain file or CMAKE_CXX_STANDARD is
      # otherwise silently ignored and you get a green build of stale config.
      # This has to happen in the container: the build dir is root-owned here,
      # so `rm -rf build-<target>` from the host fails with EPERM.
      if [ -f "build-$TARGET/CMakeCache.txt" ]; then
        for f in CMakeLists.txt src/CMakeLists.txt "cmake/platforms/$PLAT.cmake" \
                 depends/packages/boost.mk depends/packages/packages.mk; do
          if [ -e "$f" ] && [ "$f" -nt "build-$TARGET/CMakeCache.txt" ]; then
            echo ">> $f is newer than the cmake cache - reconfiguring from scratch"
            rm -rf "build-$TARGET"
            break
          fi
        done
      fi
      make -C depends -j"$(nproc)" HOST="$HOST" ${NO_QT:+NO_QT=1}
      cmake -GNinja -B "build-$TARGET" -S . \
        -DCMAKE_TOOLCHAIN_FILE="cmake/platforms/$PLAT.cmake" \
        -DENABLE_MAN=OFF -DCLIENT_VERSION_IS_RELEASE=ON \
        $SEEDER_CMAKE $GLIBC_FLAG $QT_CMAKE
      ninja -C "build-$TARGET" bitfinited bitfinite-cli bitfinite-tx bitfinite-wallet $QT_TARGET $SEEDER_TARGET
      # Strip INSIDE the container: the mingw strip only exists here, not on a
      # CI host, so a host-side strip silently no-ops and ships huge unstripped
      # Windows binaries.
      STRIP=strip; [ "$TARGET" = win ] && STRIP=x86_64-w64-mingw32-strip
      for f in bitfinited bitfinite-cli bitfinite-tx bitfinite-wallet \
               ${QT_TARGET:+qt/bitfinite-qt} ${SEEDER_TARGET:+seeder/bitcoin-seeder}; do
        "$STRIP" "build-$TARGET/src/$f$EXT" || true
      done
      chown -R "$HOST_UID:$HOST_GID" "build-$TARGET" depends 2>/dev/null || true
    '

  # ---- package on host ----
  ARCH=x86_64
  # A NO_QT build is NOT a release: it has no GUI. Name it so it can never be
  # mistaken for one, uploaded by hand, or matched by release-checksums.txt.
  # After 3.1.1 a 12.9 MB NO_QT tarball sat in dist/ next to a 31 MB published
  # release of the same name, with a SHA256SUMS matching neither.
  NAME="bitfinite-v${VERSION}-${ARCH}-${OS}${NO_QT:+-noqt}"
  OUT="dist/$NAME"
  rm -rf "$OUT" && mkdir -p "$OUT/bin"
  for b in bitfinited bitfinite-cli bitfinite-tx bitfinite-wallet; do
    cp "build-$target/src/$b$EXT" "$OUT/bin/"
  done
  [ -z "$NO_QT" ] && cp "build-$target/src/qt/bitfinite-qt$EXT" "$OUT/bin/"
  [ -n "$SEEDER_TARGET" ] && cp "build-$target/src/seeder/bitcoin-seeder$EXT" "$OUT/bin/"
  # (binaries were already stripped inside the container, where the mingw strip lives)
  ( cd "$OUT/bin" && sha256sum * ) > "$OUT/SHA256SUMS"
  ( cd dist && case "$PKGEXT" in
        tar.gz) tar czf "$NAME.tar.gz" "$NAME" ;;
        zip)    zip -qr  "$NAME.zip"    "$NAME" ;;
     esac )
  ( cd dist && sha256sum "$NAME.$PKGEXT" >> SHA256SUMS )
  echo ">> [$target] packaged: dist/$NAME.$PKGEXT"
done

echo ">> Done. Artifacts in dist/:"
ls -lh dist/*.tar.gz dist/*.zip dist/SHA256SUMS 2>/dev/null | awk '{print $5, $9}'

# Fail loudly rather than ship a checksum file that does not describe this build.
echo ">> SHA256SUMS for this run:"
sed 's/^/     /' dist/SHA256SUMS
if [ "$(wc -l < dist/SHA256SUMS)" -ne "${#TARGETS[@]}" ]; then
  echo "ERROR: SHA256SUMS has $(wc -l < dist/SHA256SUMS) entries for ${#TARGETS[@]} target(s)" >&2
  exit 1
fi
if grep -qv "v${VERSION}-" dist/SHA256SUMS; then
  echo "ERROR: SHA256SUMS references something other than v${VERSION} — refusing to ship it" >&2
  grep -v "v${VERSION}-" dist/SHA256SUMS | sed 's/^/     /' >&2
  exit 1
fi
( cd dist && sha256sum -c SHA256SUMS >/dev/null ) \
  && echo ">> checksums verified against the built artifacts" \
  || { echo "ERROR: SHA256SUMS does not match the files in dist/" >&2; exit 1; }
