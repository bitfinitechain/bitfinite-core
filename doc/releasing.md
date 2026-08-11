# Releasing BitFinite Core

**CI is the only publisher.** `.github/workflows/build-core.yaml` builds Linux and
Windows and attaches them to the GitHub release whenever a `v*` tag is pushed.
Do not also run `gh release create` or `gh release upload` — that is not a style
preference, it is the bug below.

## Why manual publishing is banned

For v3.1.0 the tag was pushed *and* the release was created by hand. Both
producers wrote the same assets, CI finished second (~52 min later) and replaced
everything. The checksums recorded minutes earlier described a tarball that no
longer existed, so `scripts/upgrade-node.sh` — which verifies before installing —
refused every host in the fleet.

Nothing was corrupted, and every binary inside was byte-identical. But the fleet
was unupgradeable until the pins were rebuilt from the published artifacts, and
the failure looked like tampering while it was diagnosed.

## The procedure

1. **Bump the version in one place.** `project(... VERSION x.y.z)` in the
   top-level `CMakeLists.txt`. `scripts/build-core-docker.sh` reads it from
   there and refuses to run if an explicit `VERSION=` disagrees — the packaging
   version and the compiled-in version cannot drift apart.

2. **Write `doc/release-notes.md`.** State what is *not* in the release as
   plainly as what is, especially anything previously announced.

   Also **keep listing components that were once called out, even when nothing
   about them changed.** `bitfinite-qt` was a headline in the 3.0.2 notes and
   then went unmentioned in 3.1.0 and 3.1.1 — it shipped in both, but silence
   after a previous mention reads as removal, and someone did read it that way.
   Absence of news is not neutral once you have made news of something.

3. **Build locally and test.** `scripts/build-core-docker.sh` produces artifacts
   in `dist/` for validation only — **`dist/` is never what gets published.**
   Run the binaries, sync a real datadir, exercise whatever behaviour changed.

   **Delete a partial build when you are done with it.** `NO_QT=1` is the fast
   validation path and it produces a package that looks like a release and is
   not one: after 3.1.1 there was a `bitfinite-v3.1.1-x86_64-linux.tar.gz` in
   `dist/` at 12.9 MB against the published 31 MB, with no GUI in it, plus a
   `dist/SHA256SUMS` naming a hash that matched nothing published. Nobody
   shipped it, but nothing stopped them either.

4. **Merge and tag.** Pushing the tag is what triggers the release. Nothing else
   is required, and nothing else should be done.

   ```
   git tag -a vX.Y.Z -m "..." && git push origin vX.Y.Z
   ```

5. **Wait for CI** (~50 min). `gh run list --limit 1`.

6. **Pin the published checksums.** Download the *published* assets — not
   `dist/` — and record the per-binary hashes in
   `scripts/release-checksums.txt`, then commit. Until this is done the upgrade
   scripts refuse the new version by design.

   ```
   gh release download vX.Y.Z -D /tmp/rel
   cd /tmp/rel && sha256sum -c SHA256SUMS      # release must be self-consistent
   tar xzf bitfinite-vX.Y.Z-x86_64-linux.tar.gz
   cat bitfinite-vX.Y.Z-x86_64-linux/SHA256SUMS   # the per-binary hashes to pin
   ```

7. **Deploy.** `scripts/upgrade-node.sh` for nodes, `scripts/upgrade-seeder.sh`
   for the DNS seeder — it lives outside the managed bin directory and the node
   script does not touch it.

## Notes worth keeping

- **Archive hashes are advisory, binary hashes are the gate.** Re-tarring
  identical binaries changes the archive hash; the binaries are what execute.
  See the header of `scripts/release-checksums.txt`.
- **Linux binaries reproduce across machines** — local Docker and GitHub runners
  produced identical bytes for all six. **Windows do not**: PE headers embed a
  build timestamp.
- **Verify a new gate in both directions.** Feed it a deliberately wrong hash and
  confirm it rejects, or you have only proven it does not crash.
- macOS is not built by CI; see `doc/build-osx.md`.
