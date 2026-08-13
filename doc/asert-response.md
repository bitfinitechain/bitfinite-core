# ASERT response to hashrate loss

A known property of the chain, written down before someone asks rather than
after. Nothing here is a defect — it is the cost side of a difficulty algorithm
that is deliberately slow to overreact, quantified so that exchanges, miners and
we ourselves can plan around it.

## Parameters

| | mainnet |
|---|---|
| `nPowTargetSpacing` | 300 s (5 min) |
| `nASERTHalfLife` | 21600 s (6 h) |
| ASERT anchor | height 0, bits `0x1b00efab` (~difficulty 70,000), anchor time == genesis `nTime` |

ASERT is **anchor-absolute**: the target for a block is computed from the
schedule drift accumulated since the anchor, not from a rolling window.

    target_n = target_anchor × 2^(drift_n / halflife)
    drift_n  = (t_n − t_anchor) − spacing × (n − n_anchor + 1)

Being far above the anchor difficulty — the chain currently runs ~190× it —
does **not** affect responsiveness. Only the half-life does.

## The exposure

If a miner disconnects and takes a share of network hashrate with them, blocks
slow immediately and difficulty only relaxes as lag accumulates. Deterministic
simulation using the parameters above (`doc/scripts` reproduction below):

| hashrate remaining | worst block time | back within 5% of target | blocks | peak chain lag |
|---|---|---|---|---|
| 75 % | 6.7 min | 16.5 h | 173 | 2.1 h |
| **50 %** | **10.0 min** | **25.9 h** | 244 | 5.6 h |
| 25 % | 20.0 min | 35.4 h | 286 | 11.6 h |
| 10 % | 50.0 min | 44.8 h | 304 | 19.5 h |

Read the 50 % row as the realistic bad day: on a network whose hashrate is
concentrated in a small number of machines, one of them leaving doubles block
times for the best part of a day. A 6-confirmation deposit that normally clears
in 30 minutes takes an hour, and keeps taking an hour until difficulty catches up.

The simulation is deterministic — it uses expected block times to isolate the
systematic response. Real intervals are Poisson-distributed on top of this, so
individual blocks will be both faster and slower than the table shows.

Reproduce:

```python
SPACING, HALFLIFE = 300.0, 6 * 3600.0
def simulate(h, tol=0.05):
    drift = t = worst = 0.0; n = 0; recov = None
    while n < 100000:
        d = 2.0 ** (-drift / HALFLIFE)   # difficulty vs pre-step equilibrium
        bt = SPACING * d / h             # expected block time at hashrate h
        worst = max(worst, bt); drift += bt - SPACING; t += bt; n += 1
        if recov is None and abs(bt - SPACING) / SPACING <= tol and n > 1:
            recov = (t, n, drift)
        if recov and n > recov[1] + 5: break
    return worst, recov
```

## What the chain actually looks like today

Measured 2026-08-13, so this ages — re-measure before quoting it:

| | |
|---|---|
| difficulty | 13,286,465 (~190× the anchor) |
| network hashrate | ~207 TH/s |
| mean interval, last 199 blocks | 5.22 min |
| median interval, last 199 blocks | 4.12 min |
| lifetime average | 4.58 min — *below* the 5 min target |

The interval distribution matches an exponential with that mean to within
sampling noise (observed 59/40/31/44/25 across the <2m, 2–4m, 4–6m, 6–10m, 10m+
bands against 63/43/29/34/29 expected). That is what a healthy Poisson process
looks like, and the 5.22 min mean sits inside one standard error of the 5 min
target at n=199. **The exposure above is a scenario, not something currently
visible in the data.**

Beware reading dispersion as malfunction: a dashboard band labelled "on target"
that captures only the 4–6 min bucket will always look alarming, because an
exponential distribution puts most of its mass outside any narrow band around
the mean.

## Options, and their costs

- **Shorten the half-life.** Faster recovery, but a difficulty algorithm that
  reacts quickly to noise also oscillates on noise, and on a small chain that
  invites hashrate to game the swings. BCH chose 2 days; we already chose a much
  more aggressive 6 hours for a young network.
- **More independent miners.** The only fix that reduces the *size* of the step
  rather than the *recovery time*. This is the argument for pool outreach, and
  it is why the exposure shrinks on its own as the network grows.
- **Do nothing, and say so.** The behaviour is bounded, understood, and
  identical in kind to every young PoW chain. Publishing the numbers is worth
  more than tuning the parameter.

Related: `doc/upstream-security-backlog.md`, `src/pow.cpp`.
