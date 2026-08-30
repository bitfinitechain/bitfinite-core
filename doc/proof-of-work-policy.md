# Proof-of-work policy

**BitFinite stays on SHA-256d. This is settled, not pending.**

The question of whether to change the mining algorithm has been open since July
2026. This document closes it, states the reasoning, and says what would have to
change for us to revisit.

## The decision

We keep SHA-256d. We are not switching algorithms and we are not pursuing
merge-mining. Security against deep reorganisation comes from the finalisation
rule described below, not from out-hashing anyone.

## Why not change the algorithm

The honest starting point is that changing it does not solve the problem.

A chain is hard to attack when its hashrate is large relative to what an
attacker can rent or redirect. BitFinite is small. On SHA-256d we are small
next to Bitcoin's miners. On any other algorithm we would be small next to
that algorithm's miners, and rental markets exist there too. Switching moves
the problem; it does not remove it.

Against that, switching costs us three things:

- **The audience.** A Bitaxe is a SHA-256 miner. "Point a Bitaxe at this chain
  and find blocks" is our entire go-to-market, and it stops being true the day
  we change. We would trade a real, differentiated position for a marginal
  change in attack cost.
- **A hard fork**, with the coordination and risk any consensus change carries
  on a chain already diverged from its upstream.
- **The existing hashrate.** Every miner currently pointed at us is running
  SHA-256 hardware.

## Why not merge-mining (AuxPoW)

Assessed in 2026-08 and shelved. Merge-mining would let Bitcoin miners secure
BitFinite at no extra energy cost, which sounds like the obvious answer. It is
not, for three reasons:

- It is months of consensus-critical work on a fork that has already diverged
  from BCHN.
- ckpool, which runs our pools, has no merge-mining coordinator. That is
  additional off-chain infrastructure to build and operate.
- **Adoption is the real blocker, not the code.** Inheriting Bitcoin's security
  requires Bitcoin miners to actually merge-mine us. Few would. A merge-mined
  chain with thin adoption is exactly as attackable as it was before, having
  spent months getting there.

This is a "not now", not a "never". If a large pool ever wanted to merge-mine
us, the calculation changes.

## What actually protects the chain

**Finalisation.** Nodes refuse to reorganise more than 10 blocks deep:

```
src/validation.h:173   static constexpr int DEFAULT_MAX_REORG_DEPTH = 10;
```

Inherited from Bitcoin Cash Node, active since genesis, exposed as
`-maxreorgdepth`. This does not prevent a 51% attack. It removes the thing that
makes one profitable: past 10 confirmations, no amount of rented hashrate can
rewrite history, because honest nodes will not accept the rewrite whatever work
it carries.

That is why our exchange guidance is to credit deposits above 10 confirmations,
and why we recommend 20. Under that rule the deposit-and-rewrite attack is
structurally blocked rather than merely expensive.

**ASERT with a 6-hour half-life** (`consensus.nASERTHalfLife`). Difficulty
follows hashrate quickly, so a miner arriving and leaving does not stall the
chain for days.

**A genesis checkpoint** (`chainparams.cpp:206`). Genesis only — we do not ship
rolling checkpoints, because they move trust to whoever publishes them.

## What has changed since the question was opened

The July 2026 assessment recorded 1.1 TH/s and noted that a single Bitaxe was
roughly the entire network.

Measured 2026-08-30 at height 18,603: **550.7 TH/s**, difficulty 51,181,876.
That is about **500× the July figure**. A Bitaxe Gamma is now roughly 0.2% of
the network rather than all of it.

This does not make the chain secure by hashrate, and we are not going to claim
it does. At spot SHA-256 rental rates of roughly $0.0043 per TH per hour,
matching the network costs on the order of **$2.40 per hour**. That is real
money where it used to be pennies, and it is still far too little to rely on.
The finalisation rule remains the argument; hashrate growth is a bonus.

## What would reopen this

State it plainly so this is a decision and not dogma:

- A major pool offers to merge-mine BitFinite. AuxPoW becomes worth the work
  when adoption is not the blocker.
- An attack actually happens that finalisation does not contain — which would
  mean our reasoning above is wrong and should be revisited immediately.
- Hashrate becomes dominated by a single renter, such that the chain stalls
  repeatedly rather than being attacked.

Absent one of those, this is not an open question and should not be treated as
one in planning or in listing conversations.

---

*Decision recorded 2026-08-30. Supersedes the "PoW change pending" status
carried since 2026-07-09.*
