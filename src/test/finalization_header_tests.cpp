// Copyright (c) 2020 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <chain.h>
#include <chainparams.h>
#include <config.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <pow.h>
#include <test/setup_common.h>
#include <util/strencodings.h>
#include <util/system.h>
#include <util/time.h>
#include <validation.h>

BOOST_FIXTURE_TEST_SUITE(finalization_header_tests, TestChain100Setup)

static COutPoint InsecureRandOutPoint() {
    return COutPoint(TxId(InsecureRand256()), 0);
}

// Construct a valid block
static CBlock GetBlock(const Config &config, CBlockIndex *pindexPrev) {
    CBlock block;
    CMutableTransaction tx;
    const Consensus::Params &params = config.GetChainParams().GetConsensus();
    const uint32_t powLimit = UintToArith256(params.powLimit).GetCompact();

    block.nVersion = 4;
    block.hashPrevBlock = pindexPrev->GetBlockHash();
    block.nBits = powLimit;
    block.nTime = pindexPrev->nTime + 1;

    tx.vin.resize(1);
    tx.vin[0].scriptSig.resize(10);
    tx.vout.resize(1);
    tx.vout[0].nValue = 50 * COIN;
    block.vtx.resize(2);
    block.vtx[0] = MakeTransactionRef(tx);
    tx.vin[0].prevout = InsecureRandOutPoint();
    block.vtx[1] = MakeTransactionRef(tx);

    block.hashMerkleRoot = BlockMerkleRoot(block);

    while (!CheckProofOfWork(block.GetHash(), block.nBits, params)) {
        ++block.nNonce;
    }

    return block;
}

// Checks that a new header is accepted (processed and stored)
static void CreateNewHeaderAndCheckIsAccepted(const Config &config, CBlockIndex *hashPrev) {
    const CBlockHeader &header = GetBlock(config, hashPrev).GetBlockHeader();
    CValidationState state;
    CBlockHeader invalid;
    const CBlockIndex *pindex = nullptr;
    BOOST_CHECK(ProcessNewBlockHeaders(config, {header}, state, &pindex, &invalid));
    BOOST_CHECK(state.IsValid());
    BOOST_CHECK(pindex != nullptr);
    BOOST_CHECK(invalid.GetHash() != header.GetHash());

    // Sanity check: accepted header should be stored in memory
    {
        LOCK(cs_main);
        BOOST_CHECK(LookupBlockIndex(header.GetHash()) != nullptr);
    }
}

// Checks that a new header is rejected (not processed and not stored)
static void CreateNewHeaderAndCheckIsRejected(const Config &config, CBlockIndex *hashPrev) {
    const CBlockHeader &header = GetBlock(config, hashPrev).GetBlockHeader();
    CValidationState state;
    CBlockHeader invalid;
    const CBlockIndex *pindex = nullptr;
    BOOST_CHECK(!ProcessNewBlockHeaders(config, {header}, state, &pindex, &invalid));
    BOOST_CHECK(state.IsInvalid());
    BOOST_CHECK(state.GetRejectCode() == REJECT_AGAINST_FINALIZED);
    BOOST_CHECK(state.GetRejectReason() == "bad-header-finalization");
    BOOST_CHECK(pindex == nullptr);
    BOOST_CHECK(invalid.GetHash() == header.GetHash());

    // Sanity check: rejected header should not be stored in memory
    {
        LOCK(cs_main);
        BOOST_CHECK(LookupBlockIndex(header.GetHash()) == nullptr);
    }
}

