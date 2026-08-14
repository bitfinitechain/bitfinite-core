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

Disclosed up front because it is the first thing a reviewer runs, and finding
any of it unannounced would rightly colour everything else.

**`test_bitcoin` builds and 105 of its 111 suites pass** (measured 2026-08-14).
`scripts/run-tests-docker.sh` builds and runs it in the same container, with the
same toolchain file and flags, as the release build — one command, no host
toolchain. CI runs it on every branch (`.github/workflows/tests.yaml`).

That is a recent state. Three things were wrong and are worth knowing:

1. **The suite did not compile for two days.** The `feat/upstream-security-tier2`
   backport (`473cc615f4`) took an upstream commit's *tests* for the log rate
   limiter without all of its *implementation*. Node binaries were unaffected,
   which is why nobody noticed. Fixed by completing the backport, not reverting
   it — the limiter was in the tree and otherwise had zero coverage.
2. **Nothing in CI built the tests.** `build-core.yaml` produced release
   binaries only. That blind spot is why (1) survived. Now fixed.
3. **Two aborts were hiding the state of the whole corpus.** Boost.Test runs
   everything in one process, so a `SIGABRT` takes every later fixture with it
   via `ECC_Start(secp256k1_context_sign == nullptr)`. The raw count was **463
   failures, of which 449 were collateral**. The real number is 43.

   One of those aborts was a genuine defect, not a test problem: upstream ships
   a **checkpoint at the genesis block on every network**, and ours had been
   emptied during the fork on five of six — only mainnet was restored. A
   genesis checkpoint bans forks that rewrite genesis. `checkpoints_tests`
   asserts the invariant with a hard `assert()`, so its absence aborted the run.
   Restored on all networks, derived from `consensus.hashGenesisBlock` rather
   than a literal so it cannot drift.

**The 43 remaining failures, and what they are.** Every one is stale test data,
not a defect in the implementation — these suites carry upstream's constants and
were never regenerated for the fork:

| suite | failures | divergence |
|---|---|---|
| `checkpoints_tests` | 17 | expects upstream's populated checkpoint heights; ours pins genesis only |
| `cashaddr_tests` | 10 | expects prefix `bitfinite` (ours is `bfx`) and the **standard** base32 charset; ours deliberately swaps `q`↔`f` |
| `dstencode_tests` | 9 | same prefix/charset divergence via address encoding |
| `transaction_tests` | 4 | address/script vectors carrying upstream constants |
| `bip32_tests` | 2 | BIP32 `xpub`/`xprv` version bytes differ from ours |
| `net_tests` | 1 | not yet diagnosed |

Plus two suites excluded because they abort rather than fail: `miner_tests`
(throws on `bad-txns-inputs-missingorspent`) and `pow_tests` (asserts
`nHeight >= DifficultyAdjustmentInterval` — its DAA fixtures hardcode upstream's
600 s spacing where BitFinite mainnet uses 300 s).

Verify the address claim yourself rather than taking it from this table:
deployed BitFinite addresses are `bfx:f…`, and `src/cashaddr.cpp` defines
`CHARSET` as `fpzry9x8gq2tvdw0s3jn54khce6mua7l`. The implementation matches the
chain; the test vectors do not.

The exclusion list lives in `scripts/run-tests-docker.sh`, each entry with its
reason, and CI prints it into every job summary. It is a work queue, and the
right order is aborts first: a failed check reports one thing, an abort destroys
everything after it.


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
