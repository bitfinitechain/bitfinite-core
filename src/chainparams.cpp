// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2023 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"


#include "amount.h"
#include "chainparamsconstants.h"
#include "chainparamsseeds.h"
#include "consensus/abla.h"
#include "consensus/consensus.h"
#include "consensus/merkle.h"
#include "netbase.h"
#include "tinyformat.h"
#include "util/strencodings.h"
#include "util/system.h"

#include <cassert>
#include <cstring>
#include <memory>
#include <stdexcept>

static CBlock CreateGenesisBlock(const char *pszTimestamp, const CScript &genesisOutputScript, uint32_t nTime,
                                 uint32_t nNonce, uint32_t nBits, int32_t nVersion, const Amount genesisReward) {
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << ScriptInt::fromIntUnchecked(545259519) << CScriptNum::fromIntUnchecked(4)
                                       << std::vector<uint8_t>((const uint8_t *)pszTimestamp,
                                                               (const uint8_t *)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime = nTime;
    genesis.nBits = nBits;
    genesis.nNonce = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

/**
 * Build the genesis block.
 */
CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion,
                          const Amount genesisReward, const char *pszTimestamp) {
    const CScript genesisOutputScript = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909"
                                                              "a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112"
                                                              "de5c384df7ba0b8d578a4c702b6bf11d5f")
                                                  << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward);
}

/**
 * Main network
 */
