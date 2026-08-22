# BitFinite (BFX) — Security Audit & Hardening Report

**Scope:** `bitfinite-core` consensus, proof-of-work, transaction/block validation,
network, P2P, RPC, seeding and configuration surface.
**Base:** fork of Bitcoin Cash Node (BCHN) v27 → Bitcoin Core lineage.
**Audit date:** 2026-06-27 · **Branch:** `master`

> **What this document is.** A transparent, evidence-backed review of the BitFinite
> node performed by the project's own engineering before mainnet relaunch. Every
> claim below is tied to a `file:line` reference you can verify yourself, and every
> genesis block was *independently recomputed* (double-SHA256 + PoW check), not taken
> on faith. This is an internal audit, not a paid third-party engagement — we publish
> it so the community can reproduce our findings and so independent reviewers have a
> concrete starting point. Responsible-disclosure contact: **bitfinitechain@proton.me**.

---

## Why this matters

Most "new" proof-of-work coins are forks of an *old* Bitcoin Core snapshot and
quietly carry years of un-patched bugs, or disable protections to get a chain
running. BitFinite took the opposite path:

1. **We built on a modern, hardened base** — BCHN v27, which carries the full
   lineage of Bitcoin/BCH security fixes — and we **verified those protections are
   actually present and switched on from block 1** (every fork-activation height is
   `0`, so nothing is "pending activation").
2. **We audited our own fork delta** and found — then fixed — real bugs we
   introduced while re-parameterising the chain (a difficulty crash, a subsidy
   regression, a money-supply-cap regression, genesis errors, a network-magic
   collision). These are documented below honestly.
3. **We disclose what is still open.** A short list of post-launch hardening items
   is published here rather than hidden.

---

## Methodology

Three independent review passes were run over the source tree:

- **Consensus / monetary fork-delta** — every parameter BFX changed vs upstream,
  with all five+ network genesis blocks re-derived from their actual
  `CreateGenesisBlock()` arguments and PoW-validated.
- **PoW / validation / historical-CVE mitigations** — confirming each well-known
  Bitcoin/BCH vulnerability mitigation is genuinely in the code and reachable.
- **Network / RPC / seeding / secrets** — magic bytes, ports, DNS/fixed seeds, RPC
  exposure defaults, and a full-tree sweep for committed secrets.

---

## Part 1 — Inherited hardening, verified present and active from block 1

These mitigations come from the BCHN/Bitcoin base. We did not invent them — our
contribution is **verifying** they are intact, reachable, and (because every BFX
fork-activation height is `0`) **enforced from the very first block**.

| Protection | Status | Evidence (`file:line`) |
|---|---|---|
| **CVE-2018-17144** — duplicate-input inflation | Present, active@0 | `consensus/tx_check.cpp:80-117`; enforced per-tx in `validation.cpp:3275-3282`; CVE cited at `validation.cpp:1739-1743` |
| **CVE-2012-2459** — Merkle duplicate-txid malleability | Present, active@0 | `consensus/merkle.cpp:46-69`; rejected "bad-txns-duplicate" in `validation.cpp:3236-3249` |
| **CVE-2010-5139** — output value overflow / `MoneyRange` | Present (multi-layer) | `consensus/tx_check.cpp:28-46`; `primitives/transaction.cpp:109-117`; `consensus/tx_verify.cpp:211-231` |
| **CVE-2013-2292** — sigops CPU-exhaustion | Present (modern SigChecks) | `consensus/consensus.h:37,64`; enforced `validation.cpp:1711-1735`; active from genesis (`phononHeight=0`) |
| **Replay protection** — `SIGHASH_FORKID` | Present, active@0 | `script/script_flags.h:84`; `script/interpreter.cpp:1886`; enabled `validation.cpp:1358-1361` (`uahfHeight=0`) |
| **Timewarp resistance** | Structurally closed | per-block absolute **ASERT** `pow.cpp:101-275` + MTP/future-time bounds `validation.cpp:3373-3380` |
| **Proof-of-work checks** | Present | `pow.cpp:412-432` (rejects negative/zero/overflow/`>powLimit`, checks `hash≤target`) |
| **BIP30** — duplicate-coinbase overwrite | Present | `validation.cpp:1560-1604` |
| **Coinbase maturity / over-payment** | Present | `COINBASE_MATURITY=100` `consensus.h:47`; over-pay rejected `validation.cpp:1757-1764` |

