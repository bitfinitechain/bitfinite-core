# Upstream security backlog — BCHN v27.0.0 → v29.0.0

BitFinite Node forked from **Bitcoin Cash Node v27.0.0** (December 2023).
Upstream is at **v29.0.0** (May 2026). That gap is 439 commits, 262 of them
non-merge, and it contains hardening we do not have.

This is the porting plan. It is deliberately **not** a rebase: most of those
262 commits are the May 2026 consensus upgrade, build plumbing, or tests. The
list below is the subset that changes how a running node behaves under
adversarial or resource-starved conditions.

## Two buckets, kept apart

**Non-consensus hardening** — everything in the tiers below. Safe to port, no
fork, no miner coordination. Ships as a normal release.

**Consensus CHIPs** — `CHIP-2024-12 P2S`, `CHIP-2021-05-loops`,
`CHIP-2025-05 Functions`, `CHIP-2025-05 Bitwise`, and
`CHIP-2021-05-vm-limits`. These are Bitcoin Cash's May 2026 network upgrade.
Adopting them on BFX is a **hard fork** requiring coordinated activation
across our miners. Out of scope here, and hard to justify while blocks carry
zero transactions. Do not let them ride along in a security release.

## Conflict surface

Measured against `v27.0.0`: our fork changes 284 files under `src/`, almost
all of it the rebrand. The files these ports touch are nearly all untouched
by us:

| file | our changes |
|---|---|
| `src/net.cpp` | none |
| `src/bloom.cpp` | none |
| `src/logging.cpp` | none |
| `src/seeder/*` | none |
| `src/blockstorage.cpp` | none |
| `src/timedata.cpp` | none |
| `src/net_processing.cpp` | 21 lines added |

So these should cherry-pick close to clean. Expect friction from the
`bitcoin-*` → `bitfinite-*` rename in build files, not from the logic.

---

## Tier 1 — port first

Self-contained, high value, and each maps to a failure we can actually hit.

| # | commit | date | why it matters to us |
|---|---|---|---|
| 1 | `seeder: Fix potential UB/race condition when starting DNS threads` | 2024-03-19 | **We run this binary.** `seed.bitfinitechain.org` is the network's only dynamic bootstrap. |
| 2 | `seeder: Don't use std::rand(); it's not guaranteed thread-safe` | 2024-03-19 | same process, same threads |
| 3 | `seeder: Fix bug where if DNS server can't start, app is silent with no indication of error` | 2024-03-19 | a dead seeder that looks alive is the worst failure mode for bootstrap |
| 4 | `seeder: Gracefully exit and always persist db immediately on exit` | 2024-03-19 | loses the crawled peer set on restart today |
| 5 | `Fix seeder checkpoint check` | 2024-03-19 | |
| 6 | `Fix socket leaks leading to process running out of file descriptors` | 2024-04-22 | real resource exhaustion on long-uptime nodes; ours run for weeks |
| 7 | `Fixed a rare crash bug when process runs out of file descriptors` | 2024-04-22 | the crash that (6) leads to — `SaveAllToDisk()` on a null `FILE *` |
| 8 | `log: Add rate limiting to LogPrintf` (+ its unit tests) | 2025-12-06/07 | 1 MiB/hour quota per message. **seed-2 is at 90% disk**; log flooding is a live risk, not a theoretical one |
| 9 | ~~`Added per-peer traffic limit rules, -peerratelimit`~~ | 2025-12-22 | **MOVED TO TIER 5 — see below.** Not self-contained. |

### Correction 2 (measured): `-peerratelimit` needs a SEQUENCED PORT, not one prerequisite

The section below was still too optimistic. It said `ea755aa648` needs
`6d94eabf6e` first, as if that were a single prerequisite. Attempting it showed
otherwise.

`6d94eabf6e` does not sit on v27.0.0. Between them are **30 commits touching
the net layer** — cumulatively **+837 / −992 lines** across `net.cpp`, `net.h`
and `net_processing.cpp`. Cherry-picking the refactor alone produces conflicts
whose "upstream" side is *other commits' code*: `AddConnection()`, the
`msghand` sleep-interval rewrite (Tier 4), and
`IsBlockRequested` / `GetNodeBlockRequestStatus` from parallel compact block
downloads (Tier 5). Resolving them means either importing that work unreviewed
or hand-merging a shared-pointer refactor into older surrounding code.

So the real shape of this task is: **port the net-layer stack in order**
(msghand CPU fix, sendcmpct tidy-up, parallel compact blocks, `AddConnection`,
then the lifetime refactor, then `-peerratelimit`). That is a project with its
own build/soak cycle on the layer that carries the live pool — not a line item
in a security release.

**Lesson for the rest of this backlog:** scoping by "which files does it touch"
and "does our fork modify them" has now under-estimated three times — first
`NodeRef`, then the C++20/`std::source_location` dependency, now this. Before
committing to any remaining item, run `git log v27.0.0..<commit> -- <its files>`
and look at how deep the stack beneath it is.

### Correction 1: `-peerratelimit` is not a Tier 1 commit

Scoping this by file-level conflict surface was not enough. `ea755aa648` is
written against `NodeRef` (`std::shared_ptr<CNode>`), which does not exist in
our tree — it is introduced by `6d94eabf6e`, *"net: Refactor lifetime
management of CNode, use smart_ptr; put in map"*, the Tier 5 commit deferred
as invasive. The conflict is not textual; the whole `CNetMsgMaker` interface
changes signature from `CNode *` to `NodeRef`.

