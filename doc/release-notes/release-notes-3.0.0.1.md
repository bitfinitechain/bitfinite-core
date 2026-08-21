> **Archived.** These are the notes for v3.0.0.1 (2026-06-26), kept as a historical
> record. They are NOT the current release. Current notes live in `doc/release-notes.md`,
> and every published build is listed at
> <https://github.com/bitfinitechain/bitfinite-core/releases>.

# BitFinite v3.0.0.1 — Mainnet Release

**Version**: v3.0.0.1
**Release Date**: 2026-06-26
**Base**: Bitcoin Cash Node v27

---

## Overview

Fresh BitFinite mainnet launch with a newly mined genesis block and ASERT
difficulty anchored at genesis. This release supersedes all earlier (v1.x/v2.x)
builds, which carried a difficulty-routing bug that prevented the chain from
advancing past block 1, plus incorrect monetary parameters.

---

## Network Parameters

| Parameter | Value |
|-----------|-------|
| **Genesis Hash** | `000000006f35956504ca93e1dc95d59a7989cdc2bc8094fd64ecabe43b238664` |
| **Genesis Merkle Root** | `8b091b56222f40fb242b3811b07cf9b75e48024501058e66c0c1c5e653bd8a1d` |
| **Genesis Timestamp** | 1782432000 (2026-06-26 00:00:00 UTC) |
| **Genesis Message** | `BFX 2026-06-26: BitFinite - sound money, freely mined` |
| **Genesis Difficulty** | `0x1d00ffff` (difficulty-1, easiest valid) |
| **Difficulty Algorithm** | ASERT, anchored at genesis (active from block 1) |
| **Block Time** | 5 minutes |
| **Block Reward** | 50 BFX |
| **Halving Interval** | 210,000 blocks (~2 years) |
| **Max Supply** | ~21,000,000 BFX |
| **Network Magic** | `BFin` (0x4246696e) |
| **P2P Port** | 19768 |
| **RPC Port** | 19769 |
| **CashAddr Prefix** | `bfx:` |

---

## Quick Start

### Server (daemon)
```bash
bitfinited -daemon
sleep 15
bitfinite-cli getblockchaininfo
```

### Desktop (GUI)
```bash
bitfinite-qt
```

### Mining
```bash
bitfinite-cli getnewaddress "mining"
bitfinite-cli generatetoaddress 10 "YOUR_ADDRESS"
```

Difficulty starts at the minimum and rises smoothly via ASERT as hashrate joins —
there is no separate bootstrap phase or transition block.

---

## Post-launch hardening (recommended)

After the chain has ~1,000+ stable blocks, bake a checkpoint plus
`nMinimumChainWork` / `defaultAssumeValid` into `src/chainparams.cpp` and publish
that as the official binary (see the TODO in `CMainParams`). Until then, low
hashrate means deep-reorg risk is elevated; operators should peer with trusted
nodes.