**No consensus check was found to have been removed or weakened during the fork.**

---

## Part 2 — BitFinite audit findings (bugs we caught and fixed)

These are defects **introduced by BitFinite** while re-parameterising the chain,
discovered by this audit process and **fixed in the current code**. They are the
substance of "we did the work," and we list them plainly.

| # | Finding | Severity (pre-fix) | Status | Evidence |
|---|---|---|---|---|
| 1 | **Difficulty crash at block 1** — ASERT was anchored at height 10001 while the DAA height was 0, so the node asserted/crashed on the first block. | Critical (chain could not advance) | **Fixed** | ASERT anchored at genesis `{0, 0x1d00ffff, 1782691200}` (mainnet); genesis-parent guard `pow.cpp:107-113` |
| 2 | **Block-subsidy regression** — coinbase subsidy had been reduced to `1 * COIN` instead of `50 * COIN`. | High (wrong issuance) | **Fixed** | `validation.cpp:885` → `50 * COIN`; halving interval `210000` `chainparams.cpp:69` |
| 3 | **Money-supply-cap regression** — `MAX_MONEY` was `100'000 * COIN` instead of the intended `21'000'000`. | High (supply-cap / `MoneyRange` wrong) | **Fixed** | `amount.h:131` → `21'000'000 * COIN` |
| 4 | **Genesis "time-too-new"** — first re-mine used a timestamp hours ahead of wall-clock, so peers rejected block 1. | High (launch blocker) | **Fixed** | genesis `nTime` moved back below wall-clock; now `1782691200` (see the verification table below) |
| 5 | **Non-mainnet genesis PoW mismatch** — changing the coinbase string altered the Merkle root for every network, but only mainnet's nonce had been re-mined. | Medium (test nets failed to start) | **Fixed** | all networks re-mined; every nonce independently PoW-verified |
| 6 | **`chipnet`/`testnet4` magic-byte collision** | Low (test-net cross-talk) | **Fixed** | distinct magic `chainparams.cpp:507-510` vs `341-344` |
| 7 | **Min-difficulty hardening on mainnet** — `fPowAllowMinDifficultyBlocks` left `true` from bootstrap would allow trivial blocks. | Medium (cheap chain takeover) | **Fixed** | mainnet `= false` `chainparams.cpp:105`; no leak path (`pow.cpp:121-125`) |
| 8 | **Committed dead-chain config** — a sample `bitfinite.conf` pointed `addnode` at the dead pre-relaunch seed and shipped placeholder RPC creds. | High (wrong-chain bootstrap + secret) | Removed from tree; **history scrub pending** (see Part 3) | replaced by clean `bitfinite.conf.example` |

All genesis blocks were re-derived and PoW-checked as part of fix verification:

| Network | nTime | nNonce | nBits | Recomputed hash == assert | PoW valid |
|---|---|---|---|---|---|
| main | 1782691200 | 3406937121 | 0x1d00ffff | ✅ | ✅ |
| testnet | 1787400000 | 575664822 | 0x1d00ffff | ✅ | ✅ |
| testnet4 | 1597811185 | 1633225309 | 0x1d00ffff | ✅ | ✅ |
| scalenet | 1598282438 | 3022631194 | 0x1d00ffff | ✅ | ✅ |
| chipnet | 1597811185 | 1633225309 | 0x1d00ffff | ✅ | ✅ |
| regtest | 1296688602 | 3 | 0x207fffff | ✅ | ✅ |

Re-verified 2026-08-22 by recomputing SHA256d over each header from the values in
this table and comparing against the `assert` in `chainparams.cpp`. All six pass.

Two rows changed in that pass:

- **main** previously read `1782432000 / 1870395023`. Those were the values from
  the first re-mine, before finding 4 above was fixed a second time; they do not
  hash to the asserted genesis and are not valid proof of work. The table said
  ✅ ✅ for both. Anyone auditing our genesis from this document would have been
  unable to reproduce it. `chainparams.cpp` was always correct — only this table
  was stale.
