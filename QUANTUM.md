# BitFinite (BFX) — Quantum Computing Posture

**Subject:** BitFinite Core (fork of Bitcoin Cash Node v27, Bitcoin lineage)
**Assessment date:** 2026-06-27 · **Last updated:** 2026-11-25 · **Branch:** `master`

> **We will not market BitFinite as "quantum-proof."** It isn't — and neither is
> Bitcoin, Bitcoin Cash, Litecoin, or any other secp256k1 proof-of-work chain that
> claims otherwise. What we *can* offer is something rarer in this space: a clear,
> code-grounded, honest account of exactly where BFX stands against quantum
> computing, and a concrete plan. Every claim below is backed by a `file:line`
> reference you can check. Trust is earned with candor, not buzzwords.

---

## TL;DR

| Question | Honest answer |
|---|---|
| Is BFX quantum-resistant today? | **No.** It uses ECDSA/Schnorr over secp256k1, like all Bitcoin-lineage chains. |
| Will a quantum computer break BFX *mining*? | **No** — this is a common myth. Grover gives only a quadratic speedup on SHA-256; ASICs dominate. PoW is safe. |
| What *is* the quantum risk? | **Signatures.** Shor's algorithm recovers a private key from an exposed *public* key. |
| What protects my coins right now? | For **unspent, never-reused** addresses: the `hash160` commitment (your public key isn't on-chain yet). |
| Does BFX have post-quantum crypto? | **None yet** — and we say so plainly rather than imply otherwise. |
| Is there a path? | **Yes, through upstream Bitcoin Cash**, which is building one. BFX can follow it only by a planned hard fork. See [Upstream progress](#upstream-progress-bitcoin-cash-2025-2026). |
| What's BFX's advantage? | A **brand-new chain** (genesis 2026-06-26) with *no* legacy exposed-key overhang, and a migration plan. |

---

## The threat model, stated correctly

A *cryptographically-relevant quantum computer* (CRQC) — which does **not** exist today —
would affect the two halves of the cryptographic stack very differently:

### Shor's algorithm → **breaks** elliptic-curve signatures
Shor solves the elliptic-curve discrete-log problem in polynomial time. Given a
**public key**, it recovers the **private key**. This breaks every signature scheme
BFX uses, all of which live on the secp256k1 curve:

- ECDSA — `src/key.cpp:253` (sign), `src/pubkey.cpp:195` (verify)
- Schnorr — `src/key.cpp:280`, `src/pubkey.cpp:215`
- Recoverable ECDSA / message signing — `src/key.cpp:309`
- `OP_CHECKDATASIG` — `src/script/interpreter.cpp:986`
- BIP32/39/44 HD keys — still secp256k1 points (`src/key.cpp:337`, `src/hash.cpp:75`)

Curve size does not help: secp256k1's ~128-bit *classical* security is irrelevant
against Shor.

### Grover's algorithm → only **weakens** hashes (quadratic, not catastrophic)

| Hash | Used for | Post-Grover strength | Verdict |
|---|---|---|---|
| SHA-256 (double) | PoW, txid, Merkle, headers (`src/hash.h:23`, `src/pow.cpp`) | ~2¹²⁸ pre-image | **Safe** |
| SHA-256 | collision surface | ~2⁸⁵ | Monitor (not a theft vector) |
| RIPEMD-160 / `hash160` | address commitment (`src/hash.h:49`, `src/pubkey.h:136`) | ~2⁸⁰ pre-image | **Thinnest margin** |
| HMAC-SHA512 | BIP32 derivation (`src/crypto/hmac_sha512.cpp`) | ~2²⁵⁶ | Safe |

Grover's quadratic speedup does **not** parallelize efficiently, so real hardware is
far worse than these ideal bounds. **Proof-of-work and transaction IDs remain safe.**

> **Myth, corrected:** "Quantum computers will break PoW mining." False. The quantum
> threat to BFX is to **spending authority (private keys)**, not to block production.

---

## When is your public key exposed? (the Shor exposure window)

Your coins are only at risk once the corresponding **public key** is on-chain. The
script type decides when that happens (`src/script/standard.cpp`):

1. **Unspent P2PKH / P2SH** — the output commits only to `hash160(pubkey)`. Your raw
   public key is **not** on the chain. Quantum-safe until you spend. *(This is most coins.)*
2. **A spend in flight (mempool)** — the moment you broadcast a spend, your pubkey is
   public but the tx isn't confirmed yet. A fast-enough CRQC could in principle derive
   the key and race a competing transaction. This window exists for **every** spend.
3. **Reused / already-spent-from addresses** — once any output is spent, that address's
   pubkey is on-chain **forever**. Anything left at or sent to it is fully exposed.
4. **P2PK outputs** — pubkey is in the output script from creation. Worst case.

**Takeaway for users:** never reuse an address. One address per receive, and never send
funds *back* to an address you've already spent from. This keeps your exposure confined
to the brief per-spend window instead of a permanent standing risk.

---

## Where BFX actually stands — and its genuine advantage

- **No post-quantum cryptography is present in the codebase.** A source-tree search for
  `dilithium`, `kyber`, `sphincs`, `falcon`, `lamport`, `post-quantum` returns nothing.
  We state this plainly.
- **But BFX has no legacy quantum debt.** Mature chains carry a large pool of
  *already-exposed* public keys — Satoshi-era P2PK coinbases, a decade of address reuse
  (commonly estimated at 25%+ of supply on Bitcoin). BFX's mainnet genesis is
  **2026-06-26** (`src/chainparams.cpp:54,157`); that exposed-key overhang is **zero**.
  Starting clean, the practical quantum-vulnerable fraction stays near zero as long as
  the ecosystem practices address non-reuse.

This is the honest version of a "quantum advantage": not resistance, but a clean slate
plus good habits — and time to migrate before a CRQC is real.

---

## Upstream progress: Bitcoin Cash, 2025-2026

BFX forks Bitcoin Cash Node, so Bitcoin Cash research is our most direct path. Two
efforts matter. Both are Bitcoin Cash work, not ours, and neither runs on BFX today.

**Quantumroot: post-quantum vaults.** A vault design by Jason Dreyzehner. It uses
LM-OTS one-time signatures (RFC 8554), which rely only on SHA-256, so no new
cryptography is involved. Its author describes it as quantum safe "at rest", says it
became possible with BCH's May 2025 upgrade, and says the May 2026 upgrade makes it
10 to 100 times smaller. It was announced on BCH's preview network (chipnet) in
November 2025. We have not found a primary source confirming use on BCH mainnet, and
the author called holding funds in one "probably unwise without an audited wallet
implementation". Sources: [announcement](https://bitcoincashresearch.org/t/quantumroot-quantum-secure-vaults-for-bitcoin-cash/1663),
[chipnet post](https://blog.bitjson.com/quantumroot-on-chipnet).

**CHIP-2026-06: Post-Quantum and Hybrid Signatures.** A BCH proposal, posted June 2026,
to let the existing check-signature opcodes verify more than one signature scheme,
including a hybrid that requires both a classical and a post-quantum signature. It
proposes starting with SPHINCS+ (SLH-DSA), which is hash-based. Press coverage
reported a possible lock-in on 2026-11-15 and activation on 2027-05-15. We have not
confirmed its status. Sources: [proposal](https://bitcoincashresearch.org/t/chip-2026-06-post-quantum-and-hybrid-signatures/1864),
[coverage](https://thequantuminsider.com/2026/11/05/bitcoin-cash-proposal-seeks-path-to-quantum-resistant-transactions/).

**Why BFX cannot use either yet.** BitFinite Core forks BCHN v27.0.0 (December 2023).
That predates BCH's May 2025 upgrade (density-based VM limits and big integers) and its
May 2026 upgrade (Loops, Functions, Bitwise, Pay-to-Script). Quantumroot needs at least
the 2025 changes. Post-quantum signatures are also large: the smallest SPHINCS+
signature is 7,856 bytes, while BFX still caps a single stack item at 520 bytes
(`src/script/script.h:26`). BCH raised that cap in 2025.

Adopting either means porting those consensus changes and activating them in a hard
fork that miners, pools and exchanges all upgrade for. That is the realistic path. It
is a planned project, not a quick patch, and we give no date for it.

---

## Roadmap toward post-quantum security

A staged, no-false-promises plan:

1. **Now — exposure minimization (no code change).** Wallets and docs promote strict
   address non-reuse; default to fresh addresses per receive. Discourage P2PK and
   address reuse at the UX layer. This is the single highest-leverage step and it's free.
2. **Now: track upstream.** Follow Quantumroot and CHIP-2026-06 (see
   [Upstream progress](#upstream-progress-bitcoin-cash-2025-2026)) and the NIST
   standards (ML-DSA, SLH-DSA, Falcon). No mainnet chain should rush a non-standardized
   scheme into consensus.
3. **Medium term — optional hash-strengthening.** Favor P2SH-32 (`OP_HASH256`,
   `src/script/standard.cpp:109`) where a 256-bit script-hash commitment is wanted over
   the 160-bit `hash160` margin.
4. **Long term: post-quantum signatures through a planned hard fork.** Port BCH's VM
   upgrades and, once it is settled upstream, its post-quantum signature support, as an
   *opt-in* alongside ECDSA/Schnorr, so users can move funds to quantum-safe outputs
   ahead of any CRQC. A hybrid (classical plus post-quantum) scheme is the likely first
   step. In the Bitcoin Cash lineage, consensus changes like this activate as scheduled
   network upgrades that every node must adopt, so this needs miners, pools and
   exchanges to coordinate. It will be planned and reviewed, not shipped prematurely.

We will update this document as each step is taken, and we will never claim a step is
done before it is.

---

## One-paragraph honest posture statement

BitFinite is **not quantum-resistant**. It authenticates all spending with ECDSA and
Schnorr signatures over secp256k1 and derives all wallet keys on that same curve, so a
quantum computer running Shor's algorithm would recover private keys from any public key
that has appeared on-chain — putting reused addresses, P2PK outputs, and unconfirmed
mempool spends at risk. Its hash functions are only weakened by Grover (PoW and txids
stay safe; unspent `hash160` addresses keep a ~2⁸⁰ cushion), and **mining is not the
quantum threat — signatures are.** The codebase contains no post-quantum cryptography
today. BFX's real advantage is being a fresh 2026 chain with no legacy exposed-key
overhang, giving it the opportunity to minimize exposure through address non-reuse and to
plan an orderly migration to post-quantum signatures. We publish this rather than market
a "quantum-proof" claim, because that honesty is the point.

*Cryptographic primitive inventory with full `file:line` evidence is maintained
alongside [SECURITY.md](SECURITY.md). Report concerns to bitfinitechain@proton.me.*
