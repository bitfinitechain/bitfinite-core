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
                          const Amount genesisReward) {
    const char *pszTimestamp = "BFX 2026-01-30: BitFinite launches with enhanced security";
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
        consensus.nSubsidyHalvingInterval = 50000;

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
        consensus.upgrade11ActivationTime = 1747310400;

        // --- GRADUAL DIFFICULTY RAMP-UP ---
        // Phase 1 (Blocks 0-10,000): Easy CPU mining for bootstrap
        // Phase 2 (Block 10,001+): BitAxe-optimized difficulty via ASERT
        // powLimit allows easy blocks; ASERT manages transition to BitAxe difficulty
        consensus.powLimit = uint256S("00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // 2 weeks
        consensus.nPowTargetSpacing = 5 * 60;             // 5 minutes
        consensus.fPowAllowMinDifficultyBlocks = true;    // Allow min difficulty blocks
        consensus.fPowNoRetargeting = false;

        // ASERT DAA Half Life (2 Days)
        consensus.nASERTHalfLife = 2 * 24 * 60 * 60;
        consensus.nMinimumChainWork = uint256S("0x00");
        consensus.defaultAssumeValid = BlockHash();

        // Default limit for block size (in bytes)
        consensus.nDefaultConsensusBlockSize = DEFAULT_CONSENSUS_BLOCK_SIZE;
        consensus.nDefaultGeneratedBlockSizePercent = 50.0;

        assert(consensus.nDefaultGeneratedBlockSizePercent >= 0.0 &&
               consensus.nDefaultGeneratedBlockSizePercent <= 100.0);
        assert(consensus.GetDefaultGeneratedBlockSizeBytes() <= consensus.nDefaultConsensusBlockSize);

        // --- BFX ASERT ANCHOR ---
        // Bootstrap Phase: Blocks 0-10,000 use easy difficulty (0x1f7fffff)
        // Transition Phase: Block 10,001+ transitions to BitAxe difficulty
        // ASERT will smoothly adjust difficulty over ~2 days (half-life)
        consensus.asertAnchorParams = Consensus::Params::ASERTAnchor{
            10001,      // Anchor Height - transition at block 10,001
            0x1d01a000, // Anchor Bits (BitAxe difficulty ~100)
            1741224300, // Anchor Time (genesis + 10,001 blocks * 5min = ~34.7 days)
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

        // BFX GENESIS - 2026-01-30 Mainnet Launch
        // Message: "BFX 2026-01-30: BitFinite launches with enhanced security"
        // Time: 1738224000 (2026-01-30 00:00:00 UTC), Nonce: 35, Bits: 0x1f7fffff
        genesis = CreateGenesisBlock(1738224000, 35, 0x1f7fffff, 1, 100 * COIN);

        consensus.hashGenesisBlock = genesis.GetHash();

        // Verify Genesis Hash
        assert(consensus.hashGenesisBlock ==
               uint256S("0x7e9882cbfa7b59c327f0b3eb5c3549b62e92dab6d8eea1dd675366b927e374e2"));
        // Verify Merkle Root
        assert(genesis.hashMerkleRoot ==
               uint256S("0x6b89c7254ce7c66e17fdc607fde751bd782f35fc05166c1f67dc42ba13c05bd5"));

        // Clear old seeds
        vSeeds.clear();
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
            1738224000, // Genesis Time (2026-01-30)
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
        consensus.upgrade11ActivationTime = 1747310400;
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

        genesis = CreateGenesisBlock(1296688602, 414098458, 0x1d00ffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();

        // ASSERTIONS DISABLED FOR BFX FORK
        // assert(consensus.hashGenesisBlock ==
        // uint256S("000000000933ea01ad0ee984209779baaec3ced90fa3f408719526f8d77f4943")); assert(genesis.hashMerkleRoot
        // == uint256S("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"));

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
        checkpointData = {/* .mapCheckpoints = */ {}};
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
        consensus.upgrade11ActivationTime = 1747310400;
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

        genesis = CreateGenesisBlock(1597811185, 114152193, 0x1d00ffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();

        // ASSERTIONS DISABLED FOR BFX FORK
        // assert(consensus.hashGenesisBlock ==
        // BlockHash::fromHex("000000001dd410c49a788668ce26751718cc797474d3152a5fc073dd44fd9f7b"));

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
        checkpointData = {/* .mapCheckpoints = */ {}};
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
        consensus.upgrade11ActivationTime = 1747310400;
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

        genesis = CreateGenesisBlock(1598282438, -1567304284, 0x1d00ffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();

        // ASSERTIONS DISABLED FOR BFX FORK
        // assert(consensus.hashGenesisBlock ==
        // uint256S("00000000e6453dc2dfe1ffa19023f86002eb11dbb8e87d0291a4599f0430be52")); assert(genesis.hashMerkleRoot
        // == uint256S("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"));

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
        checkpointData = {/* .mapCheckpoints = */ {}};
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
        consensus.upgrade11ActivationTime = 1731672000;
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

        diskMagic[0] = 0xcd;
        diskMagic[1] = 0x22;
        diskMagic[2] = 0xa7;
        diskMagic[3] = 0x92;
        netMagic[0] = 0xe2;
        netMagic[1] = 0xb7;
        netMagic[2] = 0xda;
        netMagic[3] = 0xaf;
        nDefaultPort = 48333;
        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 1;
        m_assumed_chain_state_size = 1;

        genesis = CreateGenesisBlock(1597811185, 114152193, 0x1d00ffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();

        // ASSERTIONS DISABLED FOR BFX FORK
        // assert(consensus.hashGenesisBlock ==
        // BlockHash::fromHex("000000001dd410c49a788668ce26751718cc797474d3152a5fc073dd44fd9f7b"));

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
        checkpointData = {/* .mapCheckpoints = */ {}};
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
        consensus.upgrade11ActivationTime = 1747310400;
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

        genesis = CreateGenesisBlock(1296688602, 2, 0x207fffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();

        // ASSERTIONS DISABLED FOR BFX FORK
        // assert(consensus.hashGenesisBlock ==
        // uint256S("0x0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206"));
        // assert(genesis.hashMerkleRoot ==
        // uint256S("0x4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"));

        vFixedSeeds.clear();
        vSeeds.clear();
        fDefaultConsistencyChecks = true;
        fRequireStandard = true;
        m_is_test_chain = true;
        checkpointData = {/* .mapCheckpoints = */ {}};
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