class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = CBaseChainParams::MAIN;
        consensus.nSubsidyHalvingInterval = 210000;

        // --- BFX CLEAN SLATE: ACTIVATE ALL FORKS IMMEDIATELY ---
        // Setting heights to 0 ensures "Big Blocks" and "Script" features are active from Block 1.
        consensus.BIP16Height = 0;
        consensus.BIP34Height = 0;
        consensus.BIP34Hash = BlockHash();
        consensus.BIP65Height = 0;
        consensus.BIP66Height = 0;
        consensus.CSVHeight = 0;
        consensus.uahfHeight = 0;
        consensus.daaHeight = 0;
        consensus.magneticAnomalyHeight = 0;
        consensus.gravitonHeight = 0;
        consensus.phononHeight = 0;
        consensus.axionActivationTime = 1605441600;
        consensus.upgrade8Height = 0;
        consensus.upgrade9Height = 0;
        consensus.upgrade10ActivationTime = 1715774400;
        consensus.upgrade11ActivationTime = 0;  // no node expiry — see consensus/params.h

        // --- DIFFICULTY: ASERT FROM GENESIS, RE-ANCHORED AT ~70,000 ---
        // Genesis re-anchor (2026-06-29, fair launch): the chain OPERATES at ~70,000
        // difficulty from block 2 onward via the ASERT anchor below (anchor bits =
        // 0x1b00efab). powLimit stays at diff-1 because the genesis block is diff-1
        // (CPU-mined) and IS PoW-checked on read-back (ReadBlockFromDisk), so it must
        // satisfy powLimit. Block 1 == powLimit (diff-1) is one trivial block; block 2+
        // target ~70k (5-min blocks at ~1 TH/s). The short 6h half-life keeps any
        // hashrate burst bounded; with continuous mining, difficulty stays at ~70k and
        // never eases back toward the diff-1 floor.
        consensus.powLimit = uint256S("00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // 2 weeks
        consensus.nPowTargetSpacing = 5 * 60;             // 5 minutes
        // SECURITY: Must be false on mainnet. Setting to true allows trivially
        // easy blocks, enabling chain takeover with minimal hashrate.
        // Was true during bootstrap phase — now hardened for production.
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;

        // ASERT DAA Half Life (6 hours) — fast adaptation so a hashrate burst is
        // absorbed in hours, not days, sharply limiting any excess minting.
        consensus.nASERTHalfLife = 6 * 60 * 60;
        // Anti-fake-chain / IBD trust anchor. Set to a well-buried block
        // (height 2639, ~200 deep at the time of this release) so a syncing node
        // rejects any presented chain with less accumulated work, and can skip
        // signature verification below the assume-valid block. Bump these on
        // future releases as the chain grows (chainwork from getblockheader).
        consensus.nMinimumChainWork = uint256S(
            "0x0000000000000000000000000000000000000000000000001932d1a31b9f30fd");
        consensus.defaultAssumeValid = BlockHash::fromHex(
            "00000000000018aa22cb9a4c2a84df7bfa3c4146c8acddad02610143529d5b70");

        // Default limit for block size (in bytes)
        consensus.nDefaultConsensusBlockSize = DEFAULT_CONSENSUS_BLOCK_SIZE;
        consensus.nDefaultGeneratedBlockSizePercent = 50.0;

        assert(consensus.nDefaultGeneratedBlockSizePercent >= 0.0 &&
               consensus.nDefaultGeneratedBlockSizePercent <= 100.0);
        assert(consensus.GetDefaultGeneratedBlockSizeBytes() <= consensus.nDefaultConsensusBlockSize);

        // --- BFX ASERT ANCHOR (genesis, ~70,000 difficulty) ---
        // Anchored at genesis (height 0) so Axion/ASERT is active from block 1.
        // Anchor Bits = 0x1b00efab (~diff 70,000) is the ASERT reference target, so
        // block 2+ target ~70k (~5-min blocks at ~1 TH/s). Block 1 == powLimit (~70k).
        // nPrevBlockTime MUST equal the genesis nTime (anchor == genesis).
        consensus.asertAnchorParams = Consensus::Params::ASERTAnchor{
            0,          // Anchor Height (genesis)
            0x1b00efab, // Anchor Bits (~difficulty 70,000)
            1782691200, // Anchor Time (== genesis nTime)
        };

        consensus.ablaConfig = abla::Config::MakeDefault(consensus.nDefaultConsensusBlockSize, /* fixedSize = */ false);
        assert(abla::State(consensus.ablaConfig, 0).GetBlockSizeLimit() == consensus.nDefaultConsensusBlockSize);
        assert(!consensus.ablaConfig.IsFixedSize());

        // Magic Bytes: B F i n
        diskMagic[0] = 0x42;
        diskMagic[1] = 0x46;
        diskMagic[2] = 0x69;
        diskMagic[3] = 0x6e;
        netMagic[0] = 0x42;
        netMagic[1] = 0x46;
        netMagic[2] = 0x69;
        netMagic[3] = 0x6e;
        nDefaultPort = 19768;
        nPruneAfterHeight = 100000;
        m_assumed_blockchain_size = 1;
        m_assumed_chain_state_size = 0;

        // BFX GENESIS - 2026-06-29 fair-launch re-anchor (00:00:00 UTC)
        // 50 BFX reward, 210,000 block halving (~2 years), 21M max supply.
        // Genesis is diff-1 (0x1d00ffff) and trusted/unchecked; the ~70k difficulty
        // FLOOR comes from powLimit + the ASERT anchor above (block 1 == powLimit ~70k).
        // Mined: nTime=1782691200, nNonce=3406937121
        genesis = CreateGenesisBlock(1782691200, 3406937121, 0x1d00ffff, 1, 50 * COIN,
                                     "BFX 2026-06-29: BitFinite fair launch - sound money, freely mined");

        consensus.hashGenesisBlock = genesis.GetHash();

        // Verify Genesis Hash
        assert(consensus.hashGenesisBlock ==
               uint256S("0x000000000900096d5b0f4a3489f919362f12fce06524e15074c3cd3c19aeabea"));
        // Verify Merkle Root
        assert(genesis.hashMerkleRoot ==
               uint256S("0xb256645de4317fcb50bf170bdae579dbf667c47d579a19d2a049e3ed41608609"));

        // Clear old seeds
        vSeeds.clear();
        // Dynamic DNS seed (bitcoin-seeder crawler on seed-3) — returns many live
        // peers; primary discovery source.
        vSeeds.emplace_back("seed.bitfinitechain.org");
        // Static per-node hostnames (one A record each) — fallbacks.
        vSeeds.emplace_back("seed-1.bitfinitechain.org");
        vSeeds.emplace_back("seed-2.bitfinitechain.org");
        vSeeds.emplace_back("seed-3.bitfinitechain.org");

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<uint8_t>(1, 0);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<uint8_t>(1, 5);
        base58Prefixes[SECRET_KEY] = std::vector<uint8_t>(1, 128);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};
        cashaddrPrefix = "bfx";

        vFixedSeeds.clear();

        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        m_is_test_chain = false;

        // Clean Checkpoints
        checkpointData = {/* .mapCheckpoints = */ {
            {0, consensus.hashGenesisBlock},
        }};

        // Measured from the live chain on 2026-08-22, not guessed.
        //
        // These were {1782432000, 0, 0.0} — a timestamp that was not actually
        // genesis (genesis nTime is 1782691200) and, more importantly, a zero
        // transaction count and a zero rate. Zero is not a harmless placeholder
        // here. GuessVerificationProgress divides the chain's transaction count
        // by an estimated total; with nTxCount and dTxRate both zero, any chain
        // holding at least one transaction takes the else-branch and the
        // estimate collapses to the count itself, so the ratio is exactly 1.0 at
        // every height. A node a quarter of the way through initial sync
        // reported verificationprogress 1.0, so anything deciding "am I synced
        // yet" from that RPC was reading a constant. Measured after the fix on a
        // fresh node: 0.249 at height 4163, 0.672 at 11181, 0.9998 at the tip.
        //
        // Anchor block 16434, hash 000000000000003b8bd300b88776a4872f8c6cf05817584b819af4e07d5d3474.
        // The rate is the trailing-window figure from getchaintxstats, and it
        // cross-checks against first principles: mainnet targets 300s blocks and
        // is almost entirely coinbase-only, so ~1/300 = 0.00333 tx/s is expected
        // and 0.00345 is what was measured.
        chainTxData = ChainTxData{
            1787407530, // Time of block 16434
            16836,      // Chain-wide transaction count at that block
            0.00345     // Transactions per second, trailing window
        };
    }
};

