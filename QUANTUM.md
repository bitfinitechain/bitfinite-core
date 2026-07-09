# BitFinite (BFX) — Quantum Computing Posture

**Subject:** BitFinite Core (fork of Bitcoin Cash Node v27, Bitcoin lineage)
**Assessment date:** 2026-06-27 · **Branch:** `master`

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

## Roadmap toward post-quantum security

A staged, no-false-promises plan:

1. **Now — exposure minimization (no code change).** Wallets and docs promote strict
   address non-reuse; default to fresh addresses per receive. Discourage P2PK and
   address reuse at the UX layer. This is the single highest-leverage step and it's free.
2. **Now — monitoring.** Track NIST PQC standards (ML-DSA/Dilithium, SLH-DSA/SPHINCS+,
   Falcon) and the Bitcoin/BCH research on quantum migration (e.g. pay-to-quantum-resistant
   proposals). No mainnet chain should rush a non-standardized scheme into consensus.
3. **Medium term — optional hash-strengthening.** Favor P2SH-32 (`OP_HASH256`,
   `src/script/standard.cpp:109`) where a 256-bit script-hash commitment is wanted over
   the 160-bit `hash160` margin.
4. **Long term — PQC signature migration via soft fork.** Introduce a post-quantum
   signature type (a new output/opcode) as an *opt-in* alongside ECDSA/Schnorr, letting
   users move funds to quantum-safe outputs ahead of any CRQC. A hybrid (classical +
   PQC) scheme is the likely first step. This is a future consensus change, planned and
   reviewed — not shipped prematurely.

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
