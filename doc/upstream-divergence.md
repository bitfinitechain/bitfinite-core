# Upstream divergence

What BitFinite Core inherited from Bitcoin Cash Node, what it deliberately did
not, and how to verify both. Every claim here is reproducible with the command
beside it; nothing is asserted from memory.

Generated and checked 2026-08-22 against BCHN v29.1.0.
Run `scripts/fork-drift.sh` to reproduce the figures.

## Where the fork is

BitFinite Core forked **BCHN v27.0.0** (released 2023-12-12).

Git ancestry will not show you this and will actively mislead you: our history
was re-rooted at fork time, so `git merge-base --is-ancestor v27.0.0 HEAD` says
*not an ancestor* and the merge base is a 2016 secp256k1 squash commit. Asking
git for "commits upstream has that we don't" returns 21,260, including the fix
for CVE-2013-5700 — which is present in our `src/bloom.h`. That number measures
a history rewrite, not code.

The fork point is identified by **tree hash** instead:

```bash
git rev-parse v27.0.0^{tree}                        # 9b2252d402bd…
git log --format='%H %T' HEAD | grep 9b2252d402bd   # e322554a7a14…
```

Our commit `e322554a` has a tree byte-identical to BCHN v27.0.0. That is the
fork point, provable by content regardless of what the commit graph claims.

## What we inherited and run

Everything BCHN shipped up to and including **Upgrade 10** is present, wired and
exercised:

| | |
|---|---|
| Upgrade 8, 9 | present |
| **Upgrade 10 — ABLA** (adaptive blocksize) | `src/consensus/abla.cpp`, `abla.h`, gated in `validation.cpp`, with upstream's full test-vector corpus in `src/test/data/abla_test_vectors/` |
| CashTokens | inherited |
| DAA / ASERT | inherited, retuned — see below |

```bash
ls src/consensus/abla.*                      # implementation
grep -n IsUpgrade10Enabled src/validation.cpp # call sites
```

## What we deliberately did not take

BCHN has shipped five releases since our fork: v27.1.0, v28.0.0, v28.0.1,
v29.0.0, v29.1.0. The consensus content of those is **BCH network upgrades**,
not maintenance:

| upgrade | contents | our position |
|---|---|---|
| **Upgrade 11** (May 2025) | VM Limits CHIP + BigInt CHIP — arbitrary-precision script arithmetic, new VM cost accounting | not adopted |
| **Upgrade 12** (May 2026) | node expiry bump and associated changes | not adopted |
| **Upgrade 2027** | scheduled, not yet defined | not adopted |

Adopting any of these is a **hard fork of BitFinite**, not a patch. They change
what scripts are valid. That is a governance decision about our own chain, not
an upstream maintenance task we have fallen behind on, and the two should not be
confused because a file-count diff cannot tell them apart.

This is why raw drift numbers are misleading. `scripts/fork-drift.sh` reports 914
upstream-changed source files, 783 of them in files we have never touched — but
the single largest is `src/script/interpreter.cpp` at 3,528 lines, which is the
Upgrade 11 CHIP work. "Backport the clean 783" would fork the chain.

## Known divergence that was a defect — fixed 2026-08-22

`IsUpgrade11Enabled` used to be a live gate with nothing behind it: declared,
implemented, and returning `true` from 15 May 2025 onward because the activation
time was inherited from BCH, while no rule in the node ever consulted it. It has
been **removed** (commit `b5fb09df`). Consensus behaviour is unchanged — nothing
branched on it — but the source no longer claims an upgrade this chain does not
implement.

Chasing it turned up something worse. `upgrade11ActivationTime` was not purely
dead: `init.cpp` feeds it to the "software outdated" mechanism, which warns and
then **disables RPC** once the date passes. Every network carried a BCH date
already in the past, so `-expire=1` would have expired a BitFinite node the
moment it started, on another chain's schedule. Latent only because an earlier
fix defaults `-expire` to false at the call site. It is now `0` on all six
networks, which `software_outdated` treats as "never".