/**
 * Testnet (v3)
 * NOTE: Genesis asserts disabled to prevent crashes on non-main networks
 * due to timestamp string change.
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = CBaseChainParams::TESTNET;
        consensus.nSubsidyHalvingInterval = 4200;
        consensus.BIP16Height = 0;
        consensus.BIP34Height = 0;
        consensus.BIP34Hash = BlockHash();  // BIP34Height is 0; the BCH hash here was meaningless
        consensus.BIP65Height = 0;
        consensus.BIP66Height = 0;
        consensus.CSVHeight = 0;
        consensus.powLimit = uint256S("00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60;
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nASERTHalfLife = 60 * 60;
        consensus.nMinimumChainWork = uint256S("0x00");  // fresh chain — BCH's value would never be reached
        consensus.defaultAssumeValid = BlockHash();      // no trusted history on a chain we are starting
        consensus.uahfHeight = 0;
        consensus.daaHeight = 0;
        consensus.magneticAnomalyHeight = 0;
        consensus.gravitonHeight = 0;
        consensus.phononHeight = 0;
        consensus.axionActivationTime = 1605441600;
        consensus.upgrade8Height = 0;
        consensus.upgrade9Height = 0;
        consensus.upgrade10ActivationTime = 1715774400;
        consensus.upgrade11ActivationTime = 0;  // no node expiry — see consensus/params.h
        consensus.nDefaultConsensusBlockSize = DEFAULT_CONSENSUS_BLOCK_SIZE;
        consensus.nDefaultGeneratedBlockSizePercent = 50.0;
        assert(consensus.nDefaultGeneratedBlockSizePercent >= 0.0 &&
               consensus.nDefaultGeneratedBlockSizePercent <= 100.0);
        assert(consensus.GetDefaultGeneratedBlockSizeBytes() <= consensus.nDefaultConsensusBlockSize);
        consensus.asertAnchorParams = Consensus::Params::ASERTAnchor{
            0,          // Anchor Height (genesis) — was BCH height 1421481, unreachable here
            0x1d00ffff, // Anchor Bits (powLimit / diff-1) — testnet stays cheap to mine
            1787400000, // Anchor Time (== genesis nTime)
        };
        consensus.ablaConfig = abla::Config::MakeDefault(consensus.nDefaultConsensusBlockSize, /* fixedSize = */ true);
        assert(abla::State(consensus.ablaConfig, 0).GetBlockSizeLimit() == consensus.nDefaultConsensusBlockSize);
        assert(consensus.ablaConfig.IsFixedSize());

        diskMagic[0] = 0x42;  // 'B'
        diskMagic[1] = 0x46;  // 'F'
        diskMagic[2] = 0x74;  // 't'
        diskMagic[3] = 0x65;  // 'e'   -- "BFte", matching mainnet's "BFin"
        netMagic[0] = 0x42;
        netMagic[1] = 0x46;
        netMagic[2] = 0x74;
        netMagic[3] = 0x65;
        nDefaultPort = 29768;
        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 60;
        m_assumed_chain_state_size = 2;

        // BFX testnet genesis, re-mined 2026-08-22 for a timestamp that is actually
        // ours (nBits 0x1d00ffff).
        //
        // The nonce was re-mined once before, when the coinbase string changed, but
        // the TIMESTAMP was left at 1296688604 — two seconds after Bitcoin testnet3's
        // genesis, in February 2011. That mattered because the ASERT anchor time
        // tracks genesis nTime, so ASERT measured the chain against a 15.5-year
        // backlog: it expected ~818,000 blocks to exist and saw 0. The target was
        // therefore clamped to powLimit for the next ~818,000 blocks, meaning
        // difficulty could never rise off diff-1 and any real miner would race the
        // chain through that many blocks before spacing became meaningful.
        //
        // Mainnet never had this: its anchor time is its own genesis nTime.
        genesis = CreateGenesisBlock(1787400000, 575664822, 0x1d00ffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();

        assert(consensus.hashGenesisBlock ==
               uint256S("0x00000000498add4157e47db0e5b06bdedd668af44c60762c37992be703d1ed2e"));
        assert(genesis.hashMerkleRoot ==
               uint256S("0x8b091b56222f40fb242b3811b07cf9b75e48024501058e66c0c1c5e653bd8a1d"));

        vFixedSeeds.clear();
        vSeeds.clear();
        // Single A record, DNS-only (not Cloudflare-proxied). A proxied record
        // would hand peers a Cloudflare address, and Cloudflare forwards HTTP,
        // not P2P — the connection to 29768 would simply fail. Every mainnet
        // seed record is DNS-only for the same reason.
        vSeeds.emplace_back("testnet-seed.bitfinitechain.org");
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<uint8_t>(1, 111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<uint8_t>(1, 196);
        base58Prefixes[SECRET_KEY] = std::vector<uint8_t>(1, 239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};
        cashaddrPrefix = "bfxtest";

        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        m_is_test_chain = true;
        // A checkpoint at genesis bans forks that rewrite the genesis block
        // itself. Upstream ships one on every network; ours were emptied during
        // the fork (presumably because the hashes changed) and only mainnet was
        // restored. Their absence made checkpoints_tests abort on a hard
        // assert(), and that abort cascaded into ~449 downstream test failures.
        // Derived from consensus.hashGenesisBlock rather than a literal, so it
        // cannot drift from the genesis block it is meant to pin.
        checkpointData = {/* .mapCheckpoints = */ {
            {0, consensus.hashGenesisBlock},
        }};
        // Was {1669510532, 63972968, 0.00310} — Bitcoin Cash's numbers, inherited
        // at the fork. 63.9M transactions is a chain we are not, so a fully
        // synced testnet node reported verificationprogress 0.0000017: the same
        // defect as mainnet's, in the opposite direction. Measured at block 112
        // on 2026-08-22.
        //
        // The rate is DERIVED, not measured. The observed figure right after
        // launch was 0.0327 tx/s, which only reflects the ASERT ramp mining
        // blocks every few seconds; using it would peg a synced node at a few
        // percent forever. Steady state is one coinbase per 600s target spacing.
        chainTxData = ChainTxData{
            1787408045, // Time of block 112
            116,        // Chain-wide transaction count at that block
            0.00167     // 1 tx per 600s target spacing
        };
    }
};