- **testnet** was re-mined on 2026-08-22 to move its timestamp off Bitcoin
  testnet3's February 2011 genesis, which the ASERT anchor inherited. See the
  comment on the genesis line in `chainparams.cpp` for why that mattered.

---

## Part 3 — Open items (disclosed honestly)

Nothing here is a critical or high consensus defect. These are the operational and
defence-in-depth items we are tracking; publishing them *is* the point.

| Severity | Item | Plan |
|---|---|---|
| **High (pre-publish)** | `bitfinite.conf` with placeholder creds + the dead seed IP still exists in **git history** (commit `ff8c19003`). Removing it from the working tree is not enough. | Scrub history (`git filter-repo`) before the repo is published; rotate any reused password. |
| **Medium** | Mainnet `nMinimumChainWork = 0x00` and empty `defaultAssumeValid` (`chainparams.cpp:110-117`). Correct for a chain with no history, but a fresh node has no minimum-work floor during IBD. | Bake `nMinimumChainWork` + `defaultAssumeValid` after ~1000+ blocks (procedure documented in-code). |
| **Medium** | Bootstrap depends on `seed-1/2/3.bitfinitechain.org` resolving to live BFX nodes (compiled fixed-seeds are intentionally empty). | Provision/verify seed DNS before launch; otherwise ship real fixed seeds. |
| **Medium** | `contrib/seeds/nodes_*.txt` still contain BCH/BTC peers (`:8333` etc). Not compiled in, but regenerating `chainparamsseeds.h` from them would ship wrong-chain peers. | Replace with real BFX nodes or empty the files. |
| **Low** | Test networks (testnet/testnet4/scalenet/chipnet) still use inherited BCH `nMinimumChainWork`/`defaultAssumeValid` constants that their freshly-mined genesis can never reach. | Zero them like mainnet until real BFX test history exists. |
| **Low** | Test networks reuse BCH testnet magic bytes and P2P ports, so they can handshake with BCH testnets before diverging on genesis. Mainnet is unaffected. | Optional: give BFX test nets distinct magic+ports. |
| **Info** | Benign commented-out sanity assert `pow.cpp:204` (`assert((powLimit >> 224) == 0)`); the configured `powLimit` satisfies it anyway. | Re-enable for defence-in-depth (zero cost). |
| **Info** | BFX reuses BCH's `SIGHASH_FORKID` value. No cross-chain replay risk (different genesis/UTXO set), but a distinct fork-id would add defence-in-depth. | Consider a unique fork-id in a future upgrade. |

### Known launch property (by design, not a bug)
A low-hashrate chain is inherently exposed to 51%-class reorgs until difficulty
rises. ASERT anchors the first block at minimum difficulty (`anchor target ==
powLimit`) and raises it as honest hashrate joins. The `nMinimumChainWork` /
checkpoint items above are the mitigations as the chain matures.

---

## Pre-launch security checklist

- [ ] **Scrub `bitfinite.conf` from git history** and rotate any reused password *(High)*
- [ ] Confirm `seed-1/2/3.bitfinitechain.org` resolve to live BFX nodes *(Medium)*
- [ ] Empty/replace `contrib/seeds/nodes_*.txt` *(Medium)*
- [ ] After ~1000 blocks: set `nMinimumChainWork` + `defaultAssumeValid`, bake a checkpoint *(Medium)*
- [ ] Zero test-net `nMinimumChainWork`/`defaultAssumeValid` *(Low)*
- [ ] Re-enable the `pow.cpp:204` assert *(Info)*
- [ ] Verify the `bitfinitechain` GitHub org + `bitfinitechain.org` domain are project-controlled *(Info)*

---

## Reporting a vulnerability

Please report security issues privately to **bitfinitechain@proton.me** before
public disclosure. We will acknowledge, investigate, and credit reporters who
follow responsible disclosure.

*This report reflects the state of the `master` branch on 2026-06-27 and will be
updated as the checklist above is completed.*
