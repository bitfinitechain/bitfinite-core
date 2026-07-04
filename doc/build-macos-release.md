# BitFinite Core — macOS release build (handoff)

macOS is **not** built by CI (the Docker/`depends` cross-build can't target Apple
Silicon). Build it natively on a Mac with Homebrew. On an Apple-Silicon Mac this
produces **arm64** binaries; on an Intel Mac, **x86_64**. Do it on whichever
architecture you want to ship (ideally both, on separate machines).

Everything below is copy-paste into Terminal.

## 1. Prerequisites

```bash
# Xcode command-line tools (compiler + macOS SDK)
xcode-select --install    # click Install in the popup, if not already present

# Homebrew (https://brew.sh) — skip if already installed
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Build deps (from doc/build-osx.md)
brew install berkeley-db boost cmake libevent miniupnpc ninja openssl qrencode qt@5 zeromq gh
```

`qt@5` and `openssl` are keg-only, but the project's cmake locates them via its
Homebrew helper — no extra flags needed for a standard install.

## 2. Get the source (private repo)

```bash
gh auth login          # GitHub.com > HTTPS > your PAT/browser
gh repo clone bitfinitechain/bitfinite-core
cd bitfinite-core
```

## 3. Configure + build (GUI + CLI + daemon)

```bash
cmake -GNinja -B build -S . -DENABLE_MAN=OFF
ninja -C build bitfinited bitfinite-cli bitfinite-tx bitfinite-wallet bitfinite-qt
# bitcoin-seeder is optional on macOS; add it to the ninja line if it builds:
# ninja -C build bitcoin-seeder
```

- Headless-only? add `-DBUILD_BITCOIN_QT=OFF` and drop `bitfinite-qt`.
- If the **wallet** fails on the Homebrew Berkeley-DB version, either
  `brew install berkeley-db@4` and re-run cmake, or build without the wallet:
  `-DBUILD_BITCOIN_WALLET=OFF` (drop `bitfinite-wallet` from the ninja line).

## 4. Verify the genesis (must match live mainnet)

```bash
TMP=$(mktemp -d)
./build/src/bitfinited -datadir="$TMP" -daemon
sleep 5
./build/src/bitfinite-cli -datadir="$TMP" getblockhash 0
# EXPECT: 000000000900096d5b0f4a3489f919362f12fce06524e15074c3cd3c19aeabea
./build/src/bitfinite-cli -datadir="$TMP" stop
rm -rf "$TMP"
```

If block 0 doesn't match, stop — the build is wrong; don't ship it.

## 5. Package

```bash
ARCH=$(uname -m)                       # arm64 or x86_64
VER=3.0.0.1
NAME="bitfinite-v${VER}-${ARCH}-macos"
mkdir -p "dist/$NAME/bin"
for b in bitfinited bitfinite-cli bitfinite-tx bitfinite-wallet; do
  cp "build/src/$b" "dist/$NAME/bin/"
done
cp build/src/qt/bitfinite-qt "dist/$NAME/bin/" 2>/dev/null || true
strip "dist/$NAME"/bin/* 2>/dev/null || true
( cd "dist/$NAME/bin" && shasum -a 256 * ) > "dist/$NAME/SHA256SUMS"
( cd dist && tar czf "$NAME.tar.gz" "$NAME" )

# Optional: a .app + .dmg for the GUI
ninja -C build osx-dmg    # produces build/BitFinite-Qt.dmg (name may vary)
```

## 6. (Optional) Sign + notarize for distribution

Unsigned apps trigger Gatekeeper ("unidentified developer"); users must
right-click → Open. For a clean install you need an Apple Developer ID:

```bash
codesign --deep --force --options runtime --sign "Developer ID Application: <YOU>" \
  build/src/qt/BitFinite-Qt.app
xcrun notarytool submit "dist/$NAME.tar.gz" --apple-id <id> --team-id <team> \
  --password <app-specific-pw> --wait
xcrun stapler staple build/src/qt/BitFinite-Qt.app
```

## 7. Attach to the GitHub release

```bash
gh release upload v3.0.0.1 -R bitfinitechain/bitfinite-core \
  "dist/$NAME.tar.gz" "dist/$NAME/SHA256SUMS" --clobber
```

Now the macOS build sits alongside the CI-produced Linux + Windows assets on the
same release.
