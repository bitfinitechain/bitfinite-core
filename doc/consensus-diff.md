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

## Known state of the test suite — read before you clone

Disclosed up front because it is the first thing a reviewer hits and finding it
unannounced would rightly colour everything else.

**`test_bitcoin` does not currently build.** `src/test/logging_tests.cpp`
references `BCLog::ReconstructLogInstance`, which is not defined anywhere in the
tree, plus a `chrono`→`int64_t` mismatch and a missing `operator<<`. The file
arrived on 2026-08-11 with the `feat/upstream-security-tier2` backport
(`473cc615f4`, an upstream BCHN commit): the cherry-pick took the **tests** for
the log rate-limiter without all of its **implementation**. Node binaries are
unaffected — the breakage is test-only, which is exactly why it went unnoticed.

**No CI job builds the tests.** `.github/workflows/build-core.yaml` is the only
workflow and it builds release binaries. Nothing would have caught the above.

**With `logging_tests.cpp` excluded, `pow_tests` reports 19 failures.** These are
**not** caused by anything in the consensus changes described here — verified by
running the suite with and without the `pow.cpp` assertion restoration and
getting an identical count. They break down as:

- `retargeting_test` — exercises the pre-ASERT DAA using upstream's 600 s
  spacing and adjustment interval. BitFinite uses **300 s**. Tests that hardcode
  upstream constants fail on a chain that changed them; the fixtures were never
  updated for the fork.
- `cash_difficulty_test` — aborts on the upstream assertion
  `nHeight >= params.DifficultyAdjustmentInterval()`, same root cause.
- `asert_difficulty_test`, `calculate_asert_test`, `asert_activation_anchor_test`
  — all abort in `ECC_Start` on `secp256k1_context_sign == nullptr`. These are
  **cascading**: an earlier SIGABRT left ECC initialised, so every subsequent
  fixture fails to construct. They are not independent results and should not be
  counted as three more findings.

None of this is a consensus defect, but all of it is work that must happen before
the suite can serve as evidence of anything. Fixing the fixtures for BitFinite's
parameters, completing or reverting the partial backport, and adding a CI job
that builds and runs `test_bitcoin` are prerequisites for a review that can lean
on tests rather than on reading alone.

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
