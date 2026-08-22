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

## How to check this yourself

```bash
scripts/fork-drift.sh              # fork point, releases behind, triage
scripts/fork-drift.sh --json       # same, machine-readable
scripts/fork-drift.sh v28.0.0      # against a specific upstream release
```

The script adds and fetches the BCHN remote on first run. It classifies by area
and lists consensus-sensitive files separately, because that is the only part a
human needs to read.