// Regression test for the unbounded header flood (CVE-2019-25220 class).
//
// The finalization check is the only per-header test that ties a header to OUR
// chain; every other check in AcceptBlockHeader is self-contained and none of
// them bounds how many headers get stored. But pindexFinalized is null for the
// first -finalizationdelay (2h) after every process start and until a block is
// connected, it is never persisted, and every restart re-arms that window. So
// in that window a peer could park unbounded fork headers in mapBlockIndex,
// which is never pruned, and they survive into the block index on disk.
//
// The fix anchors to maxreorgdepth back from the active tip while nothing is
// finalized yet, but only for a chain that declares a minimum chain work and
// only once the tip has reached it. This test covers both sides of that
// condition, which is also what keeps regtest able to reorg freely.
BOOST_AUTO_TEST_CASE(deepForkHeaderRefusedBeforeFirstFinalization) {
    const Config &config = GetConfig();

    // The fixture's 100 blocks are all too close to now to be finalized, so
    // this is the unguarded window, reproduced exactly as a restart produces it.
    {
        LOCK(cs_main);
        BOOST_REQUIRE(GetFinalizedBlock() == nullptr);
    }

    CBlockIndex *deepFork = nullptr;
    {
        LOCK(cs_main);
        // One block deeper than maxreorgdepth, so it can never be a legitimate
        // reorg candidate: the node would refuse it once anything is finalized.
        deepFork = ::ChainActive().Tip()->GetAncestor(::ChainActive().Tip()->nHeight - DEFAULT_MAX_REORG_DEPTH - 1);
    }
    BOOST_REQUIRE(deepFork != nullptr);

    // Regtest declares no minimum chain work, so the fallback stays off and the
    // header is stored exactly as before. This is the half that keeps deep
    // reorgs working on regtest, where developers and the functional tests rely
    // on them.
    BOOST_REQUIRE(nMinimumChainWork == 0);
    CreateNewHeaderAndCheckIsAccepted(config, deepFork);

    // Now behave like a chain that does declare one, with the tip past it.
    const arith_uint256 savedMinimumChainWork = nMinimumChainWork;
    nMinimumChainWork = arith_uint256(1);
    {
        LOCK(cs_main);
        BOOST_REQUIRE(::ChainActive().Tip()->nChainWork >= nMinimumChainWork);
        BOOST_REQUIRE(GetFinalizedBlock() == nullptr);
    }

    // THE ASSERTION THAT USED TO FAIL: with nothing finalized, this header was
    // accepted and stored. An attacker repeats it without limit.
    CreateNewHeaderAndCheckIsRejected(config, deepFork);

    // A header that does descend from the fallback anchor is still accepted, so
    // the gate rejects the fork rather than stalling normal header sync.
    CBlockIndex *shallow = nullptr;
    {
        LOCK(cs_main);
        shallow = ::ChainActive().Tip();
    }
    CreateNewHeaderAndCheckIsAccepted(config, shallow);

    nMinimumChainWork = savedMinimumChainWork;
}

BOOST_AUTO_TEST_CASE(headerFinalizationEnabledDefault) {
    const Config &config = GetConfig();
    CScript p2pk_scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    CBlock block;
    {
        LOCK(cs_main);
        // We should have no finalized block because the 100 blocks
        // generated by the test setup are too close to "now"
        BOOST_CHECK_MESSAGE(GetFinalizedBlock() == nullptr,
                            "No block finalized (tip at height "
                                << ::ChainActive().Tip()->nHeight << ")");
    }

    const int64_t finalizationStartTime = GetTime() + DEFAULT_MIN_FINALIZATION_DELAY;
    for (uint32_t i = 0; i < DEFAULT_MAX_REORG_DEPTH; i++) {
        CBlockIndex *blockToFinalize = ::ChainActive().Tip()->GetAncestor(
            ::ChainActive().Tip()->nHeight - DEFAULT_MAX_REORG_DEPTH + 1);
        SetMockTime(finalizationStartTime + 1 + i);
        block = CreateAndProcessBlock({}, p2pk_scriptPubKey);
        LOCK(cs_main);
        // Should get blocks finalizing
        BOOST_CHECK_MESSAGE(GetFinalizedBlock() == blockToFinalize,
                            "Block finalized at height "
                                << blockToFinalize->nHeight
                                << " (tip at height "
                                << ::ChainActive().Tip()->nHeight << ")");
    }
    // Block at height=100 is now finalized.
    // Working back from chain tip depth, we create headers for fork blocks.
    // Headers above finalized block depth should be accepted regardless if
    // `-finalizeheaders` is enabled or disabled, as long as the other header
    // checks pass.
    CBlockIndex *blockAtHeightOfNewHeader;
    for (uint32_t i = 0; i < DEFAULT_MAX_REORG_DEPTH; i++) {
        blockAtHeightOfNewHeader = ::ChainActive().Tip()->GetAncestor(::ChainActive().Tip()->nHeight - i);
        CreateNewHeaderAndCheckIsAccepted(config, blockAtHeightOfNewHeader->pprev);
    }

    // A header deeper than DEFAULT_MAX_REORG_DEPTH should be rejected
    {
        blockAtHeightOfNewHeader = ::ChainActive().Tip()->GetAncestor(
            ::ChainActive().Tip()->nHeight - DEFAULT_MAX_REORG_DEPTH);
        CreateNewHeaderAndCheckIsRejected(config, blockAtHeightOfNewHeader->pprev);
    }
} // headerFinalizationEnabledDefault

