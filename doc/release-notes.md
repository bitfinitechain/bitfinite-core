# Release Notes for BitFinite Node version 3.2.1

BitFinite Node version 3.2.1 is now available from:

  https://github.com/bitfinitechain/bitfinite-core/releases/tag/v3.2.1

**This is a drop-in upgrade from any 3.x release — no reindex, no wallet
migration, no consensus change, no configuration change.** Stop the node, swap
the binaries, start it again.

**Nothing in this release affects a node that is already running and synced.**
Everything here changes how a *new* node reaches the network and how quickly it
catches up. If your node is up, there is no reason to hurry.

## Fixed seeds: DNS is no longer a single point of failure

Until now `vFixedSeeds` was empty on every network, which meant DNS seeding was
the only way in. A fresh node with no `peers.dat` and no working resolver could
not reach the network at all — and `seed.bitfinitechain.org` is one name, on one
provider.

This release compiles in four mainnet nodes and one testnet node as a fallback.
Every one of them is a host we operate and already publish through DNS, so
nothing that was private has been made public; that is the deliberate trade for
removing the single point of failure. A fixed seed that later goes away is
harmless — the client simply tries the next.

**They are a fallback, not a shortcut.** The node adds them only when its
address manager is still empty 60 seconds after startup. In normal operation DNS
answers first and the fixed seeds are never touched, so do not be alarmed if you
never see them used.

Verified rather than assumed: a fresh node started with `-dnsseed=0` connected to
all four seeds and synced the full chain.

**If you run a node others rely on**, nothing is required of you. If you would
like it added to a future list, get in touch — the list lives in
`contrib/seeds/nodes_main.txt`.

## Faster initial sync

`nMinimumChainWork` and `defaultAssumeValid` were pinned at height 2639 and had
fallen roughly 14,000 blocks behind. Both now point at height 16640, about 200
deep at the time of this release.

For a new node this means signature verification is skipped below that block
during initial sync, and a presented chain with less accumulated work is rejected
outright. `chainTxData` is refreshed to the same block, so the sync progress
percentage reflects the current chain rather than a months-old estimate.

These are trust anchors for syncing, not consensus rules. They do not change
which blocks are valid.

## Developer tooling for the removed networks

Version 3.2.0 removed testnet4, scalenet and chipnet but left their tooling
behind, and it had started to rot: `generate-seeds.py` still opened
`nodes_testnet4.txt`, `nodes_scalenet.txt` and `nodes_chipnet.txt`, so deleting
those files alone would have made the script crash rather than simply produce
dead output.

Worth stating plainly, because it was the more serious half: every `nodes_*.txt`
in `contrib/seeds` was inherited **Bitcoin Cash** data. `nodes_main.txt` listed
94 peers on port 8333, none of them ours, and the generator hardcoded 8333/18333
as the mainnet and testnet ports. Anyone regenerating `chainparamsseeds.h` would
have compiled Bitcoin Cash nodes into a BitFinite binary. It never shipped,
because `vFixedSeeds` was cleared — the same emptiness this release fills. The
lists are now ours and the ports are 19768 and 29768.

The functional test framework was also still using Bitcoin Cash's network magic
bytes, which is fixed here. Test-only; it does not affect the node.

## Verifying your download

Both builds ship unsigned. There is no code-signing certificate for BitFinite
yet, so the published `SHA256SUMS` file is the trust mechanism — check it rather
than relying on the operating system to vouch for the binary.

```
sha256sum -c SHA256SUMS --ignore-missing
```

On Windows, PowerShell:

```
Get-FileHash bitfinite-v3.2.1-x86_64-windows.zip -Algorithm SHA256
```

**Windows will warn you.** SmartScreen and Defender flag unsigned executables
from the internet, and a node that opens network sockets and holds a wallet is
exactly the shape of thing they warn about. The warning is about the missing
signature, not about anything detected in the binary.

**One asymmetry worth knowing.** The Linux build reproduces: build it yourself in
the same container and you get the same binary, so the hash is something you can
independently confirm. The Windows build does not reproduce, so its checksum
attests only that the file you downloaded is the file CI produced.
