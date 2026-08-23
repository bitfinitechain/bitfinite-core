// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2018-2020 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//
// Unit tests for block-chain checkpoints
//

#include <checkpoints.h>

#include <chain.h>
#include <chainparams.h>
#include <config.h>
#include <consensus/validation.h>
#include <streams.h>
#include <uint256.h>
#include <util/strencodings.h>
#include <validation.h>

#include <pow.h>

#include <test/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <memory>

// REGTEST, not mainnet. Two reasons, and they are the same two that took
// miner_tests off the exclusion list.
//
// The fork cases below need real blocks built on OUR genesis. Upstream could
// hardcode Bitcoin's 2009 blocks 1 and 2 because its test ran against Bitcoin's
// genesis; ours differs, so every hashPrevBlock differs and the whole fixture
// collapses. Regtest lets the headers be mined at run time instead — powLimit is
// 7fffffff… so a valid nonce is found in about one attempt.
//
// It also removes an inconsistency that was there before: the suite fixture was
// MAIN while ban_fork_at_genesis_block used DummyConfig, which defaults to
// REGTEST. That case was checking regtest params against a mainnet chain state
// and passing by luck.
struct RegtestingSetup : public TestingSetup {
    RegtestingSetup() : TestingSetup(CBaseChainParams::REGTEST) {}
};

// Mine a header on top of `prev`. On regtest the target is powLimit, so the
// grind almost always succeeds immediately; the loop is for correctness.
static CBlockHeader MineHeader(const CBlockHeader &prev, uint32_t marker,
                               const Consensus::Params &params) {
    CBlockHeader h;
    h.nVersion = prev.nVersion;
    h.hashPrevBlock = prev.GetHash();
    // Distinguishes sibling headers built on the same parent. Nothing reads it:
    // ProcessNewBlockHeaders validates headers, which carry no transactions.
    h.hashMerkleRoot = ArithToUint256(arith_uint256(marker));
    h.nTime = prev.nTime + 1;
    h.nBits = UintToArith256(params.powLimit).GetCompact();
    h.nNonce = 0;
    while (!CheckProofOfWork(h.GetHash(), h.nBits, params)) {
        ++h.nNonce;
    }
    return h;
}

BOOST_FIXTURE_TEST_SUITE(checkpoints_tests, RegtestingSetup)

BOOST_AUTO_TEST_CASE(sanity) {
    // Upstream tested this against Bitcoin's checkpoints at heights 11111 and
    // 134444. BitFinite pins exactly one checkpoint, at genesis, so those two
    // heights are simply unchecked here — CheckBlock returns true for any hash
    // at a height with no checkpoint, and the two negative assertions could
    // never hold. Rewritten against the checkpoints we actually ship, so the
    // case tests the same logic with our data instead of upstream's.
    const auto params = CreateChainParams(CBaseChainParams::MAIN);
    const CCheckpointData &checkpoints = params->Checkpoints();

    // Exactly one checkpoint, and it is genesis.
    BOOST_CHECK_EQUAL(checkpoints.mapCheckpoints.size(), 1UL);
    const auto genesisIt = checkpoints.mapCheckpoints.find(0);
    BOOST_REQUIRE(genesisIt != checkpoints.mapCheckpoints.end());
    const BlockHash genesisHash = genesisIt->second;
    BOOST_CHECK(genesisHash == params->GenesisBlock().GetHash());

    const BlockHash wrong = BlockHash::fromHex(
        "0000000069e244f73d78e8fd29ba2fd2ed618bd6fa2ee92559f542fdb26e7c1d");

    // The right hash at the checkpoint height passes.
    BOOST_CHECK(Checkpoints::CheckBlock(checkpoints, 0, genesisHash));
    // A wrong hash at the checkpoint height is rejected. This is the only
    // height on this chain where that can happen, and it is what the genesis
    // checkpoint exists to do: ban an alternative genesis.
    BOOST_CHECK(!Checkpoints::CheckBlock(checkpoints, 0, wrong));
    // Any height without a checkpoint accepts anything.
    BOOST_CHECK(Checkpoints::CheckBlock(checkpoints, 1, wrong));
    BOOST_CHECK(Checkpoints::CheckBlock(checkpoints, 11111, wrong));
    BOOST_CHECK(Checkpoints::CheckBlock(checkpoints, 134444, genesisHash));
}

BOOST_AUTO_TEST_CASE(ban_fork_at_genesis_block) {
    DummyConfig config;

    // Sanity check that a checkpoint exists at the genesis block
    auto &checkpoints = config.GetChainParams().Checkpoints().mapCheckpoints;
    assert(checkpoints.find(0) != checkpoints.end());

    // Another precomputed genesis block (with differing nTime) should conflict
    // with the regnet genesis block checkpoint and not be accepted or stored
    // in memory.
    CBlockHeader header =
        CreateGenesisBlock(1296688603, 2, 0x207fffff, 1, 50 * COIN);

    // Header should not be accepted
    CValidationState state;
    CBlockHeader invalid;
    const CBlockIndex *pindex = nullptr;
    BOOST_CHECK(
        !ProcessNewBlockHeaders(config, {header}, state, &pindex, &invalid));
    BOOST_CHECK(state.IsInvalid());
    BOOST_CHECK(pindex == nullptr);
    BOOST_CHECK(invalid.GetHash() == header.GetHash());

    // Sanity check to ensure header was not saved in memory
    {
        LOCK(cs_main);
        BOOST_CHECK(LookupBlockIndex(header.GetHash()) == nullptr);
    }
}