BOOST_AUTO_TEST_CASE(headerFinalizationEnabledNonDefault) {
    gArgs.ForceSetArg("-maxreorgdepth", std::string("1"));
    gArgs.ForceSetArg("-finalizeheaders", std::string("1"));
    const Config &config = GetConfig();
    CScript p2pk_scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    CBlock block;
    {
        LOCK(cs_main);
        // We should have no finalized block because the 100 blocks
        // generated by the test setup are too close to "now"
        BOOST_CHECK_MESSAGE(GetFinalizedBlock() == nullptr,
                            "No block finalized (tip at height "
                                << ::ChainActive().Tip()->nHeight << ")");
    }

    // Create a finalized tip by moving up sufficiently in time
    const int64_t finalizationStartTime = GetTime() + DEFAULT_MIN_FINALIZATION_DELAY;
    SetMockTime(finalizationStartTime);
    block = CreateAndProcessBlock({}, p2pk_scriptPubKey);
    {
        LOCK(cs_main);
        CBlockIndex *blockToFinalize = ::ChainActive().Tip()->pprev;
        BOOST_CHECK_MESSAGE(GetFinalizedBlock() == blockToFinalize,
                            "Block finalized at height "
                                << blockToFinalize->nHeight
                                << " (tip at height "
                                << ::ChainActive().Tip()->nHeight << ")");
    }

    CBlockIndex *blockAtHeightOfNewHeader;
    // Block at height=100 is now finalized.
    // A header at same height as tip should be rejected
    {
        blockAtHeightOfNewHeader = ::ChainActive().Tip()->GetAncestor(
            ::ChainActive().Tip()->nHeight - 1);
        CreateNewHeaderAndCheckIsRejected(config, blockAtHeightOfNewHeader->pprev);
    }
    // A header one below tip should also be rejected
    {
        blockAtHeightOfNewHeader = ::ChainActive().Tip()->GetAncestor(
            ::ChainActive().Tip()->nHeight - 2);
        CreateNewHeaderAndCheckIsRejected(config, blockAtHeightOfNewHeader->pprev);
    }
    // A header building on tip should be accepted
    {
        blockAtHeightOfNewHeader = ::ChainActive().Tip();
        CreateNewHeaderAndCheckIsAccepted(config, blockAtHeightOfNewHeader);
    }
    gArgs.ClearArg("-finalizeheaders");
    gArgs.ClearArg("-maxreorgdepth");
} // headerFinalizationEnabledNonDefault

