import hashlib
import struct
import time
import binascii
import sys

pszTimestamp = "BitFinite: Dec 23 2025 The unique BitFinite Era begins."
nTime = 1766475312
nBits = 0x207fffff
nVersion = 1
nReward = 1 * 100000000

print(f"--- GENESIS MINER FOR BitFinite ---")
print(f"Timestamp: {pszTimestamp}")

if len(sys.argv) < 2:
    print("\n[ERROR] Provide the Merkle Root from debug.log!")
    print("Usage: python3 bfx_genesis_miner.py <MERKLE_ROOT_HEX>")
    exit()

merkle_root_hex = sys.argv[1]
merkle_bin = binascii.unhexlify(merkle_root_hex)[::-1]
nonce = 0

def dsha256(data):
    return hashlib.sha256(hashlib.sha256(data).digest()).digest()

print(f"Mining with Root: {merkle_root_hex}...")

while True:
    header = (
        struct.pack("<I", nVersion) +
        bytes(32) +
        merkle_bin +
        struct.pack("<I", nTime) +
        struct.pack("<I", nBits) +
        struct.pack("<I", nonce)
    )
    hash_val = dsha256(header)[::-1].hex()
    
    if hash_val.startswith("0000"):
        print(f"\nSUCCESS! Nonce: {nonce} | Hash: {hash_val} | Time: {nTime}")
        break
    nonce += 1
    if nonce % 1000000 == 0:
        print(f"Scanning... {nonce/1000000:.1f}M", end="\r")