class ChainParamsWithCheckpoints : public CChainParams {
public:
    ChainParamsWithCheckpoints(const CChainParams &chainParams,
                               CCheckpointData &checkpoints)
        : CChainParams(chainParams) {
        checkpointData = checkpoints;
    }
};

/**
 * Four headers mined on top of the regtest genesis:
 *
 *   G ---> A ---> AA (checkpointed)
 *    \       \
 *     \--> B  \-> AB
 *
 * After the node has accepted only A and AA:
 *   * B should be rejected for forking prior to an accepted checkpoint
 *   * AB should be rejected for forking at an accepted checkpoint
 *
 * Upstream builds this from three hex blobs of Bitcoin's 2009 blocks and asserts
 * Bitcoin's genesis hash. None of that survives the fork — our genesis differs,
 * so every hashPrevBlock differs and each blob is an orphan. The structure is
 * mined here instead, which is why the suite runs on regtest.
 */
BOOST_AUTO_TEST_CASE(ban_fork_prior_to_and_at_checkpoints) {
    const auto regtest = CreateChainParams(CBaseChainParams::REGTEST);
    const Consensus::Params &consensus = regtest->GetConsensus();
    const CBlockHeader headerG = regtest->GenesisBlock();

    // The markers only make sibling headers distinct; nothing interprets them.
    const CBlockHeader headerA  = MineHeader(headerG, 1, consensus);
    const CBlockHeader headerAA = MineHeader(headerA, 2, consensus);
    const CBlockHeader headerB  = MineHeader(headerG, 3, consensus);
    const CBlockHeader headerAB = MineHeader(headerA, 4, consensus);

    BOOST_CHECK(headerA.hashPrevBlock == headerG.GetHash());
    BOOST_CHECK(headerB.hashPrevBlock == headerG.GetHash());
    BOOST_CHECK(headerAA.hashPrevBlock == headerA.GetHash());
    BOOST_CHECK(headerAB.hashPrevBlock == headerA.GetHash());
    BOOST_CHECK(headerA.GetHash() != headerB.GetHash());
    BOOST_CHECK(headerAA.GetHash() != headerAB.GetHash());

    // Checkpoint AA at height 2. Built after mining because the hash is not
    // known until then, which is why this is not a static factory as upstream
    // had it.
    CCheckpointData checkpoints = {/* .mapCheckpoints = */ {{2, headerAA.GetHash()}}};
    DummyConfig config(
        std::make_unique<ChainParamsWithCheckpoints>(*regtest, checkpoints));

    CBlockHeader invalid;
    const CBlockIndex *pindex = nullptr;

    // A and AA are accepted.
    for (const CBlockHeader &h : {headerA, headerAA}) {
        CValidationState state;
        BOOST_CHECK(ProcessNewBlockHeaders(config, {h}, state, &pindex, &invalid));
        BOOST_CHECK(state.IsValid());
        BOOST_CHECK(pindex != nullptr);
        BOOST_CHECK(invalid.IsNull());
        pindex = nullptr;
    }

    // B forks below the checkpoint height and must be rejected.
    {
        CValidationState state;
        BOOST_CHECK(!ProcessNewBlockHeaders(config, {headerB}, state, &pindex, &invalid));
        BOOST_CHECK(state.IsInvalid());
        BOOST_CHECK(state.GetRejectCode() == REJECT_CHECKPOINT);
        BOOST_CHECK(state.GetRejectReason() == "bad-fork-prior-to-checkpoint");
        BOOST_CHECK(pindex == nullptr);
        BOOST_CHECK(invalid.GetHash() == headerB.GetHash());
        LOCK(cs_main);
        BOOST_CHECK(LookupBlockIndex(headerB.GetHash()) == nullptr);
    }

    // AB sits at the checkpoint height with the wrong hash.
    {
        CValidationState state;
        BOOST_CHECK(!ProcessNewBlockHeaders(config, {headerAB}, state, &pindex, &invalid));
        BOOST_CHECK(state.IsInvalid());
        BOOST_CHECK(state.GetRejectCode() == REJECT_CHECKPOINT);
        BOOST_CHECK(state.GetRejectReason() == "checkpoint mismatch");
        BOOST_CHECK(pindex == nullptr);
        BOOST_CHECK(invalid.GetHash() == headerAB.GetHash());
        LOCK(cs_main);
        BOOST_CHECK(LookupBlockIndex(headerAB.GetHash()) == nullptr);
    }
}

BOOST_AUTO_TEST_SUITE_END()
