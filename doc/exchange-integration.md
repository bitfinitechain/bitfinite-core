# Exchange integration

For exchanges, custodians and payment processors listing BitFinite (BFX).

Two things matter more than the rest of this document, so they come first.

## 1. Credit deposits at more than 10 confirmations

**Use 20 confirmations (about 100 minutes).** The minimum safe value is 11.

BitFinite nodes refuse to reorganise more than 10 blocks deep. This is not a
policy suggestion or a monitoring rule; it is enforced in consensus code:

```
src/validation.h:173   static constexpr int DEFAULT_MAX_REORG_DEPTH = 10;
```

The value is exposed as `-maxreorgdepth` and is active by default. A node that
sees a competing chain requiring a deeper rewrite rejects it, whatever work it
carries.

**Why this is the important number for you.** BitFinite is a small SHA-256d
chain. Its hashrate is a rounding error next to Bitcoin's, and majority hashrate
can be rented on the open market for a few dollars an hour. The standard attack
on a chain this size is to rent hashrate, deposit coins to an exchange,
withdraw something else, then rewrite the chain to erase the deposit.

The reorg cap structurally blocks that attack rather than making it expensive.
Past 10 confirmations, no quantity of rented hashrate buys a rewrite, because
honest nodes will not accept one. Credit deposits above that depth and the
double-spend path is closed. Credit them below it and no confirmation count
saves you, because the chain is cheap to out-hash.

We would rather state this plainly than have your risk desk discover the
hashrate number on its own and assume we were hoping they would not look.

## 2. Do not use a stock cashaddr library

**BitFinite uses a modified base32 alphabet. `q` and `f` are swapped.**

```
BitFinite   fpzry9x8gq2tvdw0s3jn54khce6mua7l     src/cashaddr.cpp:16
standard    qpzry9x8gf2tvdw0s3jn54khce6mua7l
```

A Bitcoin Cash or generic cashaddr implementation will encode and decode our
addresses **without erroring** and produce the wrong result. There is no
checksum failure to warn you, because the checksum is computed over the swapped
alphabet too.

Practical consequences:

- Mainnet addresses look like `bfx:f…`, not `bfx:q…`. An address beginning
  `bfx:q` is a sign that something encoded it with the wrong alphabet.
- Validate addresses with our library, or with your own implementation using the
  alphabet above. Do not reuse a BCH validator.
- The prefix is `bfx` on mainnet and `bfxtest` on testnet, and it is part of the
  checksum. `bfx:…` and a bare `…` are the same address; a BCH-prefixed string
  is not.

Reference implementations: `src/cashaddr.cpp` in this repository, and
`bitfinite-wallet` for a Dart version.

## Chain parameters

| | mainnet | testnet |
|---|---|---|
| Proof of work | SHA-256d | SHA-256d |
| Block target | 5 minutes (`nPowTargetSpacing = 300`) | 10 minutes |
| Difficulty algorithm | ASERT, half-life **6 hours** | ASERT, half-life 1 hour |
| Block subsidy | 50 BFX | 50 BFX |
| Halving interval | 210,000 blocks | 4,200 blocks |
| Maximum supply | 21,000,000 BFX | — |
| Decimals | 8 (1 BFX = 100,000,000 satoshis) | 8 |
| Address prefix | `bfx` | `bfxtest` |
| P2P port | 19768 | 29768 |
| RPC port | 19769 | 29769 |
| Max reorg depth | 10 | 10 |

The short ASERT half-life is deliberate. Hashrate on a chain this size arrives
and leaves in large relative steps, and a 6-hour half-life lets difficulty
follow it instead of stalling the chain for hours after a miner departs.

## Running a node

BitFinite Node is a fork of Bitcoin Cash Node v27.0.0. If you have integrated
BCHN, or Bitcoin Core before it, the RPC surface will be familiar.

```
bitfinited -daemon -txindex=1
bitfinite-cli getblockchaininfo
```

Releases, with checksums, are at
<https://github.com/bitfinitechain/bitfinite-core/releases>.

Notes that matter for an exchange deployment:

- **`-txindex=1`** if you look up arbitrary transactions rather than only your
  own wallet's.
- **Do not lower `-maxreorgdepth`.** It is the guarantee section 1 relies on.
- DNS seeding uses `seed.bitfinitechain.org`; fixed seeds ship in the binary, so
  a node still finds peers if DNS is unavailable.
- The chain inherits **CashTokens** and **ABLA** from BCHN. If you do not intend
  to support tokens, treat token-bearing outputs as you would any output you do
  not recognise, and do not sweep them.

## Wallet and tooling support

| | |
|---|---|
| Node RPC | this repository |
| Electrum protocol | `electr.bitfinitechain.org:443` (electrs, TLS) |
| Block explorer | <https://explorer.bitfinitechain.org> |
| Supply API | `https://explorer.bitfinitechain.org/api/supply` |
| Mobile wallet | `bitfinite-wallet` (Android) |

The supply endpoints return plain numbers and are intended for listing sites:

```
/api/supply/circulating    /api/supply/total    /api/supply/max
```

## What we will tell you if you ask

- The network is small and its security budget is small. We do not market BFX as
  a store of value, and we would rather you set your confirmation policy from
  section 1 than from a hashrate chart that flatters us.
- Developer-held balances are disclosed on request, with the addresses, so you
  can verify the figure against the chain rather than take ours. As of
  2026-08-27 that was about 23% of circulating supply and under 1% of the
  21,000,000 cap, and a quarter of it has already been distributed. We would
  rather give you a number you can check than a slogan you cannot.
- The reorg cap is inherited from BCHN, not invented here, and has been active
  since genesis.

## Contact

`bitfinitechain@gmail.com` — happy to answer risk questions in writing, and to
put you in touch with a node you can point a test integration at.
