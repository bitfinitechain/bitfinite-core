> **Archived.** Notes for v3.1.3, kept as a historical record. Current notes
> live in `doc/release-notes.md`.

# Release Notes for BitFinite Node version 3.1.3

BitFinite Node version 3.1.3 is now available from:

  https://github.com/bitfinitechain/bitfinite-core/releases/tag/v3.1.3

**Drop-in upgrade from 3.1.x or 3.0.x on mainnet — no reindex, no wallet
migration, no consensus change.** Stop the node, swap the binaries, start it
again.

**Testnet users must delete their testnet data directory.** The testnet genesis
block changed in this release, so a node started against an existing
`testnet3/` directory will refuse to run. See "Public testnet" below. Mainnet
and regtest are unaffected.

Every change to consensus-reachable code was reviewed against the previous tag
before release. Mainnet block validation is byte-for-byte identical to 3.1.2.

## Fixes

- **`verificationprogress` reported a constant, not a measurement.** On mainnet
  it returned exactly `1.0` at every height, so a node three blocks into initial
  sync described itself as fully synced. `chainTxData` shipped with a
  transaction count and rate of zero, and `GuessVerificationProgress` divides
  the chain's transaction count by an estimate built from those constants; with
  both at zero the estimate collapses to the count itself and the ratio is
  always one.

  Measured on a fresh node before the fix: `1.0` at height 4163 of 16434. After:
  `0.249` at 4163, `0.672` at 11181, `0.9998` at the tip.

  This matters to anyone gating an action on whether the node is caught up. An
  exchange crediting deposits on that field would have been acting on a chain
  missing twelve thousand blocks. `initialblockdownload` was always correct and
  remains the field to rely on; this makes the other one honest as well.

  Testnet had the same defect in the opposite direction — it still carried
  Bitcoin Cash's transaction count of 63,972,968, so a fully synced testnet node
  reported `0.0000017`. Both networks now carry values measured from the live
  chains with `getchaintxstats`.

- **`-expire=1` no longer expires a BitFinite node on Bitcoin Cash's schedule.**
  The node-expiry date was inherited from BCH's Upgrade 11 scheduling, 15 May
  2025, which is in the past. A node started with `-expire=1` therefore expired
  immediately and disabled its own RPC. The default has always been forced off
  in BitFinite, so no default configuration was affected, but the option was
  unusable. BitFinite schedules no expiry date; the value is now zero on every
  network and the mechanism does nothing unless a date is supplied explicitly.

- **Undefined behaviour in `CNoDestination::operator<`.** It returned `true`
  unconditionally, so both `a < b` and `b < a` held for equivalent values. That
  is not a strict weak ordering, and `CTxDestination` goes into
  `std::set` and `std::map` in `GetAddressGroupings`, `GetAddressBalances`,
  `ListCoins` and the address book. A wallet holding two non-standard outputs is
  enough to reach it, and libstdc++'s `std::sort` can read past the end of a
  range given a comparator like this. Backport of BCHN's "Trivial: Prevent UB in
  class CNoDestination". Not consensus code — `CNoDestination` is an address
  classification used by the wallet and RPC, never by script validation.

## Public testnet

BitFinite now operates a testnet, reachable without configuration:

| | |
|---|---|
| P2P seed | `testnet-seed.bitfinitechain.org:29768` |
| Genesis | `00000000498add4157e47db0e5b06bdedd668af44c60762c37992be703d1ed2e` |
| Network magic | `BFte` |
| Address prefix | `bfxtest:` |
| Default RPC port | 29769 |

```
bitfinited -testnet -daemon
```

The genesis block was re-mined for this release, which is why an existing
testnet data directory has to go. The previous testnet genesis carried a
timestamp of 1296688604 — two seconds after Bitcoin's testnet3 genesis, in
February 2011 — inherited at the fork and never updated. That was not cosmetic.
The ASERT anchor time tracks genesis time, so the difficulty algorithm was
measuring a fifteen-year backlog against a chain at height zero: it expected
roughly 818,000 blocks to exist, saw none, and clamped the target to `powLimit`.
Difficulty could never have risen off diff-1 for the next 818,000 blocks, and
the first real miner pointed at the chain would have raced it through all of
them. Mainnet never had this defect; its anchor time is its own genesis time.

