# Release Notes for BitFinite Node version 3.2.2

BitFinite Node version 3.2.2 is now available from:

  https://github.com/bitfinitechain/bitfinite-core/releases/tag/v3.2.2

**This is a security release. Upgrade.** It is still a drop-in upgrade from any
3.x release: no reindex, no wallet migration, no consensus change, no
configuration change. Stop the node, swap the binaries, start it again.

Unlike 3.2.1, this one does affect a node that is already running and synced.
Two of the issues below let an unauthenticated peer crash or exhaust the memory
of any reachable node. If you run a node, this is worth doing today.

The issues were reported by **OpenVuln**, an automated vulnerability analysis
project, which reviewed this tree and supplied reproductions:

  https://huggingface.co/spaces/zai-org/OpenVuln

Credit where it is due: the report was specific, it was right, and several of
the findings had been sitting in the code since the fork. Every finding was
reproduced against our own source before being fixed, and every fix carries a
regression test that was confirmed to fail without it.

Two places where we diverged from the suggested patches, both noted below with
the reason: the compact block fix, where the upstream approach returns a value
that converts to the wrong boolean, and the seeder user-agent fix, where the
suggested include does not compile in this tree.

## What was wrong, and what it meant

### A duplicate message could stop the node

`PartiallyDownloadedBlock` is single use: reconstructing a compact block
consumes it. That was enforced with an assertion, and the compact block handler
has a deliberately forgiving path that leaves the consumed object reachable. A
peer could therefore send a second reply for the same block and abort the
process.

Two things made it worse than it looks. Assertions are compiled into our
released binaries, so this was a crash and not a debug check. And a node that
dies is a node that restarts, which re-opens the window described next.

This is the same class as CVE-2024-35202. Both Bitcoin Core and Bitcoin Cash
Node fixed it after our fork point, and the fix was never carried across.

**A note for anyone porting the upstream patch.** The upstream version returns
a status enum from a function that returns `bool`. Because the success value is
zero, the error converts to `true`, which tells the caller every transaction is
already present and suppresses the request it still needs to send. Our version
returns `false`. If you are comparing us to upstream here, the difference is
deliberate.

### Block headers were stored without limit

Every check applied to an incoming block header is self contained: the hash is
unique, the work matches what the header claims, the previous block is known,
the timestamp is in range. Not one of them bounds how *many* headers a peer can
make us store, and the index is never pruned. The only check that ties a header
to our chain is the finalization check, and it does nothing until a block has
been finalized, which is two hours after every process start and not before a
block is connected. That state is not written to disk, so every restart opens
the window again.

The effect is that a peer could park unbounded header data in memory during that
window, and it was written to the block index on disk and read back at every
startup. One wave kept costing memory until the data directory was rebuilt.

This is the CVE-2019-25220 class. The real upstream answer is headers
pre-synchronization, which is a much larger change and is **not** in this
release. What is here instead: while nothing has been finalized yet, headers
must descend from a point `maxreorgdepth` behind the current tip. That closes
the window for a node that is already synced, which is the case that was
reachable at any time.

**This does not cover a node still performing its initial sync**, nor one run
with `-maxreorgdepth=-1`. Said plainly because it is the limitation of the fix.

### The DNS seeder would vouch for anyone

The seeder decides which peers a new node connects to first, so its checks are a
security control for the whole network, not just for itself. Its only check that
a crawled peer is on our chain could be skipped by that peer simply saying so:
the condition was keyed on a block height the peer reports about itself. Any
host that spoke the protocol was recorded as verified and served in the DNS
answers.

The check no longer reads the peer's self-reported height when deciding whether
to trust it, and the block header a peer presents must now carry valid proof of
work rather than being accepted unexamined.

**This raises the cost of the attack. It does not eliminate it**, and that is
worth being direct about. The proof-of-work floor on this network is low enough
that a determined attacker can still produce a qualifying header, and the
seeder holds no chain of its own, so it cannot judge whether that header's
difficulty is plausible for its height. Closing this properly is a design
change rather than another check, and it is not in this release.

### The seeder could be made to exhaust its own memory

Three separate problems, all in the crawler:

- The user-agent string a peer reports was read without the length limit the
  node itself applies to the same field. The allocation followed the length the
  peer *claimed*, so a message of about a hundred bytes could ask for tens of
  megabytes, across every crawler thread.
- The per-connection limit on harvested addresses did not actually stop
  collection. Past the limit, each further message still contributed one more
  address for as long as the connection was held open.
- The address database had no size limit at all, and the bloat was written to
  disk and reloaded at every start.

All three are now bounded. An allocation failure in a crawler thread also used
to terminate the whole process; it now fails that one crawl.

**A note for anyone porting this one too.** The obvious fix is to reuse the
node's own limit constant by including its networking header. That does not
compile in the seeder: the header pulls in the address manager, which declares
a class with the same name as one of the seeder's own. That collision is very
likely why the seeder never inherited the node's limit in the first place. The
value is mirrored in the seeder instead, with a comment to keep the two equal.

## If you run the DNS seeder

This section is for us and for anyone running `bitcoin-seeder`. Node operators
can skip it.

The stored "this peer was verified" flag is no longer trusted across the
upgrade, because every flag written by an older seeder was granted by the check
that has just been fixed. The database record version changes accordingly and
each entry has to prove itself again on the next crawl.

**Expect the served set to be empty for a few minutes after restart.** During
that window the seeder answers from untested entries rather than returning
nothing. This is the intended behavior and not a fault. Plan the restart
accordingly, and do not deploy it at the same time as anything else that
affects bootstrapping.

## Functional test framework

The functional test suite could not start a node, and had not been able to
since the binaries and the configuration file were renamed. The framework was
still looking for the old binary names and still writing the old configuration
file name, so a test node never read its own configuration, came up on the wrong
network and collided with its siblings on a single port.

This is test-only and does not affect the node. It is called out because the
suite was reporting a connection failure that pointed nowhere near the cause.

One test, `p2p_dos_header_tree.py`, still fails. It carries block header data
inherited from upstream that cannot connect to our genesis, so it needs new data
rather than a code fix. Stated here rather than quietly excluded.

## Components in this release

Unchanged, and listed because absence of news is not neutral. The Linux build
ships `bitfinited`, `bitfinite-cli`, `bitfinite-tx`, `bitfinite-wallet`,
`bitfinite-qt` and `bitcoin-seeder`. The Windows build ships the same set
without `bitcoin-seeder`, which is a Linux-only target. Both are identical to
3.2.1 in what they contain.

## Verifying your download

Both builds ship unsigned. There is no code-signing certificate for BitFinite
yet, so the published `SHA256SUMS` file is the trust mechanism. Check it rather
than relying on the operating system to vouch for the binary.

```
sha256sum -c SHA256SUMS --ignore-missing
```

On Windows, PowerShell:

```
Get-FileHash bitfinite-v3.2.2-x86_64-windows.zip -Algorithm SHA256
```

**Windows will warn you.** SmartScreen and Defender flag unsigned executables
from the internet, and a node that opens network sockets and holds a wallet is
exactly the shape of thing they warn about. The warning is about the missing
signature, not about anything detected in the binary.

**One asymmetry worth knowing.** The Linux build reproduces: build it yourself in
the same container and you get the same binary, so the hash is something you can
independently confirm. The Windows build does not reproduce, so its checksum
attests only that the file you downloaded is the file CI produced.