Two help strings were also wrong and are corrected: `-expire` printed
`DEFAULT_EXPIRE` (still upstream's `true`) while the call site passes `false`,
and `-upgrade11activationtime` described itself as a BitFinite network upgrade
rather than what it is, a node-expiry date. The argument keeps its inherited
name so upstream's functional tests still run.

```bash
grep -rn IsUpgrade11Enabled src/          # only the removal note remains
grep -rn upgrade11ActivationTime src/chainparams.cpp   # 0 on every network
```

We still retain `IsUpgrade8Enabled`, which upstream deleted as long-activated
cleanup. Harmless, and a fair example of drift that is neither a fix nor a
decision — just tidying we have not done.

## Where we intentionally differ from BCHN beyond upgrades

- **ASERT half-life is 6 hours**, not BCH's 2 days, matching a 5-minute target
  block interval rather than 10.
- **Genesis and chainparams are our own**, which is why upstream's
  `miner_tests` block table cannot be reused — its nonces were mined against
  BCH's genesis. See the exclusion notes in `scripts/run-tests-docker.sh`.
- **cashaddr uses a swapped alphabet** (`q`↔`f`), so addresses are `bfx:f…`.

## Security triage of the untouched node code — 2026-08-22

Scope: the 101 source files that upstream changed between v27.0.0 and v29.1.0 and
that BitFinite has never modified. Excludes tests, Qt, bench, the 36
consensus-sensitive files, and the 71 node-code files we have modified.

**Result: no security backlog. Nothing urgent, nothing to backport for safety.**

### The method matters more than the number

The naive query returns 1,629 upstream commits touching these files, and keyword
matching flags 51 as security-relevant. Both figures are wrong, in the same way
the commit-ancestry drift count was wrong.

Filtering by COMMITTER date — when a change actually landed in BCHN — leaves
**102** commits after our 2023-12-12 fork. The other ~1,500 predate it and are
already in our tree. Verified by checking the code, not by trusting the range:

| flagged as missing | actually |
|---|---|
| `[CVE-2019-18936]` UniValue JSON depth | **present** — `MAX_JSON_DEPTH = 512`, guard at `univalue_read.cpp:315` |
| `[mining] Fix potential crash in submitblock` | **present** — the `enable_shared_from_this` / `ReqEntry` / `Create()` form |
| `Fix memleak in TorController` | present |
| `Fix memory leak in multiUserAuthorized` | present |
| `fix uninitialized read stringifying addrLocal` | present |

Use committer date, and verify each candidate against the code. A file appearing
in a drift list says nothing about whether its fix is missing.

### What the 102 post-fork commits actually are

Overwhelmingly toolchain and build compatibility: GCC 14, clang-8, Boost 1.85,
Arch Linux, older GCC 8.3.0, Darwin gitian, compiler warnings. Valuable when we
move build environments; irrelevant to a running node.

Two are real code changes and both are absent from our tree:

**1. `blockstorage`: ftell overflow asserts** (upstream 2025-08-01) — adds
`assert(zSize == nSize && "Overflow check failed")` around `ftell` results.
**Structurally unreachable here.** The overflow needs a block file above 4 GB and
`MAX_BLOCKFILE_SIZE` is `0x8000000` — 128 MB. Defensive hardening upstream, not a
live bug. Worth taking with the next batch; not worth taking alone.

**2. `validationinterface`: registering after threads start** (upstream
2026-03-20) — reworks `RegisterValidationInterface()` and adds
`IsStoppedOrAboutToStop()` to the scheduler. Affects a startup-ordering race.
Our node registers interfaces before starting the scheduler, so it is not
currently reachable either, but this one is a genuine correctness fix and the
better of the two to take.

### Recommendation

No emergency. Fold both into a batched maintenance backport whenever we next take
upstream non-consensus work, alongside the 630 upstream test files — which remain
the highest-value part of the 878 non-consensus changes, since they are
regression coverage we do not have.

### Second pass — the 71 files we have also modified

The harder half: files where our changes and upstream's overlap, so each needs
reading rather than pattern-matching.

**Result: also no missing security fixes.**

164 upstream commits touch these files; filtering by committer date leaves those
that landed after our fork. Stripping build/toolchain noise leaves ~14 with
security or correctness language. Every one checked against our code:

| upstream fix | in our tree |
|---|---|
| `net: UB in CNode::ReceiveMsgBytes` | present (`net.cpp:629`, the `find()` form) |
| `Fix socket leaks -> fd exhaustion` | present (`~CSeederNode` closes the socket) |
| `Rare crash when out of file descriptors` | present |
| `seeder: UB/race starting DNS threads` | present |
| `seeder: UB passing pointers through void *` | present |
| `seeder: graceful exit, persist db` | present |
| `Fix seeder checkpoint check` | present |
| `UB in AddToCompactExtraTransactions` | present |
| `net: msghand CPU / early-wake logic` | present |
| `CBloomFilter is{Empty,Full}` (CVE-2013-5700) | present |
| `Logging: unbuffered fileout on reopen` | present — and independently found here first |

One genuine absence, and it is not reachable:

**`mempool: Fix potential for bug in removeForBlock()`** (upstream 2024-12-24)
splits `addNoLimit` from `addForBlock` and clamps a per-instance
`maxDynamicUsage` so `while (DynamicMemoryUsage() > maxUsage)` cannot spin
forever. That loop can only fail to terminate if the cap is below the pool's own
baseline, which requires the configurable constructor upstream added. Ours has no
such constructor — `maxDynamicUsage()` is `static`, returning
`max(20 * DEFAULT_CONSENSUS_BLOCK_SIZE, GetMaxMemPoolSize())`, a floor of 640 MB.
Nothing can drive it low enough to hang. Take it with the next batch as a
refactor; it is not a live defect.

### Method notes

Two checks produced wrong answers and both were caught by reading the code:

- Marker-grep reported the logging fix ABSENT. It is present; our copy carries a
  long explanatory comment that displaced the line the marker matched. **Every
  "absent" must be confirmed by reading the file.**
- Symbol-presence checks (does `RegisterLoad` exist?) prove nothing about whether
  a fix is applied. Compare the fixed *form*, not the identifier.

### Combined scope

Both passes together cover all 172 non-consensus node-code files upstream changed
between v27.0.0 and v29.1.0 — 101 untouched, 71 modified. **No security backlog in
either.** Not yet triaged: the 36 consensus files (deliberate divergence, see
above) and the 630 upstream test files.

### Third pass — the 36 consensus-sensitive files

The question this set answers: which upstream work *forces* a hard fork, and which
merely lives in consensus files without changing validity. 41 upstream commits
landed here after our fork. They split cleanly.

**Hard-fork territory — cannot be taken without a chain decision (8):**

| | |
|---|---|
| `2026 CHIPS: Bitops, Loops, Functions, and P2S` | a whole further upgrade beyond Upgrade 11 |
| `Remove script flags SCRIPT_64_BIT_INTEGERS / SCRIPT_NATIVE_INTROSPECTION` | changes script semantics |
| `Post 2026 upgrade: checkpoints, height-based activation` | activation scheme |
| `Upgrade 11: Switch to height-based activation` | activation scheme |
| `Script VM: native int64_t with BigInt fallback` | depends on BigInt |
| `C++20 TODOs for script/bigint.cpp` | depends on BigInt |
| `LibAuth test vectors (on top of BigInt + VMLimits)` | depends on the CHIPs |
| `Bump node expiry to May 2027` | scheduling, and we removed our expiry |

Note the first row: BCH has a **2026 CHIP set** — Bitops, Loops, Functions, P2S —
beyond the Upgrade 11 work already declined. The gap widens by design, not neglect.

**The other 33 touch consensus files but do not change validity:** allocation and
copying optimisations in `VerifyScript` and the checksig paths, the `StackItem`
abstraction, `prevector`/`std::span` and `boost::variant`→`std::variant`
migrations, `coinstatsindex`, INV relay tuning, GCC warning fixes, comment typos.
Safe in principle, large in practice, and worth nothing to us on their own.

**Three were worth extracting. One is now fixed.**

`Trivial: Prevent UB in class CNoDestination` (upstream 2025-02-28) — **applied**.
`operator<` returned `true` for two equivalent values, so both `a < b` and
`b < a` held. That is not a strict weak ordering, and it is undefined behaviour in
every ordered container `CTxDestination` reaches — the wallet puts it in
`std::set<CTxDestination>` and `std::map<CTxDestination, …>` in
`GetAddressGroupings`, `GetAddressBalances`, `ListCoins` and the address book. A
wallet holding a non-standard output yields `CNoDestination`, and two of them in
one container is enough; libstdc++ `std::sort` can read past the end of the range
on a broken comparator. One line, no consensus effect — `CNoDestination` is an
address classification used by wallet and RPC, never by script validation.

Two left deliberately, both non-urgent:

- `CBlockIndex::nChainTx` is still `unsigned int` here; upstream widened it to
  `uint64_t`. At 16.4k blocks the 32-bit ceiling is decades away. Cheap, take it
  with the next batch.
- `-check-abla` (upstream 2024-12-23) makes the slow ABLA startup checks optional
  and off by default. We run ABLA, so this is a real startup-time win on HDD, but
  it is performance, not correctness.

## How to check this yourself

```bash
scripts/fork-drift.sh              # fork point, releases behind, triage
scripts/fork-drift.sh --json       # same, machine-readable
scripts/fork-drift.sh v28.0.0      # against a specific upstream release
```

The script adds and fetches the BCHN remote on first run. It classifies by area
and lists consensus-sensitive files separately, because that is the only part a
human needs to read.
