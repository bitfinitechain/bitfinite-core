# What BitFinite changed in BCHN v27.0.0

Scoping document for an independent diff review. The point of this file is to
make the review **cheap to quote and hard to game**: every number below is
reproducible with a command you can run yourself against public repositories,
and the areas that carry consensus meaning are separated from the areas that do
not.

Fork base: **Bitcoin Cash Node v27.0.0**. Tag `v27.0.0` is present in this
repository, so `git diff v27.0.0..master` works directly.

## Headline

```bash
git diff --shortstat v27.0.0..master -- src/
#  310 files changed, 5939 insertions(+), 6824 deletions(-)
```

That number is misleading in our favour *and* against us, so do not use it as a
proxy for effort. Two corrections follow.

### 1. Over half the changed files are renaming only

```bash
git diff --name-only v27.0.0..master -- src/ | wc -l          # 310
# files whose changed lines only ever mention bitcoin/bitfinite/BCH/BFX: 167
```

By directory:

| area | files changed | consensus-bearing |
|---|---|---|
| `qt/` | 136 | no — desktop GUI |
| top level `src/` | 66 | partly |
| `test/` | 33 | no — but see below |
| `seeder/` | 12 | no — DNS seeder |
| `wallet/` | 11 | no |
| `univalue/`, `rpc/`, `util/`, `script/`, `interfaces/`, `primitives/` | 31 | no |
| `consensus/` | 4 | **yes** |

### 2. The largest single diff is almost entirely reformatting

`src/validation.cpp` reports 1,653 changed lines, which would dominate any
quote. It is not 1,653 lines of new logic:

```bash
# every function the diff appears to delete is still present
grep -c FlushStateToDisk src/validation.cpp        # 14
grep -c AcceptToMemoryPool src/validation.cpp      # 3

# normalise line wrapping, then compare token streams
diff <(git show v27.0.0:src/validation.cpp | tr -s ' \n\t' '\n' | grep -v '^$' | sort) \
     <(tr -s ' \n\t' '\n' < src/validation.cpp   | grep -v '^$' | sort) \
  | grep -cE '^[<>]'
#  243
```

243 differing tokens against ~1,653 changed lines. The file was re-wrapped at a
different column width; the functional delta is small. **Review the 243, not the
1,653.**

## Where the consensus meaning actually is

Five files. This is the review.

| file | what to check |
|---|---|
| `src/chainparams.cpp` | genesis block, ports, network magic, DNS seeds, ASERT anchor, checkpoints, `powLimit`, halving interval |
| `src/pow.cpp` | **11 lines, 2 hunks** — see below |
| `src/consensus/params.h` | parameter struct additions |
| `src/consensus/abla.h`, `activation.h`, `merkle.h` | inherited CHIP surfaces |
| `src/validation.cpp` | the 243 tokens above |

Issuance is unchanged. `GetBlockSubsidy` is untouched and the only diff around
the coinbase check is a variable extraction:

```diff
-    Amount blockReward =
-        nFees + GetBlockSubsidy(pindex->nHeight, consensusParams);
+    Amount blockReward = nFees + GetBlockSubsidy(pindex->nHeight, consensusParams);
```

There is no height-conditional issuance, no premine allocation in genesis, and
no special-case coinbase rule. That is the claim the review exists to confirm
independently — verify it, don't take it from this file.

### `src/pow.cpp` in full

Two hunks, both worth understanding.

**(a) Genesis special case.** ASERT cannot compute a target with no history, and
our anchor *is* genesis, so block 1 returns `powLimit`:

```cpp
if (pindexPrev->pprev == nullptr) {
    return UintToArith256(params.powLimit).GetCompact();
}
```

**(b) A disabled overflow assertion — found and restored during this audit.**
Upstream ships `assert((powLimit >> 224) == 0);` to guarantee headroom in
`CalculateASERT`. It had been commented out during the genesis/anchor work. The
disable was never necessary:

- mainnet/testnet/scalenet `powLimit` is `00000000ffff…` — exactly 32 leading
  zero bits, so the condition holds
- the only value that would trip it is regtest's `7fffffff…`, which is
  **inherited unchanged from upstream** and never reaches this function because
  `fPowNoRetargeting` short-circuits `GetNextWorkRequired` first
- every ASERT unit test uses `CBaseChainParams::MAIN`

Restored. Flagged here rather than quietly fixed, because "we disabled an
assertion in consensus arithmetic and put it back" is exactly what a reviewer
should be told before they find it.

## Difficulty behaviour

