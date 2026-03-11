#!/usr/bin/env python3
"""
BFX Genesis Block Miner - Fast Version
Uses easier difficulty for quicker mining
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
    
    # P2PKH output
    pubkey_script = bytes.fromhex("76a914") + b"\x00" * 20 + bytes.fromhex("88ac")
    output_script = struct.pack("<B", len(pubkey_script)) + pubkey_script
    
    locktime = b"\x00\x00\x00\x00"
    
    # Complete transaction
    coinbase_tx = (
        tx_version + tx_in_count + prev_output + script_sig + sequence +
        tx_out_count + output_value + output_script + locktime
    )
    
    # Merkle root
    merkle_root = hash256(coinbase_tx)
    
    print(f"Mining BFX Genesis Block")
    print(f"=" * 60)
    print(f"Timestamp: {timestamp} ({time.strftime('%Y-%m-%d %H:%M:%S UTC', time.gmtime(timestamp))})")
    print(f"Bits: {bits_hex}")
    print(f"Message: {message}")
    print(f"Merkle Root: {merkle_root[::-1].hex()}")
    print(f"=" * 60)
    print()
    
    # Calculate target from bits
    exponent = bits >> 24
    mantissa = bits & 0xFFFFFF
    target = mantissa * (2 ** (8 * (exponent - 3)))
    
    print(f"Target: {target:064x}")
    print(f"Mining... (this may take a while)")
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
            print(f"=" * 60)
            print(f"Nonce: {nonce}")
            print(f"Hash: {block_hash[::-1].hex()}")
            print(f"Time: {elapsed:.2f}s ({elapsed/60:.2f} minutes)")
            print(f"Hash rate: {hash_rate:,.0f} H/s")
            print(f"=" * 60)
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
            if now - last_update >= 1.0:  # Update every second
                elapsed = now - start_time
                hash_rate = nonce / elapsed if elapsed > 0 else 0
                print(f"Nonce: {nonce:,} | Hash rate: {hash_rate:,.0f} H/s | Time: {elapsed:.0f}s", end="\r")
                last_update = now

if __name__ == "__main__":
    # BFX Mainnet Genesis Parameters
    TIMESTAMP = 1738224000  # 2026-01-30 00:00:00 UTC
    BITS = "0x1f7fffff"      # Much easier difficulty for faster mining
    REWARD = 100 * 100000000  # 100 BFX
    MESSAGE = "BFX 2026-01-30: BitFinite launches with enhanced security"
    
    result = mine_genesis_block(TIMESTAMP, BITS, REWARD, MESSAGE)
