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

set -euo pipefail
cd "$(dirname "$0")/.."
REPO="$(pwd)"
IMAGE="${BFX_CORE_IMAGE:-bitfinite-core-build}"
VERSION="${VERSION:-3.0.0.1}"
NO_QT="${NO_QT:-}"
TARGETS=("$@"); [ ${#TARGETS[@]} -eq 0 ] && TARGETS=(linux win)

echo ">> Building toolchain image ($IMAGE)…"
docker build -f Dockerfile.build -t "$IMAGE" "$REPO"

mkdir -p dist
grep -q "^dist/" .gitignore 2>/dev/null || echo "dist/" >> .gitignore
grep -q "^build-linux/" .gitignore 2>/dev/null || printf "build-linux/\nbuild-win/\ndepends/x86_64-*/\ndepends/work/\ndepends/sources/\n" >> .gitignore

for target in "${TARGETS[@]}"; do
  case "$target" in
    linux) HOST=x86_64-linux-gnu;    PLAT=Linux64; EXT="";     OS=linux;   PKGEXT=tar.gz ;;
    win)   HOST=x86_64-w64-mingw32;  PLAT=Win64;   EXT=".exe"; OS=windows; PKGEXT=zip ;;
    *) echo "unknown target: $target (use linux|win)"; exit 1 ;;
  esac
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
    -e SEEDER_CMAKE="$SEEDER_CMAKE" -e SEEDER_TARGET="$SEEDER_TARGET" \
    "$IMAGE" bash -euxo pipefail -c '
      git config --global --add safe.directory /work
      make -C depends -j"$(nproc)" HOST="$HOST" ${NO_QT:+NO_QT=1}
      cmake -GNinja -B "build-$TARGET" -S . \
        -DCMAKE_TOOLCHAIN_FILE="cmake/platforms/$PLAT.cmake" \
        -DENABLE_MAN=OFF $SEEDER_CMAKE $GLIBC_FLAG $QT_CMAKE
      ninja -C "build-$TARGET" bitfinited bitfinite-cli bitfinite-tx bitfinite-wallet $QT_TARGET $SEEDER_TARGET
      chown -R "$HOST_UID:$HOST_GID" "build-$TARGET" depends 2>/dev/null || true
    '

  # ---- package on host ----
  ARCH=x86_64
  NAME="bitfinite-v${VERSION}-${ARCH}-${OS}"
  OUT="dist/$NAME"
  rm -rf "$OUT" && mkdir -p "$OUT/bin"
  for b in bitfinited bitfinite-cli bitfinite-tx bitfinite-wallet; do
    cp "build-$target/src/$b$EXT" "$OUT/bin/"
  done
  [ -z "$NO_QT" ] && cp "build-$target/src/qt/bitfinite-qt$EXT" "$OUT/bin/"
  [ -n "$SEEDER_TARGET" ] && cp "build-$target/src/seeder/bitcoin-seeder$EXT" "$OUT/bin/"
  # strip (mingw needs the cross strip)
  if [ "$target" = win ]; then STRIP=x86_64-w64-mingw32-strip; else STRIP=strip; fi
  $STRIP "$OUT"/bin/* 2>/dev/null || true
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