ASERT anchored at height 0 with a 6-hour half-life. The response to a hashrate
step is quantified in `doc/asert-response.md`, including the case an exchange
will ask about (half the network leaving ⇒ ~10 min blocks for ~26 h).

## State of the test suite — read before you clone

Disclosed up front because it is the first thing a reviewer runs.

**`test_bitcoin` builds and 117 of its 120 suites pass** (measured 2026-08-14).
One command, same container and toolchain as the release build:

```bash
git clone https://github.com/bitfinitechain/bitfinite-core
cd bitfinite-core && scripts/run-tests-docker.sh
```

CI runs it on every branch (`.github/workflows/tests.yaml`). The three excluded
suites are listed with a reason each in `scripts/run-tests-docker.sh`, and CI
prints that list into every job summary.

### What the fork process left behind

The suite did not run for months — the release pipeline built binaries and
nothing built the tests. When it was made to run, it surfaced a specific and
instructive set of problems. They are recorded here, classified honestly,
because "what did the rebrand break" is a question a reviewer should not have to
ask twice.

**Real defects in shipped code — found and fixed:**

| defect | consequence |
|---|---|
| Genesis checkpoint missing on **5 of 6 networks** | Upstream pins a checkpoint at the genesis block on every network; ours were emptied during the fork and only mainnet was restored. A genesis checkpoint bans forks that rewrite genesis. |
| ASERT overflow assertion commented out | `assert((powLimit >> 224) == 0)` guards the headroom used by `CalculateASERT`. The condition held on every live network, so nothing was exploitable — but a disabled guard on consensus arithmetic is worth having back. |
| Log buffering after rotation | Reopened debug log left fully buffered, losing recent lines on a crash. **Inherited from upstream (2018) and already fixed by BCHN in v29.0.0** — we found it independently and backported theirs. Not our discovery. |

**Rebranding damage — test-only, no effect on the node:**

Four suites failed because a find-replace landed inside a value that carries a
checksum or an encoding. Every one produced output that still *parsed*, which is
why nothing looked wrong:

| suite | what the replace did |
|---|---|
| `cashaddr_tests` | replaced the prefix *inside the value the checksum covers* — `bitcoincash:` → `bitfinite:`, which is not even our prefix (`bfx`) |
| `dstencode_tests` | same |
| `bip32_tests` | altered **one character** inside a base58 `xpub` string |
| `net_tests` | invented a client name that has never existed: the fixture expected `/BitFinite Node:…/` while `CLIENT_NAME` is `BitFinite`, so it asserted a user agent no node has ever sent |

**Not a defect — a design consequence:**

`transaction_tests` assumed BCH's upgrade timeline. magneticAnomaly introduced a
100-byte minimum transaction size and upgrade9 later relaxed it to 64; on BCH
those are four and a half years apart. BitFinite launched with every inherited
upgrade already active at height 0, so that window never existed here. The test
is now agnostic to chain shape rather than deleted.

**The rule worth taking from this:** a replace that lands inside a checksummed,
hashed or encoded value is not a rename — it is corruption that still parses.
None of it was detectable by reading; all of it failed the moment a test
computed the expected value independently.

### Still excluded, with reasons

- **`checkpoints_tests`** — the fixtures build blocks on **Bitcoin's** genesis
  hash (`000000000019d6689c…`) and use Bitcoin's historical checkpoint heights.
  Needs its fixtures derived from chainparams. Our own checkpoint data is
  correct.
- **`miner_tests`** — its `blockinfo[]` nonce table was mined against upstream's
  chain. Regenerating it is infeasible here: our ASERT anchor puts mainnet at
  ~70,000 difficulty from block 2, i.e. 3.0e14 hashes per block against
  difficulty-1's 4.3e9. The fix is porting the suite to regtest.
- **`pow_tests`** — exercises `GetNextCashWorkRequired`, which is dead code on
  this chain (`IsAxionEnabled` is `nHeight >= 0` with the anchor at genesis), and
  its ASERT vectors hardcode BCH's 2-day half-life against our 6 hours.

## Still missing upstream

`doc/upstream-security-backlog.md` tracks BCHN fixes between v27.0.0 and v29.0.0
that are not yet backported. Read it alongside this file — a diff review that
only looks at what we *added* misses what we have not yet *taken*.

## Reproducing every number here

```bash
git clone https://github.com/bitfinitechain/bitfinite-core && cd bitfinite-core
git diff --shortstat v27.0.0..master -- src/
git diff --stat     v27.0.0..master -- src/consensus src/pow.cpp src/chainparams.cpp
git diff            v27.0.0..master -- src/pow.cpp
```