/**
 * Testnet (v4)
 */
class CTestNet4Params : public CChainParams {
public:
    CTestNet4Params() {
        strNetworkID = CBaseChainParams::TESTNET4;
        consensus.nSubsidyHalvingInterval = 4200;
        consensus.BIP16Height = 1;
        consensus.BIP34Height = 2;
        consensus.BIP34Hash = BlockHash::fromHex("00000000b0c65b1e03baace7d5c093db0d6aac224df01484985ffd5e86a1a20c");
        consensus.BIP65Height = 3;
        consensus.BIP66Height = 4;
        consensus.CSVHeight = 5;
        consensus.powLimit = uint256S("00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60;
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nASERTHalfLife = 60 * 60;
        consensus.nMinimumChainWork = ChainParamsConstants::TESTNET4_MINIMUM_CHAIN_WORK;
        consensus.defaultAssumeValid = ChainParamsConstants::TESTNET4_DEFAULT_ASSUME_VALID;
        consensus.uahfHeight = 6;
        consensus.daaHeight = 3000;
        consensus.magneticAnomalyHeight = 4000;
        consensus.gravitonHeight = 5000;
        consensus.phononHeight = 0;
        consensus.axionActivationTime = 1605441600;
        consensus.upgrade8Height = 95464;
        consensus.upgrade9Height = 148043;
        consensus.upgrade10ActivationTime = 1715774400;
        consensus.upgrade11ActivationTime = 0;  // no node expiry — see consensus/params.h
        consensus.nDefaultConsensusBlockSize = 2 * ONE_MEGABYTE;
        consensus.nDefaultGeneratedBlockSizePercent = 100.0;
        assert(consensus.nDefaultGeneratedBlockSizePercent >= 0.0 &&
               consensus.nDefaultGeneratedBlockSizePercent <= 100.0);
        assert(consensus.GetDefaultGeneratedBlockSizeBytes() <= consensus.nDefaultConsensusBlockSize);
        consensus.asertAnchorParams = Consensus::Params::ASERTAnchor{
            16844,
            0x1d00ffff,
            1605451779,
        };
        consensus.ablaConfig = abla::Config::MakeDefault(consensus.nDefaultConsensusBlockSize, /* fixedSize = */ true);
        assert(abla::State(consensus.ablaConfig, 0).GetBlockSizeLimit() == consensus.nDefaultConsensusBlockSize);
        assert(consensus.ablaConfig.IsFixedSize());

        diskMagic[0] = 0xcd;
        diskMagic[1] = 0x22;
        diskMagic[2] = 0xa7;
        diskMagic[3] = 0x92;
        netMagic[0] = 0xe2;
        netMagic[1] = 0xb7;
        netMagic[2] = 0xda;
        netMagic[3] = 0xaf;
        nDefaultPort = 28333;
        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 1;
        m_assumed_chain_state_size = 1;

        // BFX testnet4/chipnet genesis: re-mined for the v3 timestamp (nBits 0x1d00ffff).
        genesis = CreateGenesisBlock(1597811185, 1633225309, 0x1d00ffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();

        assert(consensus.hashGenesisBlock ==
               uint256S("0x000000008501486885bd552b802438f68dc5a219f60212e6ec47b9cec077e173"));
        assert(genesis.hashMerkleRoot ==
               uint256S("0x8b091b56222f40fb242b3811b07cf9b75e48024501058e66c0c1c5e653bd8a1d"));

        vFixedSeeds.clear();
        vSeeds.clear();
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<uint8_t>(1, 111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<uint8_t>(1, 196);
        base58Prefixes[SECRET_KEY] = std::vector<uint8_t>(1, 239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};
        cashaddrPrefix = "bfxtest";
        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        m_is_test_chain = true;
        // A checkpoint at genesis bans forks that rewrite the genesis block
        // itself. Upstream ships one on every network; ours were emptied during
        // the fork (presumably because the hashes changed) and only mainnet was
        // restored. Their absence made checkpoints_tests abort on a hard
        // assert(), and that abort cascaded into ~449 downstream test failures.
        // Derived from consensus.hashGenesisBlock rather than a literal, so it
        // cannot drift from the genesis block it is meant to pin.
        checkpointData = {/* .mapCheckpoints = */ {
            {0, consensus.hashGenesisBlock},
        }};
        chainTxData = {1669510845, 126464, 0.0017};
    }
};

/**
 * Scalenet
 */
class CScaleNetParams : public CChainParams {
public:
    CScaleNetParams() {
        strNetworkID = CBaseChainParams::SCALENET;
        consensus.nSubsidyHalvingInterval = 4200;
        consensus.BIP16Height = 1;
        consensus.BIP34Height = 2;
        consensus.BIP34Hash = BlockHash::fromHex("00000000c8c35eaac40e0089a83bf5c5d9ecf831601f98c21ed4a7cb511a07d8");
        consensus.BIP65Height = 3;
        consensus.BIP66Height = 4;
        consensus.CSVHeight = 5;
        consensus.powLimit = uint256S("00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60;
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nASERTHalfLife = 2 * 24 * 60 * 60;
        consensus.nMinimumChainWork = ChainParamsConstants::SCALENET_MINIMUM_CHAIN_WORK;
        consensus.defaultAssumeValid = ChainParamsConstants::SCALENET_DEFAULT_ASSUME_VALID;
        consensus.uahfHeight = 6;
        consensus.daaHeight = 3000;
        consensus.magneticAnomalyHeight = 4000;
        consensus.gravitonHeight = 5000;
        consensus.phononHeight = 0;
        consensus.axionActivationTime = 1605441600;
        consensus.upgrade8Height = 10'006;
        consensus.upgrade9Height = 10'006;
        consensus.upgrade10ActivationTime = 1715774400;
        consensus.upgrade11ActivationTime = 0;  // no node expiry — see consensus/params.h
        consensus.nDefaultConsensusBlockSize = 256 * ONE_MEGABYTE;
        consensus.nDefaultGeneratedBlockSizePercent = 6.25;
        assert(consensus.nDefaultGeneratedBlockSizePercent >= 0.0 &&
               consensus.nDefaultGeneratedBlockSizePercent <= 100.0);
        assert(consensus.GetDefaultGeneratedBlockSizeBytes() <= consensus.nDefaultConsensusBlockSize);
        consensus.asertAnchorParams.reset();
        consensus.ablaConfig = abla::Config::MakeDefault(consensus.nDefaultConsensusBlockSize, /* fixedSize = */ false);
        assert(abla::State(consensus.ablaConfig, 0).GetBlockSizeLimit() == consensus.nDefaultConsensusBlockSize);
        assert(!consensus.ablaConfig.IsFixedSize());

        diskMagic[0] = 0xba;
        diskMagic[1] = 0xc2;
        diskMagic[2] = 0x2d;
        diskMagic[3] = 0xc4;
        netMagic[0] = 0xc3;
        netMagic[1] = 0xaf;
        netMagic[2] = 0xe1;
        netMagic[3] = 0xa2;
        nDefaultPort = 38333;
        nPruneAfterHeight = 10000;
        m_assumed_blockchain_size = 250;
        m_assumed_chain_state_size = 50;

        // BFX scalenet genesis: re-mined for the v3 timestamp (nBits 0x1d00ffff).
        genesis = CreateGenesisBlock(1598282438, 3022631194, 0x1d00ffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();

        assert(consensus.hashGenesisBlock ==
               uint256S("0x00000000096af1e07b1f4cf1c22aa42b8d1dca0bf8a3860a5f0951fd1ad08853"));
        assert(genesis.hashMerkleRoot ==
               uint256S("0x8b091b56222f40fb242b3811b07cf9b75e48024501058e66c0c1c5e653bd8a1d"));

        vFixedSeeds.clear();
        vSeeds.clear();
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<uint8_t>(1, 111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<uint8_t>(1, 196);
        base58Prefixes[SECRET_KEY] = std::vector<uint8_t>(1, 239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};
        cashaddrPrefix = "bfxtest";
        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        m_is_test_chain = true;
        // A checkpoint at genesis bans forks that rewrite the genesis block
        // itself. Upstream ships one on every network; ours were emptied during
        // the fork (presumably because the hashes changed) and only mainnet was
        // restored. Their absence made checkpoints_tests abort on a hard
        // assert(), and that abort cascaded into ~449 downstream test failures.
        // Derived from consensus.hashGenesisBlock rather than a literal, so it
        // cannot drift from the genesis block it is meant to pin.
        checkpointData = {/* .mapCheckpoints = */ {
            {0, consensus.hashGenesisBlock},
        }};
        chainTxData = {1660124250, 489847053, 0.00001};
    }
};

/**
 * Chipnet
 */
class CChipNetParams : public CChainParams {
public:
    CChipNetParams() {
        strNetworkID = CBaseChainParams::CHIPNET;
        consensus.nSubsidyHalvingInterval = 4200;
        consensus.BIP16Height = 1;
        consensus.BIP34Height = 2;
        consensus.BIP34Hash = BlockHash::fromHex("00000000b0c65b1e03baace7d5c093db0d6aac224df01484985ffd5e86a1a20c");
        consensus.BIP65Height = 3;
        consensus.BIP66Height = 4;
        consensus.CSVHeight = 5;
        consensus.powLimit = uint256S("00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60;
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nASERTHalfLife = 60 * 60;
        consensus.nMinimumChainWork = ChainParamsConstants::CHIPNET_MINIMUM_CHAIN_WORK;
        consensus.defaultAssumeValid = ChainParamsConstants::CHIPNET_DEFAULT_ASSUME_VALID;
        consensus.uahfHeight = 6;
        consensus.daaHeight = 3000;
        consensus.magneticAnomalyHeight = 4000;
        consensus.gravitonHeight = 5000;
        consensus.phononHeight = 0;
        consensus.axionActivationTime = 1605441600;
        consensus.upgrade8Height = 95464;
        consensus.upgrade9Height = 121956;
        consensus.upgrade10ActivationTime = 1700049600;
        consensus.upgrade11ActivationTime = 0;  // no node expiry — see consensus/params.h
        consensus.nDefaultConsensusBlockSize = 2 * ONE_MEGABYTE;
        consensus.nDefaultGeneratedBlockSizePercent = 100.0;
        assert(consensus.nDefaultGeneratedBlockSizePercent >= 0.0 &&
               consensus.nDefaultGeneratedBlockSizePercent <= 100.0);
        assert(consensus.GetDefaultGeneratedBlockSizeBytes() <= consensus.nDefaultConsensusBlockSize);
        consensus.asertAnchorParams = Consensus::Params::ASERTAnchor{
            16844,
            0x1d00ffff,
            1605451779,
        };
        consensus.ablaConfig = abla::Config::MakeDefault(consensus.nDefaultConsensusBlockSize, /* fixedSize = */ false);
        assert(abla::State(consensus.ablaConfig, 0).GetBlockSizeLimit() == consensus.nDefaultConsensusBlockSize);
        assert(!consensus.ablaConfig.IsFixedSize());

        // SECURITY: Chipnet uses unique magic bytes to avoid collision
        // with Testnet4 (which was previously identical: 0xe2b7daaf).
        diskMagic[0] = 0xcd;
        diskMagic[1] = 0x22;
        diskMagic[2] = 0xa7;
        diskMagic[3] = 0x92;
        netMagic[0] = 0xc4;
        netMagic[1] = 0xb8;
        netMagic[2] = 0xdb;
        netMagic[3] = 0xb0;
        nDefaultPort = 48333;
        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 1;
        m_assumed_chain_state_size = 1;

        // BFX testnet4/chipnet genesis: re-mined for the v3 timestamp (nBits 0x1d00ffff).
        genesis = CreateGenesisBlock(1597811185, 1633225309, 0x1d00ffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();

        assert(consensus.hashGenesisBlock ==
               uint256S("0x000000008501486885bd552b802438f68dc5a219f60212e6ec47b9cec077e173"));
        assert(genesis.hashMerkleRoot ==
               uint256S("0x8b091b56222f40fb242b3811b07cf9b75e48024501058e66c0c1c5e653bd8a1d"));

        vFixedSeeds.clear();
        vSeeds.clear();
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<uint8_t>(1, 111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<uint8_t>(1, 196);
        base58Prefixes[SECRET_KEY] = std::vector<uint8_t>(1, 239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};
        cashaddrPrefix = "bfxtest";
        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        m_is_test_chain = true;
        // A checkpoint at genesis bans forks that rewrite the genesis block
        // itself. Upstream ships one on every network; ours were emptied during
        // the fork (presumably because the hashes changed) and only mainnet was
        // restored. Their absence made checkpoints_tests abort on a hard
        // assert(), and that abort cascaded into ~449 downstream test failures.
        // Derived from consensus.hashGenesisBlock rather than a literal, so it
        // cannot drift from the genesis block it is meant to pin.
        checkpointData = {/* .mapCheckpoints = */ {
            {0, consensus.hashGenesisBlock},
        }};
        chainTxData = {1669512215, 126405, 0.0018};
    }
};

/**
 * Regression test
 */
class CRegTestParams : public CChainParams {
public:
    CRegTestParams() {
        strNetworkID = CBaseChainParams::REGTEST;
        consensus.nSubsidyHalvingInterval = 150;
        consensus.BIP16Height = 0;
        consensus.BIP34Height = 100000000;
        consensus.BIP34Hash = BlockHash();
        consensus.BIP65Height = 1351;
        consensus.BIP66Height = 1251;
        consensus.CSVHeight = 576;
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60;
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        consensus.nASERTHalfLife = 2 * 24 * 60 * 60;
        consensus.nMinimumChainWork = uint256S("0x00");
        consensus.defaultAssumeValid = BlockHash();
        consensus.uahfHeight = 0;
        consensus.daaHeight = 0;
        consensus.magneticAnomalyHeight = 0;
        consensus.gravitonHeight = 0;
        consensus.phononHeight = 0;
        consensus.axionActivationTime = 1605441600;
        consensus.upgrade8Height = 0;
        consensus.upgrade9Height = 0;
        consensus.upgrade10ActivationTime = 1715774400;
        consensus.upgrade11ActivationTime = 0;  // no node expiry — see consensus/params.h
        consensus.nDefaultConsensusBlockSize = DEFAULT_CONSENSUS_BLOCK_SIZE;
        consensus.nDefaultGeneratedBlockSizePercent = 50.0;
        assert(consensus.nDefaultGeneratedBlockSizePercent >= 0.0 &&
               consensus.nDefaultGeneratedBlockSizePercent <= 100.0);
        assert(consensus.GetDefaultGeneratedBlockSizeBytes() <= consensus.nDefaultConsensusBlockSize);
        consensus.ablaConfig = abla::Config::MakeDefault(consensus.nDefaultConsensusBlockSize, /* fixedSize = */ false);
        assert(abla::State(consensus.ablaConfig, 0).GetBlockSizeLimit() == consensus.nDefaultConsensusBlockSize);
        assert(!consensus.ablaConfig.IsFixedSize());

        diskMagic[0] = 0xfa;
        diskMagic[1] = 0xbf;
        diskMagic[2] = 0xb5;
        diskMagic[3] = 0xda;
        netMagic[0] = 0xda;
        netMagic[1] = 0xb5;
        netMagic[2] = 0xbf;
        netMagic[3] = 0xfa;
        nDefaultPort = 18444;
        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 0;
        m_assumed_chain_state_size = 0;

        // BFX regtest genesis: re-mined for the v3 timestamp (nBits 0x207fffff).
        genesis = CreateGenesisBlock(1296688602, 3, 0x207fffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();

        assert(consensus.hashGenesisBlock ==
               uint256S("0x1af18d35c21aa6e032692ef5f63b015a36f72d8719c7f765b2309a50f9975428"));
        assert(genesis.hashMerkleRoot ==
               uint256S("0x8b091b56222f40fb242b3811b07cf9b75e48024501058e66c0c1c5e653bd8a1d"));

        vFixedSeeds.clear();
        vSeeds.clear();
        fDefaultConsistencyChecks = true;
        fRequireStandard = true;
        m_is_test_chain = true;
        // A checkpoint at genesis bans forks that rewrite the genesis block
        // itself. Upstream ships one on every network; ours were emptied during
        // the fork (presumably because the hashes changed) and only mainnet was
        // restored. Their absence made checkpoints_tests abort on a hard
        // assert(), and that abort cascaded into ~449 downstream test failures.
        // Derived from consensus.hashGenesisBlock rather than a literal, so it
        // cannot drift from the genesis block it is meant to pin.
        checkpointData = {/* .mapCheckpoints = */ {
            {0, consensus.hashGenesisBlock},
        }};
        chainTxData = ChainTxData{0, 0, 0};
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<uint8_t>(1, 111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<uint8_t>(1, 196);
        base58Prefixes[SECRET_KEY] = std::vector<uint8_t>(1, 239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};
        cashaddrPrefix = "bfxreg";
    }
};

static std::unique_ptr<CChainParams> globalChainParams;

const CChainParams &Params() {
    assert(globalChainParams);
    return *globalChainParams;
}

std::unique_ptr<CChainParams> CreateChainParams(const std::string &chain) {
    if (chain == CBaseChainParams::MAIN) {
        return std::make_unique<CMainParams>();
    }

    if (chain == CBaseChainParams::TESTNET) {
        return std::make_unique<CTestNetParams>();
    }

    if (chain == CBaseChainParams::TESTNET4) {
        return std::make_unique<CTestNet4Params>();
    }

    if (chain == CBaseChainParams::REGTEST) {
        return std::make_unique<CRegTestParams>();
    }

    if (chain == CBaseChainParams::SCALENET) {
        return std::make_unique<CScaleNetParams>();
    }

    if (chain == CBaseChainParams::CHIPNET) {
        return std::make_unique<CChipNetParams>();
    }

    throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string &network) {
    SelectBaseParams(network);
    globalChainParams = CreateChainParams(network);
}

SeedSpec6::SeedSpec6(const char *pszHostPort) {
    const CService service = LookupNumeric(pszHostPort, 0);
    if (!service.IsValid() || service.GetPort() == 0)
        throw std::invalid_argument(strprintf("Unable to parse numeric-IP:port pair: %s", pszHostPort));
    if (!service.IsRoutable()) throw std::invalid_argument(strprintf("Not routable: %s", pszHostPort));
    *this = SeedSpec6(service);
}
