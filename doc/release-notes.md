# Release Notes for BitFinite Node version 3.1.1

BitFinite Node version 3.1.1 is now available from:

  https://github.com/bitfinitechain/bitfinite-core/releases/tag/v3.1.1

**Drop-in upgrade from 3.1.0 or 3.0.x — no reindex, no wallet migration, no
consensus change.** Stop the node, swap the binaries, start it again.

This is the second tranche of the BCHN v27→v29 security backport. Where 3.1.0
concentrated on the DNS seeder and resource exhaustion, this one is about
undefined behaviour on paths a remote peer can reach.

## Fixes

- **CBloomFilter CVE backport.** Removes the `isEmpty`/`isFull` flags, a
  backport of Bitcoin Core PR 18806, with an additional guard against division
  by zero in `CBloomFilter::Hash` under all inputs. Bloom filters are built
  from data supplied by peers.
- **Undefined behaviour in `CNode::ReceiveMsgBytes`** — directly on the network
  input path, reached by every message from every peer.
- **Undefined behaviour in `CConnMan::SocketSendData`** for messages over 2 GiB.
- **Undefined behaviour in `AddToCompactExtraTransactions`.**
- **`MSG_FILTERED_BLOCK` no longer reads the block from disk when the peer has
  set no filter.** Previously any peer could make a node do disk reads for
  nothing.

## Not in this release

- **`-peerratelimit` is still deferred.** We said in 3.1.0 that it was next.
  It is not here, and the reason is worse than we thought: it needs upstream's
  `CNode` lifetime refactor, and that refactor sits on a stack of roughly 30
  net-layer commits — about 840 added and 990 removed lines across `net.cpp`,
  `net.h` and `net_processing.cpp`. Cherry-picking it alone silently drags in
  unrelated work (parallel compact block downloads, the `msghand` sleep rewrite)
  or means hand-merging a shared-pointer refactor into older surrounding code.
  Either way we would be shipping p2p code in a shape upstream has never run.
  It needs a sequenced port of that stack, which is its own piece of work with
  its own testing, not a line item in a security release.
- **`DisconnectedBlockTransactions::addForBlock` assert → `CHECK_NONFATAL`**
  is deferred. The commit is 13 lines, but upstream had already rewritten the
  function's algorithm underneath it and its new test needs block fixture data
  we do not carry.
- **The four May-2026 consensus CHIPs** remain out of scope: adopting them is a
  hard fork requiring coordinated miner activation.

## Verify

```
sha256sum -c SHA256SUMS
```
