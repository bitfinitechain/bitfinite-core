#!/usr/bin/env python3
"""
BFX Genesis Block Miner
Mines a new genesis block for BitFinite mainnet
"""

import hashlib
import struct
import time

def hash256(data):
    """Double SHA256 hash"""
    return hashlib.sha256(hashlib.sha256(data).digest()).digest()

def mine_genesis_block(timestamp, bits, reward_satoshis):
    """
    Mine a genesis block
    
    Args:
        timestamp: Unix timestamp for genesis block
        bits: Difficulty target in compact format
        reward_satoshis: Block reward in satoshis
    """
    
    # Genesis message
    message = b"BFX 2026-01-30: BitFinite launches with enhanced security"
    
    # Build coinbase transaction
    # Version
    tx_version = struct.pack("<I", 1)
    
    # Input count
    tx_in_count = b"\x01"
    
    # Previous output (null for coinbase)
    prev_output = b"\x00" * 32 + b"\xff\xff\xff\xff"
    
    # Script sig
    script_sig_data = (
        struct.pack("<I", 545259519) +  # Height/extra nonce
        struct.pack("<B", 4) +           # Push 4 bytes
        struct.pack("<I", len(message)) +
        message
    )
    script_sig = struct.pack("<B", len(script_sig_data)) + script_sig_data
    
    # Sequence
    sequence = b"\xff\xff\xff\xff"
    
    # Output count
    tx_out_count = b"\x01"
    
    # Output value (50 BFX in satoshis)
    output_value = struct.pack("<Q", reward_satoshis)
    
    # Output script (P2PKH to a standard address)
    pubkey_script = bytes.fromhex("76a914") + b"\x00" * 20 + bytes.fromhex("88ac")
    output_script = struct.pack("<B", len(pubkey_script)) + pubkey_script
    
    # Locktime
    locktime = b"\x00\x00\x00\x00"
    
    # Build complete transaction
    coinbase_tx = (
        tx_version +
        tx_in_count +
        prev_output +
        script_sig +
        sequence +
        tx_out_count +
        output_value +
        output_script +
        locktime
    )
    
    # Calculate merkle root (single transaction)
    merkle_root = hash256(coinbase_tx)
    
    print(f"Mining genesis block...")
    print(f"Timestamp: {timestamp}")
    print(f"Bits: 0x{bits:08x}")
    print(f"Message: {message.decode()}")
    print(f"Merkle Root: {merkle_root[::-1].hex()}")
    print()
    
    # Mine the block
    version = 1
    prev_block = b"\x00" * 32
    
    nonce = 0
    start_time = time.time()
    
    while True:
        # Build block header
        header = (
            struct.pack("<I", version) +
            prev_block +
            merkle_root +
            struct.pack("<I", timestamp) +
            struct.pack("<I", bits) +
            struct.pack("<I", nonce)
        )
        
        # Hash the header
        block_hash = hash256(header)
        
        # Check if we found a valid block
        if int.from_bytes(block_hash[::-1], 'big') < (2 ** 256) // (2 ** (256 - 29)):  # Approximate for 0x207fffff
            elapsed = time.time() - start_time
            hash_rate = nonce / elapsed if elapsed > 0 else 0
            
            print(f"✅ GENESIS BLOCK FOUND!")
            print(f"Nonce: {nonce}")
            print(f"Hash: {block_hash[::-1].hex()}")
            print(f"Time: {elapsed:.2f}s")
            print(f"Hash rate: {hash_rate:.0f} H/s")
            print()
            print("Update chainparams.cpp with:")
            print(f"  Time: {timestamp}")
            print(f"  Nonce: {nonce}")
            print(f"  Bits: 0x{bits:08x}")
            print(f"  Hash: 0x{block_hash[::-1].hex()}")
            print(f"  Merkle: 0x{merkle_root[::-1].hex()}")
            return
        
        nonce += 1
        
        if nonce % 100000 == 0:
            elapsed = time.time() - start_time
            hash_rate = nonce / elapsed if elapsed > 0 else 0
            print(f"Nonce: {nonce:,} | Hash rate: {hash_rate:.0f} H/s", end="\r")

if __name__ == "__main__":
    # BFX Genesis parameters
    TIMESTAMP = 1738224000  # 2026-01-30 00:00:00 UTC
    BITS = 0x207fffff        # Easy difficulty
    REWARD = 100 * 100000000  # 100 BFX in satoshis
    
    print("=" * 60)
    print("BFX Genesis Block Miner")
    print("=" * 60)
    print()
    
    mine_genesis_block(TIMESTAMP, BITS, REWARD)
