# BitFinite Core

BitFinite Core is the full-node software for the **BitFinite (BFX)** protocol — a
proof-of-work chain derived from [Bitcoin Cash Node](https://bitcoincashnode.org/).
It ships the daemon (`bitfinited`), the RPC/CLI client (`bitfinite-cli`), and the
Qt desktop wallet (`bitfinite-qt`).

## Consensus & specification

| | |
|---|---|
| Ticker | **BFX** |
| Max supply | **21,000,000 BFX** |
| Block time (target) | **5 minutes** |
| PoW algorithm | **SHA-256d** |
| Difficulty algorithm | **ASERT** (aserti3-2d), **6-hour half-life** |
| Initial block reward | **50 BFX** |
| Halving interval | **210,000 blocks** (~2 years) |
| Coinbase maturity | **100 blocks** |
| Address format | CashAddr, prefix **`bfx:`** |

## Network parameters

| | |
|---|---|
| P2P port | **19768** |
| RPC port | **19769** |
| Network magic | `0x4246696e` (ASCII **"BFin"**) |
| Genesis block | `000000000900096d5b0f4a3489f919362f12fce06524e15074c3cd3c19aeabea` |
| DNS seeds | `seed.bitfinitechain.org`, `seed-1.bitfinitechain.org`, `seed-2.bitfinitechain.org`, `seed-3.bitfinitechain.org` |

## Downloads (binaries)

Prebuilt Linux and Windows binaries (`bitfinited`, `bitfinite-cli`,
`bitfinite-qt`) are published on the
[Releases](https://github.com/bitfinitechain/bitfinite-core/releases) page.
Verify your download against the release `SHA256SUMS`.

The Linux binaries are produced from a reproducible **depends/Docker** build and
static-link boost/libevent/BDB/miniupnpc, so `bitfinited` runs on a stock
distribution without extra `apt` packages.

## Running a node

Minimal `~/.bitfinite/bitfinite.conf`:

```conf
server=1
txindex=1          # required for the explorer / address indexing
rpcuser=<user>
rpcpassword=<strong-password>
```

```bash
bitfinited -daemon                    # start the daemon
bitfinite-cli getblockchaininfo
bitfinite-cli stop
```

The node discovers peers via the DNS seeds above. To add peers manually, use
`addnode=<host>` in the config or `bitfinite-cli addnode <host> add`.

## Building from source

The full source is in this repository. The build is Dockerized — no host
toolchain required:

```bash
scripts/build-core-docker.sh          # → build-linux/src/{bitfinited,bitfinite-cli,bitfinite-qt}
```

This uses the Bitcoin Cash Node `depends` system inside a container to produce
portable, statically-linked binaries.

## License

BitFinite Core is released under the terms of the **MIT license** — see
[COPYING](COPYING). It derives from Bitcoin Cash Node and Bitcoin Core, whose
copyright notices are retained throughout the source.
