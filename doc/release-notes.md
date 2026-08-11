# Release Notes for BitFinite Node version 3.1.0

BitFinite Node version 3.1.0 is now available from:

  https://github.com/bitfinitechain/bitfinite-core/releases/tag/v3.1.0

This is a security and robustness release. **Drop-in upgrade from 3.0.x — no
reindex, no wallet migration, no consensus change.** Stop the node, swap the
binaries, start it again.

## Why this release exists

BitFinite forked from Bitcoin Cash Node v27.0.0 in December 2023. Upstream is
now at v29.0.0, so two and a half years of their fixes were missing here. This
release ports the subset that changes how a node behaves under adversarial or
resource-starved conditions. The full ranked plan is in
`doc/upstream-security-backlog.md`.

## DNS seeder

The seeder serves `seed.bitfinitechain.org`, the network's dynamic bootstrap.
Two of the fixes here were verified by running 3.0.2 and 3.1.0 side by side
against the same failure:

- **A seeder that cannot bind its DNS port now says so.** Previously it printed
  `done`, reported `0 DNS requests` and kept running while answering nothing.
  A dead seed looked healthy.
- **It now shuts down cleanly and persists its database on the way out.** 3.0.2
  ignored SIGINT entirely and had to be killed, discarding every peer crawled
  since the last periodic dump.
- Fixed a race and undefined behaviour when starting DNS threads, replaced a
  thread-unsafe `std::rand()`, moved from pthreads to `std::thread`, and fixed
  the checkpoint check.

## Node robustness

- **Socket descriptor leak fixed**, along with the crash it eventually caused
  when the process ran out of file descriptors. This one matters on nodes with
  long uptime.
- **Log rate limiting** (`-logratelimit`, on by default): each source location
  gets a 1 MiB per hour quota, so a noisy or hostile peer cannot fill the disk.
  Suppressed messages are marked. `-logratelimit=0` disables it.
- **Control characters are filtered out of log messages**, so peer-supplied
  strings cannot manipulate a terminal reading the log.
- Thread-unsafe `strerror()` replaced with `SysErrorString()` throughout.

## Build

- **The project now builds as C++20** (was C++17), which the logging changes
  require, with Boost 1.77 in `depends` (was 1.70 — C++20 removed
  `std::allocator<T>::pointer`, which Boost 1.70 still used).
- **Linux and Windows now build in separate toolchain images.** Linux stays on
  Ubuntu 22.04 so the glibc floor for released binaries does not move; Windows
  moves to 24.04 for a mingw new enough to provide `<source_location>`.
- **Fixed: `CMAKE_PREFIX_PATH` did not include `depends`** on the Linux
  toolchain, so config-mode `find_package` resolved host libraries while headers
  came from `depends`. Every previous native Linux build linked mismatched Boost;
  it only worked because the versions happened to agree on the symbols used.
- **Fixed: the packaging version could disagree with the compiled-in version.**
  It is now read from `project()` in `CMakeLists.txt` and the build refuses to
  run on a mismatch — the same defect v3.0.2 was released to correct.
- **Fixed: the build directory is now reconfigured when a configuration input
  changes.** CMake caches `find_package` results, so toolchain edits were
  previously ignored and could produce a passing build of stale configuration.

## Not in this release

- **`-peerratelimit` (per-peer bandwidth limits) is deferred.** It was named in
  our announcement, and it did not make it: upstream's implementation is written
  against a `CNode` lifetime refactor we have not ported. Rewriting it against
  our current interface would mean shipping network-layer code in a form
  upstream has never run. It is first in line for the next release.
- **The four May-2026 consensus CHIPs from BCHN v29 are not included.** Those
  are Bitcoin Cash's network upgrade; adopting them here would be a hard fork
  requiring coordinated miner activation, and they must not ride along in a
  security release.

## Verify

```
sha256sum -c SHA256SUMS
```
