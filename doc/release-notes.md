# Release Notes for BitFinite Node version 3.1.2

BitFinite Node version 3.1.2 is now available from:

  https://github.com/bitfinitechain/bitfinite-core/releases/tag/v3.1.2

**Drop-in upgrade from 3.1.x or 3.0.x — no reindex, no wallet migration, no
consensus change.** Stop the node, swap the binaries, start it again.

This release exists because the unit test suite was made to run again after a
long absence, and it found things. Nothing here changes how blocks are
validated: the changes to `chainparams.cpp` add a checkpoint at the genesis
block, which can only ever reject an alternative genesis, and the change to
`pow.cpp` restores an assertion that provably holds on every network reaching
that code. Both were verified line by line before tagging.

## Fixes

- **Genesis checkpoint restored on five of six networks.** Upstream ships a
  checkpoint at the genesis block on every network. Ours were emptied during the
  fork and only mainnet was ever restored. A genesis checkpoint bans forks that
  rewrite the genesis block — defence in depth that was silently dropped. The
  entry is derived from `consensus.hashGenesisBlock` rather than a literal, so it
  cannot drift from the block it pins.

- **Log lines are no longer lost on a crash after log rotation.** `setbuf` was
  applied to the file handle being closed rather than the newly opened one,
  leaving the reopened debug log fully buffered. Recent lines sat in memory
  instead of on disk — precisely when a crash makes them valuable. Present in
  upstream from 2018 through v28.0.1; **BCHN fixed this in v29.0.0 before we
  found it independently, and this is a backport of their fix**, not a discovery
  of ours.

- **ASERT overflow assertion re-enabled.** `assert((powLimit >> 224) == 0)`
  guards the headroom `CalculateASERT` relies on. It had been commented out
  during the genesis/anchor work and the disable was never necessary — the
  condition holds on mainnet, testnet and scalenet, and the only value that would
  trip it (regtest's) never reaches the function. No behavioural change; a guard
  on consensus arithmetic is worth having.

- **Log rate limiter completed.** The tier-2 security backport landed the tests
  for this facility without all of its implementation, so the suite stopped
  compiling. Added the missing `BCLog::ReconstructLogInstance`, a streaming
  operator for `LogRateLimiter::Status`, and a `chrono` overload of
  `CScheduler::scheduleFromNow`.

## Testing

- **CI now builds and runs the unit tests on every branch**
  (`.github/workflows/tests.yaml`). Previously nothing built `test_bitcoin` at
  all — the release pipeline produced binaries only, which is why a half-landed
  backport went unnoticed for two days.

- **117 of 120 suites pass.** `scripts/run-tests-docker.sh` runs them in the same
  container and toolchain that builds releases. The three exclusions each carry
  their reason in that script and are printed into every CI job summary.

- Six suites were repaired. Five had been broken by the original rebranding: a
  find-replace that landed inside values carrying a checksum or an encoding
  (address prefixes, a base58 `xpub`, and a client name that never existed). One,
  `transaction_tests`, assumed Bitcoin Cash's upgrade timeline, which does not
  apply to a chain that launched with every inherited upgrade already active.

## Documentation

- `doc/consensus-diff.md` — scoping document for an independent review of our
  changes against BCHN v27.0.0, including an honest account of what the fork
  process left behind.
- `doc/asert-response.md` — measured difficulty response to hashrate loss.

## Not in this release

No consensus change. No change to the difficulty algorithm's behaviour, block
validity, transaction rules, or the P2P protocol. Nodes running 3.1.1 and 3.1.2
remain fully compatible on the same chain.
