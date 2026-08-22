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

        chainTxData = ChainTxData{
            1782432000, // Genesis Time (2026-06-26)
            0,          // 0 Txs
            0.0         // 0 tx/sec
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
        consensus.BIP16Height = 514;
        consensus.BIP34Height = 21111;
        consensus.BIP34Hash = BlockHash::fromHex("0000000023b3a96d3484e5abb3755c413e7d41500f8e2a5c3f0dd01299cd8ef8");
        consensus.BIP65Height = 581885;
        consensus.BIP66Height = 330776;
        consensus.CSVHeight = 770112;
        consensus.powLimit = uint256S("00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60;
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nASERTHalfLife = 60 * 60;
        consensus.nMinimumChainWork = ChainParamsConstants::TESTNET_MINIMUM_CHAIN_WORK;
        consensus.defaultAssumeValid = ChainParamsConstants::TESTNET_DEFAULT_ASSUME_VALID;
        consensus.uahfHeight = 1155875;
        consensus.daaHeight = 1188697;
        consensus.magneticAnomalyHeight = 1267996;
        consensus.gravitonHeight = 1341711;
        consensus.phononHeight = 1378460;
        consensus.axionActivationTime = 1605441600;
        consensus.upgrade8Height = 1500205;
        consensus.upgrade9Height = 1552787;
        consensus.upgrade10ActivationTime = 1715774400;
        consensus.upgrade11ActivationTime = 0;  // no node expiry — see consensus/params.h
        consensus.nDefaultConsensusBlockSize = DEFAULT_CONSENSUS_BLOCK_SIZE;
        consensus.nDefaultGeneratedBlockSizePercent = 50.0;
        assert(consensus.nDefaultGeneratedBlockSizePercent >= 0.0 &&
               consensus.nDefaultGeneratedBlockSizePercent <= 100.0);
        assert(consensus.GetDefaultGeneratedBlockSizeBytes() <= consensus.nDefaultConsensusBlockSize);
        consensus.asertAnchorParams = Consensus::Params::ASERTAnchor{
            1421481,
            0x1d00ffff,
            1605445400,
        };
        consensus.ablaConfig = abla::Config::MakeDefault(consensus.nDefaultConsensusBlockSize, /* fixedSize = */ true);
        assert(abla::State(consensus.ablaConfig, 0).GetBlockSizeLimit() == consensus.nDefaultConsensusBlockSize);
        assert(consensus.ablaConfig.IsFixedSize());

        diskMagic[0] = 0x0b;
        diskMagic[1] = 0x11;
        diskMagic[2] = 0x09;
        diskMagic[3] = 0x07;
        netMagic[0] = 0xf4;
        netMagic[1] = 0xe5;
        netMagic[2] = 0xf3;
        netMagic[3] = 0xf4;
        nDefaultPort = 18333;
        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 60;
        m_assumed_chain_state_size = 2;

        // BFX testnet genesis: re-mined for the v3 timestamp (nBits 0x1d00ffff).
        genesis = CreateGenesisBlock(1296688604, 1168812312, 0x1d00ffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();

        assert(consensus.hashGenesisBlock ==
               uint256S("0x000000004b6e4759b93dbe0f3df9fa909d65830e75e88117aadffe8e9779ea4f"));
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
        chainTxData = ChainTxData{1669510532, 63972968, 0.00310};
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