BOOST_AUTO_TEST_CASE(headerFinalizationDisabledOne) {
    // Set to finalize block below tip
    gArgs.ForceSetArg("-maxreorgdepth", std::string("1"));
    // Disable the finalization of headers
    gArgs.ForceSetArg("-finalizeheaders", std::string("0"));
    // Turn off finalization delay so we can omit the time advances.
    // We are only interested in the finalized block depth and whether
    // we can fork headers anywhere
    gArgs.ForceSetArg("-finalizationdelay", std::string("0"));

    const Config &config = GetConfig();
    CScript p2pk_scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    CBlock block;
    {
        LOCK(cs_main);
        // We should have no finalized block because the 100 blocks
        // generated by the test setup are too close to "now";
        BOOST_CHECK_MESSAGE(GetFinalizedBlock() == nullptr,
                            "No block finalized (tip at height "
                                << ::ChainActive().Tip()->nHeight << ")");
    }

    CBlockIndex *blockToFinalize = ::ChainActive().Tip()->GetAncestor(
        ::ChainActive().Tip()->nHeight - gArgs.GetArg("-maxreorgdepth", DEFAULT_MAX_REORG_DEPTH));

    for (uint32_t i = 0; i < DEFAULT_MAX_REORG_DEPTH; i++) {
        blockToFinalize = ::ChainActive().Next(blockToFinalize);
        block = CreateAndProcessBlock({}, p2pk_scriptPubKey);
        LOCK(cs_main);
        // Should get blocks finalizing
        BOOST_CHECK_MESSAGE(GetFinalizedBlock() == blockToFinalize,
                            "Block not finalized at height "
                                << blockToFinalize->nHeight
                                << " (tip at height "
                                << ::ChainActive().Tip()->nHeight << ")");
    }
    // Ancestor of tip is finalized.
    // Working back from chain tip depth, we create headers for fork blocks.
    // As long as we are not deeper than finalization depth, they must be
    // accepted if finalizeheaders is disabled.
    CBlockIndex *blockAtHeightOfNewHeader;
    for (uint32_t i = 0; i < DEFAULT_MAX_REORG_DEPTH; i++) {
        blockAtHeightOfNewHeader = ::ChainActive().Tip()->GetAncestor(::ChainActive().Tip()->nHeight - i);
        CreateNewHeaderAndCheckIsAccepted(config, blockAtHeightOfNewHeader->pprev);
    }

    // With header finalization disabled, a header deeper than
    // DEFAULT_MAX_REORG_DEPTH should be accepted
    {
        blockAtHeightOfNewHeader = ::ChainActive().Tip()->GetAncestor(
            ::ChainActive().Tip()->nHeight - DEFAULT_MAX_REORG_DEPTH);
        CreateNewHeaderAndCheckIsAccepted(config, blockAtHeightOfNewHeader->pprev);
    }

    // And another deeper one is accepted too.
    {
        blockAtHeightOfNewHeader = ::ChainActive().Tip()->GetAncestor(
            ::ChainActive().Tip()->nHeight - DEFAULT_MAX_REORG_DEPTH - 1);
        CreateNewHeaderAndCheckIsAccepted(config, blockAtHeightOfNewHeader->pprev);
    }
    gArgs.ClearArg("-finalizationdelay");
    gArgs.ClearArg("-finalizeheaders");
    gArgs.ClearArg("-maxreorgdepth");
} // headerFinalizationDisabledOne

BOOST_AUTO_TEST_CASE(headerFinalizationDisabledZero) {
    // Disable the finalization of headers
    gArgs.ForceSetArg("-finalizeheaders", std::string("0"));
    // 0 also finalizes block below tip
    gArgs.ForceSetArg("-maxreorgdepth", std::string("0"));

    const Config &config = GetConfig();
    CScript p2pk_scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    CBlock block;

    // Ancestor of tip is finalized.
    // Working back from chain tip depth to genesis, we create headers for fork
    // blocks. They must all be accepted if finalizeheaders is disabled. (no
    // checkpoints on this regtest chain)
    CBlockIndex *blockAtHeightOfNewHeader;
    auto tipHeight = ::ChainActive().Tip()->nHeight;
    for (auto i = 0; i < tipHeight; i++) {
        blockAtHeightOfNewHeader = ::ChainActive().Tip()->GetAncestor(::ChainActive().Tip()->nHeight - i);
        CreateNewHeaderAndCheckIsAccepted(config, blockAtHeightOfNewHeader->pprev);
    }
    gArgs.ClearArg("-maxreorgdepth");
    gArgs.ClearArg("-finalizeheaders");

} // headerFinalizationDisabledZero

BOOST_AUTO_TEST_SUITE_END()
