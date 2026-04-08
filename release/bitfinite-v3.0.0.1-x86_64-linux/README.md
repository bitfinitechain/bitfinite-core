# BitFinite Core v3.0.0.1 — Linux x86_64

## Quick Start

```bash
# Install binaries
sudo cp bin/* /usr/local/bin/

# Create data directory
mkdir -p ~/.bitfinite

# Run the daemon
bitfinited -daemon

# Check status
bitfinite-cli getblockchaininfo

# Or run the Qt wallet
bitfinite-qt
```

## Binaries

| Binary | Description |
|--------|-------------|
| `bitfinited` | Full node daemon |
| `bitfinite-cli` | RPC command-line client |
| `bitfinite-qt` | Qt GUI wallet |
| `bitfinite-tx` | Transaction utility |
| `bitfinite-wallet` | Wallet utility |

## Network Parameters

| Parameter | Value |
|-----------|-------|
| Block Time | 5 minutes |
| Initial Reward | 50 BFX |
| Halving Interval | 210,000 blocks (~2 years) |
| Max Supply | ~21,000,000 BFX |
| Default Port | 19768 |
| RPC Port | 19769 |
| Address Prefix | `bfx:` |
| Algorithm | SHA-256d (PoW) |
| Difficulty | ASERT DAA |

## Genesis Block

```
Hash:   00000000c3c7c4eefbb569b5062edbbabe76a66793b5558cadde2a47ec111ae2
Time:   2026-03-11 00:00:03 UTC
Launch: April 1, 2026 (Official Mainnet Launch)
```

## Sample Config (~/.bitfinite/bitfinite.conf)

```
server=1
daemon=1
rpcuser=bfxrpc
rpcpassword=CHANGE_ME_TO_A_STRONG_PASSWORD
rpcport=19769
```