`6d94eabf6e` is 10 files, +305/-352, entirely in the network layer including
`denialofservice_tests.cpp` and `net_tests.cpp`. Two ways forward, both for a
later release:

1. Port `6d94eabf6e` first, then `ea755aa648` applies as written. Preferred —
   it keeps us on code upstream actually tests, and the refactor closes a
   use-after-free class on its own.
2. Rewrite `ea755aa648` against raw `CNode *`. Cheaper, but ships network-layer
   code in a form upstream has never run.

Do not attempt either in a release that also carries unrelated fixes. Note
this leaves us with **no per-peer bandwidth cap** until then, which is worth
weighing against the fact that the Bitcointalk announcement named it.

The seeder commits (1–5) come with several refactors from the same day that
are likely prerequisites. Take the whole `src/seeder/` series from
2024-03-19 rather than picking individual hunks.

## Tier 2 — unsafe behaviour on attacker-reachable paths

**Status: SHIPPED in v3.1.1**, except as noted. `#15` (`SysErrorString`) went out
in v3.1.0. `#16` is deferred — the commit is 13 lines, but upstream had already
rewritten `DisconnectedBlockTransactions::addForBlock`'s algorithm underneath it,
and its new test needs `Get_block877227` fixture data we do not carry. Softening
one assert on a reorg path does not justify porting an algorithm rewrite plus
test fixtures.


| # | commit | date | note |
|---|---|---|---|
| 10 | `net: Fix potential for UB in CNode::ReceiveMsgBytes` | 2024-12-06 | UB directly on the network input path |
| 11 | `net: Future-proof and prevent UB in CConnMan::SocketSendData for >2GiB messages` | 2024-05-19 | |
| 12 | `Bugfix: Fix potential UB in AddToCompactExtraTransactions` | 2024-07-11 | |
| 13 | `net: remove is{Empty,Full} flags from CBloomFilter, clarify CVE fix` | 2024-11-24 | backport of Core PR 18806 plus a division-by-zero guard in `CBloomFilter::Hash` |
| 14 | `net_processing: Avoid reading the block for MSG_FILTERED_BLOCK if no filter` | 2024-04-23 | free disk reads for any peer |
| 15 | `Replace all usages of thread-unsafe strerror() with SysErrorString` (+ the utility commit) | 2024-07-11 | |
| 16 | `Update DisconnectedBlockTransactions::addForBlock to use CHECK_NONFATAL rather than assert()` | 2024-12-22 | an `assert` on a reorg path is a remote shutdown |

## Tier 3 — worth more to us than to upstream

| # | commit | date | note |
|---|---|---|---|
| 17 | `p2p: Don't use timestamps from inbound peers` | 2021-11-29 | **BFX-specific value.** Our nodes hold 4–7 peers. On a network this small a couple of hostile inbounds is a large share of the time samples feeding adjusted time. On BCH it is noise; here it is leverage. |
| 18 | `[net] Ignore unlikely timestamps in version messages` | 2020-05-10 | pairs with 17 |

## Tier 4 — resource efficiency on small boxes

| # | commit | date |
|---|---|---|
| 19 | `net: Avoid excess CPU usage by msghand thread; fix "early wake up" logic` | 2024-12-08 |
| 20 | `Optimize INV TX relay, reduce inefficiency/redundant sorting` | 2025-02-24 |
| 21 | `Slightly modernize the code in blockstorage and add overflow checks` | 2025-08-01 |
| 22 | `backport: Fix compile warning, fix UB, and optimize CoinUndoSpend` | 2024-09-12 |

## Tier 5 — defer, revisit when there is transaction volume

Large, and their value scales with mempool activity we do not have.

- `txrequest` module + companions — 7 commits, 2024-07-03/05. Core's tx-request
  overhaul; real DoS hardening for tx relay. Currently relaying almost nothing.
- Orphan-processing interruptibility — 3 commits, 2024-07-03.
- `net: Refactor lifetime management of CNode, use smart_ptr` — 2025-01-17.
  Prevents a use-after-free class, but invasive.
- Parallel compact block downloads — 3 commits, 2024-12-05.

## Explicitly not porting

- **The consensus CHIPs.** See above.
- **UPnP / libnatpmp port mapping.** Our nodes have public IPs; this is attack
  surface with no benefit.
- **Fuzz harnesses** (bloom, txrequest, `PartiallyDownloadedBlock`, the
  `FuzzedDataProvider` helper). Worth having eventually, no runtime effect.
- **`nChainTx` widened to 64-bit.** Ours is still `unsigned int` (32-bit,
  `src/chain.h:147`). Overflow is 4.29 billion transactions away and the chain
  has roughly 12,600. Listed for completeness, not urgency.

## Method

Upstream was fetched into private refs without adding a remote:

```
git fetch --no-tags https://github.com/bitcoin-cash-node/bitcoin-cash-node.git \
  "+refs/tags/v27.0.0:refs/bchn/v27.0.0" \
  "+refs/tags/v29.0.0:refs/bchn/v29.0.0"

git log --no-merges refs/bchn/v27.0.0..refs/bchn/v29.0.0
```

Port onto a branch, build with `scripts/build-core-docker.sh`, and deploy with
`scripts/upgrade-node.sh` — which already backs up and can roll back.
