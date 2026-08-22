# genesis-mine

Finds — or verifies — the nonce for a genesis block header.

This exists because the genesis verification table in `SECURITY.md` asserts that
every network's `nTime` / `nNonce` reproduces the hash asserted in
`chainparams.cpp`. That claim should be checkable by anyone reading it, not
taken on trust, and one row of that table was wrong for months precisely
because nothing recomputed it.

## Build

```
gcc -O3 -march=native -o genesis-mine genesis-mine.c -lcrypto -lpthread \
    -Wno-deprecated-declarations
```

## Verify an existing genesis

Pass the nonce as a sixth argument and it checks instead of searching. The
merkle root and nBits come from the network's block in `chainparams.cpp`.

```
./genesis-mine <merkle-root> <nTime> <nBits> <version> <threads> <nonce>
```

For example, mainnet:

```
./genesis-mine b256645de4317fcb50bf170bdae579dbf667c47d579a19d2a049e3ed41608609 \
               1782691200 1d00ffff 1 4 3406937121
-> verify nonce=3406937121 hash=000000000900096d5b0f4a3489f919362f12fce06524e15074c3cd3c19aeabea
```

which is the hash `CMainParams` asserts.

## Mine a new one

Drop the nonce argument and it searches. At `nBits` 0x1d00ffff the expected work
is 2^32 hashes; four cores of a 2026 laptop found the current testnet genesis in
77 seconds.

```
./genesis-mine <merkle-root> <nTime> <nBits> <version> <threads>
```

The merkle root does **not** depend on `nTime` — the coinbase carries the
`pszTimestamp` string, not the header timestamp — so changing only the timestamp
lets you reuse the merkle root already in `chainparams.cpp`. Changing the
coinbase string does not: re-derive the merkle root first, or every network's
nonce silently stops matching, which is `SECURITY.md` finding 5.
