#!/usr/bin/env python3
"""
BFX Genesis Block Miner - BitAxe Difficulty
Mines genesis block with BitAxe-appropriate difficulty
"""

import hashlib
import struct
import time

def hash256(data):
    """Double SHA256 hash"""
    return hashlib.sha256(hashlib.sha256(data).digest()).digest()

def mine_genesis_block(timestamp, bits_hex, reward_satoshis, message):
    """Mine a genesis block with given parameters"""
    
    # Convert bits from hex
    bits = int(bits_hex, 16)
    
    # Build coinbase transaction
    tx_version = struct.pack("<I", 1)
    tx_in_count = b"\x01"
    prev_output = b"\x00" * 32 + b"\xff\xff\xff\xff"
    
    # Script sig with message
    height_bytes = struct.pack("<I", 545259519)
    push_4 = struct.pack("<B", 4)
    msg_bytes = message.encode('utf-8')
    script_sig_data = height_bytes + push_4 + struct.pack("<B", len(msg_bytes)) + msg_bytes
    script_sig = struct.pack("<B", len(script_sig_data)) + script_sig_data
    
    sequence = b"\xff\xff\xff\xff"
    tx_out_count = b"\x01"
    output_value = struct.pack("<Q", reward_satoshis)
    
    # P2PKH output (using CreateGenesisBlock pubkey)
    pubkey_script = bytes.fromhex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5fac")
    output_script = struct.pack("<B", len(pubkey_script)) + pubkey_script
    
    locktime = b"\x00\x00\x00\x00"
    
    # Complete transaction
    coinbase_tx = (
        tx_version + tx_in_count + prev_output + script_sig + sequence +
        tx_out_count + output_value + output_script + locktime
    )
    
    # Merkle root
    merkle_root = hash256(coinbase_tx)
    
    print(f"Mining BFX Genesis Block (BitAxe Difficulty)")
    print(f"=" * 70)
    print(f"Timestamp: {timestamp} ({time.strftime('%Y-%m-%d %H:%M:%S UTC', time.gmtime(timestamp))})")
    print(f"Bits: {bits_hex}")
    print(f"Message: {message}")
    print(f"Merkle Root: {merkle_root[::-1].hex()}")
    print(f"=" * 70)
    print()
    
    # Calculate target from bits
    exponent = bits >> 24
    mantissa = bits & 0xFFFFFF
    target = mantissa * (2 ** (8 * (exponent - 3)))
    
    print(f"Target: {target:064x}")
    print(f"Mining... (this will take several minutes with BitAxe difficulty)")
    print()
    
    # Mine
    version = 1
    prev_block = b"\x00" * 32
    nonce = 0
    start_time = time.time()
    last_update = start_time
    
    while True:
        header = (
            struct.pack("<I", version) +
            prev_block +
            merkle_root +
            struct.pack("<I", timestamp) +
            struct.pack("<I", bits) +
            struct.pack("<I", nonce)
        )
        
        block_hash = hash256(header)
        hash_int = int.from_bytes(block_hash[::-1], 'big')
        
        if hash_int < target:
            elapsed = time.time() - start_time
            hash_rate = nonce / elapsed if elapsed > 0 else 0
            
            print(f"\n✅ GENESIS BLOCK FOUND!")
            print(f"=" * 70)
            print(f"Nonce: {nonce}")
            print(f"Hash: {block_hash[::-1].hex()}")
            print(f"Time: {elapsed:.2f}s ({elapsed/60:.2f} minutes)")
            print(f"Hash rate: {hash_rate:,.0f} H/s")
            print(f"=" * 70)
            print()
            print("Update src/chainparams.cpp CMainParams() with:")
            print(f"")
            print(f"    genesis = CreateGenesisBlock({timestamp}, {nonce}, {bits_hex}, 1, {reward_satoshis // 100000000} * COIN);")
            print(f"")
            print(f"    assert(consensus.hashGenesisBlock ==")
            print(f"           uint256S(\"0x{block_hash[::-1].hex()}\"));")
            print(f"    assert(genesis.hashMerkleRoot ==")
            print(f"           uint256S(\"0x{merkle_root[::-1].hex()}\"));")
            print()
            return {
                'timestamp': timestamp,
                'nonce': nonce,
                'bits': bits_hex,
                'hash': block_hash[::-1].hex(),
                'merkle': merkle_root[::-1].hex(),
                'message': message
            }
        
        nonce += 1
        
        # Progress update every 100k hashes
        if nonce % 100000 == 0:
            now = time.time()
            if now - last_update >= 5.0:  # Update every 5 seconds
                elapsed = now - start_time
                hash_rate = nonce / elapsed if elapsed > 0 else 0
                eta = ((target / hash_rate) - elapsed) if hash_rate > 0 else 0
                print(f"Nonce: {nonce:,} | Hash rate: {hash_rate:,.0f} H/s | Elapsed: {elapsed:.0f}s | ETA: {eta:.0f}s", end="\r")
                last_update = now

if __name__ == "__main__":
    # BFX Mainnet Genesis Parameters (BitAxe Difficulty)
    TIMESTAMP = 1738224000  # 2026-01-30 00:00:00 UTC
    BITS = "0x1d01a000"      # BitAxe difficulty (~100)
    REWARD = 100 * 100000000  # 100 BFX
    MESSAGE = "BFX 2026-01-30: BitFinite launches with BitAxe mining"
    
    print("This will take approximately 5-30 minutes depending on your CPU...")
    print("Estimated with modern CPU: ~10-15 minutes")
    print()
    
    result = mine_genesis_block(TIMESTAMP, BITS, REWARD, MESSAGE)