Testnet matches mainnet on every upgrade activation height, which is not true of
`testnet4`, `scalenet` or `chipnet` — those still carry Bitcoin Cash's heights
and remain unoperated. It deliberately differs on proof of work: ten-minute
target spacing against mainnet's five, and minimum-difficulty blocks permitted
after twenty minutes, which mainnet does not allow. Treat it as a rehearsal for
consensus, not for block timing.

Testnet coins have no value and the chain carries no permanence guarantee.

## Other changes

- **The Upgrade 11 gate was removed.** BitFinite has not adopted Bitcoin Cash's
  Upgrade 11, and the VM Limits and BigInt CHIPs change which scripts are valid,
  so adopting them would be a hard fork of this chain rather than a backport.
  The gate was never wired to any rule here: it was declared, it returned true
  from 15 May 2025 onward because the activation time was inherited, and nothing
  in the node ever asked. Its only caller was its own unit test. Keeping it cost
  nothing at runtime and cost accuracy everywhere else, because the source
  asserted an upgrade this chain does not implement.

- **The startup warning about unoperated networks is accurate again.** It was
  describing the testnet as it existed before it was rebuilt, and printed on
  every testnet start. Testnet is excluded and named as the supported public
  test chain; the warning remains for `testnet4`, `scalenet` and `chipnet`,
  where its description still holds.

- **`SECURITY.md`'s genesis verification table listed mainnet values that do not
  reproduce.** It recorded `1782432000 / 1870395023`, from an earlier re-mine,
  and marked both the hash match and the proof of work as verified. Those values
  hash to neither the asserted genesis nor valid proof of work, so anyone
  auditing the chain from that document could not have reproduced it.
  `chainparams.cpp` was always correct; the table was stale. All six networks
  have been re-derived and now verify.

- **`contrib/genesis/` is new** — the tool that found the above. It mines or
  verifies a genesis nonce, so the claim in that table can be checked rather
  than trusted. Four cores verify or find a nonce at `nBits` 0x1d00ffff in
  around 77 seconds.

## Verifying your download

Both builds ship unsigned. There is no code-signing certificate for BitFinite
yet, so the published `SHA256SUMS` file is the trust mechanism — check it rather
than relying on the operating system to vouch for the binary.

```
sha256sum -c SHA256SUMS --ignore-missing
```

On Windows, PowerShell:

```
Get-FileHash bitfinite-v3.1.3-x86_64-windows.zip -Algorithm SHA256
```

and compare against the matching line in `SHA256SUMS`.

**Windows will warn you.** SmartScreen and Defender flag unsigned executables
from the internet, and a node that opens network sockets and holds a wallet is
exactly the shape of thing they warn about. The warning is about the missing
signature, not about anything detected in the binary. If that is not acceptable
for your environment, run the Linux build, or run Windows inside a VM.

**One asymmetry worth knowing.** The Linux build reproduces: build it yourself
in the same container and you get the same binary, so the hash is something you
can independently confirm. The Windows build does not reproduce, so its checksum
attests only that the file you downloaded is the file CI produced. That is a
real difference in how much the two checksums prove, and it is why
`scripts/release-checksums.txt` distinguishes binary hashes from archive hashes.

## For developers

- **`miner_tests` was ported to regtest**, taking it from a `SIGABRT` and 111
  failures to 3. It remains excluded from the default run for those three,
  documented in `scripts/run-tests-docker.sh`. The exclusion note previously
  blamed a stale nonce table; the real cause is that BitFinite activates every
  upgrade at height 0 while upstream activates Magnetic Anomaly at mainnet
  height 556766 and Upgrade9 at 792772. Tests that mine a hundred blocks
  therefore run upstream in a pre-2018 rule regime that BitFinite does not have.
  Minimum transaction size, `SCRIPT_VERIFY_SIGPUSHONLY` and
  `SCRIPT_VERIFY_CLEANSTACK` all reject its constructions here and never touch
  it upstream. Worth knowing before diagnosing any other inherited test.

- The default suite is unchanged at 117 of 120 suites passing.
