# Release Notes for BitFinite Node version 3.2.0

BitFinite Node version 3.2.0 is now available from:

  https://github.com/bitfinitechain/bitfinite-core/releases/tag/v3.2.0

**Mainnet is a drop-in upgrade from 3.1.x or 3.0.x — no reindex, no wallet
migration, no consensus change.** Stop the node, swap the binaries, start it
again. Testnet and regtest are likewise unaffected.

**This release removes three test networks.** If your configuration or scripts
pass `-testnet4`, `-scalenet` or `-chipnet`, or set `testnet4=1` and the like in
a config file, **the node will refuse to start**:

```
Error reading configuration file: Invalid configuration value testnet4
```

A leftover `[test4]`, `[scale]` or `[chip]` *section* is gentler — the node
starts and warns:

```
Warning: bitfinite.conf:1 Section [chip] is not recognized.
```

That is the one upgrade step in this release, and it is covered in full below.

## Removed: testnet4, scalenet and chipnet

These three networks were inherited whole when BitFinite forked Bitcoin Cash
Node, and they were never BitFinite networks in any meaningful sense. Each
carried Bitcoin Cash's genesis block, Bitcoin Cash's upgrade activation heights
and Bitcoin Cash's ASERT anchor. None had a DNS seed, a fixed seed, or a node
anyone operated. The startup code printed a warning telling you not to use them.

They existed to be warned about, so they are gone.

**What each was for, and why it did not transfer.** `chipnet` exists on Bitcoin
Cash to rehearse CHIPs roughly six months before they activate on mainnet — its
only distinguishing parameter was an earlier `upgrade10ActivationTime`. BitFinite
has not adopted those CHIPs, so a network for testing them early rehearsed
somebody else's schedule. `scalenet` is a large-block stress network, 256 MB
against 2 MB elsewhere; a sound idea, but stress-testing block propagation
requires peers, and it had none. `testnet4` is simply a testnet3 with shorter
history, which is only useful if it is running.

**If you want any of them back**, the honest versions look different: our own
genesis, our own spacing, an activation date for an upgrade we have actually
decided to adopt, and at least one node. That is a decision rather than a config
file, and nothing in this release forecloses it. The removed code is in git
history.

**What to do on upgrade.** Remove `testnet4=1`, `scalenet=1` or `chipnet=1` from
any config file — those stop the node from starting. Then remove any `[test4]`,
`[scale]` or `[chip]` sections, which only produce a warning but are now dead
weight.

Old `testnet4/`, `scalenet/` and `chipnet/` data directories are simply ignored
and can be deleted at your convenience. Mainnet, testnet and regtest data
directories are untouched.

## Removed: the pre-ASERT difficulty algorithms

`GetNextCashWorkRequired` (the cw144 algorithm) and `GetNextEDAWorkRequired` (the
emergency difficulty adjustment) are gone, along with the three test cases that
exercised them.

`GetNextWorkRequired` reaches those two only when Axion is not yet active, and
Axion is time-gated. Mainnet and testnet have genesis timestamps well past the
activation time, so both always took the ASERT branch. The three removed
networks kept Bitcoin Cash's 2020 genesis timestamps, which sit 83 to 88 days
*before* the activation time they also inherited — so on those chains alone the
pre-ASERT algorithms were live.

That distinction was worth establishing before deleting anything. An earlier note
in this repository asserted the legacy code was unreachable, and it was not.
With the networks gone it now is, on every chain this node can select.

The unreachable branch that remains returns a defined value rather than relying
on an assertion. This build compiles without `NDEBUG` on purpose, so assertions
survive into release, but that is a build setting: a toolchain that restored
`NDEBUG` would have turned a crash into undefined behaviour, in the function that
decides proof-of-work difficulty.

## Test suite: 120 of 120, with an empty exclusion list

For the first time, no suite is excluded from the default run.

The exclusion list was a visible work queue rather than a quiet silence — every
entry carried its reason in `scripts/run-tests-docker.sh`, printed into every CI
job summary. It is now empty, and `miner_tests`, `checkpoints_tests` and
`pow_tests` all pass.

Almost every one of those failures came from a single difference rather than
from stale test data, and it is worth stating because it will come up again.
**BitFinite activates every upgrade at height 0. Upstream activates them at real
historical heights, far above where these tests reach.** A test that mines a
hundred blocks therefore runs upstream in a pre-2018 rule regime that BitFinite
does not have, and four separate consensus rules broke a suite for exactly that
reason:

- **Minimum transaction size.** `miner_tests` built a 62-byte coinbase against a
  65-byte floor.
- **`SCRIPT_VERIFY_SIGPUSHONLY`.** It padded a scriptSig with `OP_DROP`, which is
  not a push.
- **`SCRIPT_VERIFY_CLEANSTACK`.** That same scriptSig left nineteen stack items.
- **CTOR.** `miner_tests` asserted which transaction sat at which index in a
  block, but canonical transaction ordering sorts them by txid, so those indices
  never described selection order here at all.

`checkpoints_tests` was hard-wired to Bitcoin's genesis and its 2009 blocks; it
now mines its own fork structure at run time. `pow_tests` inherited BCH's
600-second spacing and two-day half-life as literals throughout; those are
derived from the chain's own parameters now, so the cases test the algorithm
rather than one chain's numbers.

No expected value was rewritten to match our output. Where a suite could not
observe what it claimed to — selection preference under CTOR — the assertion was
replaced with what is observable and the gap documented, rather than adjusted
until it passed.

## Verifying your download

Both builds ship unsigned. There is no code-signing certificate for BitFinite
yet, so the published `SHA256SUMS` file is the trust mechanism — check it rather
than relying on the operating system to vouch for the binary.

```
sha256sum -c SHA256SUMS --ignore-missing
```

On Windows, PowerShell:

```
Get-FileHash bitfinite-v3.2.0-x86_64-windows.zip -Algorithm SHA256
```

**Windows will warn you.** SmartScreen and Defender flag unsigned executables
from the internet, and a node that opens network sockets and holds a wallet is
exactly the shape of thing they warn about. The warning is about the missing
signature, not about anything detected in the binary.

**One asymmetry worth knowing.** The Linux build reproduces: build it yourself in
the same container and you get the same binary, so the hash is something you can
independently confirm. The Windows build does not reproduce, so its checksum
attests only that the file you downloaded is the file CI produced.
