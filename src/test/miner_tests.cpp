// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2023 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <miner.h>

#include <chain.h>
#include <chainparams.h>
#include <coins.h>
#include <config.h>
#include <consensus/consensus.h>
#include <consensus/merkle.h>
#include <consensus/tx_verify.h>
#include <consensus/validation.h>
#include <policy/policy.h>
#include <pow.h>
#include <pubkey.h>
#include <script/standard.h>
#include <txmempool.h>
#include <uint256.h>
#include <util/strencodings.h>
#include <util/system.h>
#include <validation.h>

#include <test/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <memory>
#include "compat/optional.h"

// This suite runs on REGTEST, not mainnet, and that is load-bearing.
//
// It needs 110 blocks before it can spend a coinbase, and it used to get them
// from a hardcoded {extranonce, nonce} table mined against upstream's chain.
// Our genesis differs, so every hashPrevBlock differs, so none of those nonces
// satisfied proof of work any more: the blocks were rejected, and the later
// bad-txns-inputs-missingorspent failures were fallout from spending coinbases
// of blocks that were never accepted. Block assembly was never the problem.
//
// Regenerating the table for mainnet is not possible. Our ASERT anchor puts
// mainnet at roughly 70,000 difficulty from block 2 — about 3e14 hashes per
// block against difficulty-1's 4.3e9 — so 110 blocks is on the order of 3e16
// hashes. Upstream could precompute theirs only because BCH mainnet began at
// difficulty 1. The same choice that stops BFX being trivially mineable stops
// the table being regenerable.
//
// On regtest powLimit is 7fffffff… and fPowNoRetargeting is set, so a valid
// nonce is found in about one attempt and the blocks are ground at run time
// instead. Two further regtest properties matter here: the subsidy halving
// interval is 150, so all 110 blocks pay 50 COIN and BLOCKSUBSIDY below stays
// right; and BIP34Height is 100000000, so the deliberately non-BIP34 coinbase
// scriptSig this test builds is not rejected for its height encoding, which it
// would be on mainnet where BIP34 is active from height 0.
struct RegtestingSetup : public TestingSetup {
    RegtestingSetup() : TestingSetup(CBaseChainParams::REGTEST) {}
};

BOOST_FIXTURE_TEST_SUITE(miner_tests, RegtestingSetup)

static CFeeRate blockMinFeeRate = CFeeRate(DEFAULT_BLOCK_MIN_TX_FEE_PER_KB);

// The scriptSig used to spend the coinbase outputs this suite mines, which have
// an empty scriptPubKey.
//
// It has to be push-only and leave exactly one truthy stack item, because
// Magnetic Anomaly is active from height 0 on our chains and that makes
// SCRIPT_VERIFY_SIGPUSHONLY and SCRIPT_VERIFY_CLEANSTACK consensus rules. A bare
// OP_1 satisfies both and is what this test used to use — but the resulting
// transaction serialises to 62 bytes, and Upgrade9 sets a 65-byte floor, so
// ContextualCheckTransaction rejects it as bad-txns-undersize. Pushing eight
// bytes instead clears the floor while keeping the script trivially valid.
//
// None of this is fork-specific divergence. Both rules are ordinary Bitcoin Cash
// consensus; the test simply predates them and was never updated, which is why
// it could only ever have run on a chain that had not activated them.
static CScript MinSizeSpendSig() {
    return CScript() << std::vector<uint8_t>(8, 0x01);
}

static BlockAssembler AssemblerForTest(const Config &config, const CTxMemPool &mempool) {
    BlockAssembler::Options options;
    options.blockMinFeeRate = blockMinFeeRate;
    return BlockAssembler(config, mempool, options);
}

// How many blocks to mine before the test can spend a coinbase. COINBASE_MATURITY
// is 100, and the test spends the first few coinbases, so 110 leaves headroom.
// This replaces a 110-entry table of {extranonce, nonce} pairs; the nonces are
// now ground at run time (see the suite comment) and the extranonce is simply
// the loop index, which is all it ever needed to be — its only job is to keep
// each coinbase distinct.
static constexpr size_t NUM_BLOCKS_TO_MINE = 110;


using CBlockIndexPtr = std::unique_ptr<CBlockIndex>;

static CBlockIndexPtr CreateBlockIndex(int nHeight) {
    CBlockIndexPtr index(new CBlockIndex);
    index->nHeight = nHeight;
    index->pprev = ::ChainActive().Tip();
    return index;
}

static bool TestSequenceLocks(const CTransaction &tx, int flags)
    EXCLUSIVE_LOCKS_REQUIRED(cs_main) {
    LOCK(::g_mempool.cs);
    return CheckSequenceLocks(::g_mempool, tx, flags);
}

// Test suite for feerate transaction selection.
// Implemented as an additional function, rather than a separate test case, to
// allow reusing the blockchain created in CreateNewBlock_validity.
static void TestPackageSelection(const Config &config,
                                 const CScript &scriptPubKey,
                                 const std::vector<CTransactionRef> &txFirst)
    EXCLUSIVE_LOCKS_REQUIRED(cs_main, ::g_mempool.cs) {
    // Test the ancestor feerate transaction selection.
    TestMemPoolEntryHelper entry;

    // Test that a medium fee transaction will be selected before a higher fee
    // transaction when the high-fee tx has a low fee parent.
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig = MinSizeSpendSig();
    tx.vin[0].prevout = COutPoint(txFirst[0]->GetId(), 0);
    tx.vout.resize(1);
    tx.vout[0].nValue = int64_t(5000000000LL - 1000) * SATOSHI;
    // This tx has a low fee: 1000 satoshis.
    // Save this txid for later use.
    TxId parentTxId = tx.GetId();
    g_mempool.addUnchecked(entry.Fee(1000 * SATOSHI)
                               .Time(GetTime())
                               .SpendsCoinbase(true)
                               .FromTx(tx));

    // This tx has a medium fee: 10000 satoshis.
    tx.vin[0].prevout = COutPoint(txFirst[1]->GetId(), 0);
    tx.vout[0].nValue = int64_t(5000000000LL - 10000) * SATOSHI;
    TxId mediumFeeTxId = tx.GetId();
    g_mempool.addUnchecked(entry.Fee(10000 * SATOSHI)
                               .Time(GetTime())
                               .SpendsCoinbase(true)
                               .FromTx(tx));

    // This tx has a high fee, but depends on the first transaction.
    tx.vin[0].prevout = COutPoint(parentTxId, 0);
    // 50k satoshi fee.
    tx.vout[0].nValue = int64_t(5000000000LL - 1000 - 50000) * SATOSHI;
    TxId highFeeTxId = tx.GetId();
    g_mempool.addUnchecked(entry.Fee(50000 * SATOSHI)
                               .Time(GetTime())
                               .SpendsCoinbase(false)
                               .FromTx(tx));

    std::unique_ptr<CBlockTemplate> pblocktemplate =
        AssemblerForTest(config, g_mempool).CreateNewBlock(scriptPubKey);


    // KNOWN FAILURE on BitFinite — the last thing keeping this suite excluded.
    //
    // These three assertions are upstream's, unchanged, and they expect the
    // medium-fee transaction to be mined ahead of the high-fee one whose parent
    // pays a low fee. What the assembler actually produces here is
    // parent, high, medium — the package first.
    //
    // Measured: all three transactions serialise to 69 bytes, so the package
    // scores (1000 + 50000) / 138 = 369 sat/byte against medium's 10000 / 69 =
    // 145. Ancestor-feerate selection prefers the package, and no choice of
    // equal-sized transactions can make 51000/2s smaller than 10000/s. The
    // expectation cannot hold as written; the stated intent needs a parent that
    // is genuinely low FEERATE, meaning a physically larger transaction, not
    // merely a low absolute fee.
    //
    // Deliberately NOT "fixed" by editing these expected values. Rewriting an
    // expectation to match whatever our code emits would assert only that our
    // code does what our code does, and would give that a test's authority. The
    // honest fix is to change the INPUTS so the scenario expresses its intent,
    // and that wants its own reviewed commit with the fee arithmetic derived
    // first.
    BOOST_CHECK(pblocktemplate->block.vtx[1]->GetId() == mediumFeeTxId);
    BOOST_CHECK(pblocktemplate->block.vtx[2]->GetId() == parentTxId);
    BOOST_CHECK(pblocktemplate->block.vtx[3]->GetId() == highFeeTxId);

    // Test that a tranactions with ancestor below the block min tx fee doesn't get included
    tx.vin[0].prevout = COutPoint(highFeeTxId, 0);
    // 0 fee.
    tx.vout[0].nValue = int64_t(5000000000LL - 1000 - 50000) * SATOSHI;
    TxId freeTxId = tx.GetId();
    g_mempool.addUnchecked(entry.Fee(Amount::zero()).FromTx(tx));

    // Add a child transaction with high fee.
    Amount feeToUse = 50000 * SATOSHI;

    tx.vin[0].prevout = COutPoint(freeTxId, 0);
    tx.vout[0].nValue =
        int64_t(5000000000LL - 1000 - 50000) * SATOSHI - feeToUse;
    TxId highFeeDecendantTxId = tx.GetId();
    g_mempool.addUnchecked(entry.Fee(feeToUse).FromTx(tx));
    pblocktemplate =
        AssemblerForTest(config, g_mempool).CreateNewBlock(scriptPubKey);

    // Verify that the free tx and its high fee descendant tx didn't get selected.
    for (const auto &txn : pblocktemplate->block.vtx) {
        BOOST_CHECK(txn->GetId() != freeTxId);
        BOOST_CHECK(txn->GetId() != highFeeDecendantTxId);
    }
}

static void TestCoinbaseMessageEB(uint64_t eb, const std::string &cbmsg) {
    GlobalConfig config;
    config.SetConfiguredMaxBlockSize(eb);

    CScript scriptPubKey =
        CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909"
                              "a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112"
                              "de5c384df7ba0b8d578a4c702b6bf11d5f")
                  << OP_CHECKSIG;

    const CBlockIndex *pindexMinedTip;
    std::unique_ptr<CBlockTemplate> pblocktemplate =
        BlockAssembler(config, g_mempool).CreateNewBlock(scriptPubKey, 0, true, &pindexMinedTip);

    CBlock *pblock = &pblocktemplate->block;

    // IncrementExtraNonce creates a valid coinbase and merkleRoot
    unsigned int extraNonce = 0;
    IncrementExtraNonce(pblock, pindexMinedTip, config, extraNonce);
    unsigned int nHeight = pindexMinedTip->nHeight + 1;
    std::vector<uint8_t> vec(cbmsg.begin(), cbmsg.end());
    BOOST_CHECK(pblock->vtx[0]->vin[0].scriptSig ==
                ((CScript() << ScriptInt::fromIntUnchecked(nHeight) << CScriptNum::fromIntUnchecked(extraNonce) << vec) +
                 COINBASE_FLAGS));
}

// Coinbase scriptSig has to contains the correct EB value
// converted to MB, rounded down to the first decimal
BOOST_AUTO_TEST_CASE(CheckCoinbase_EB) {
    TestCoinbaseMessageEB(1000001, "/EB1.0/");
    TestCoinbaseMessageEB(2000000, "/EB2.0/");
    TestCoinbaseMessageEB(8000000, "/EB8.0/");
    TestCoinbaseMessageEB(8320000, "/EB8.3/");
}

// NOTE: These tests rely on CreateNewBlock doing its own self-validation!
BOOST_AUTO_TEST_CASE(CreateNewBlock_validity) {
    // Note that by default, these tests run with size accounting enabled.
    GlobalConfig config;
    const CChainParams &chainparams = config.GetChainParams();
    CScript scriptPubKey =
        CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909"
                              "a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112"
                              "de5c384df7ba0b8d578a4c702b6bf11d5f")
                  << OP_CHECKSIG;
    std::unique_ptr<CBlockTemplate> pblocktemplate;
    CMutableTransaction tx;
    CScript script;
    TestMemPoolEntryHelper entry;
    entry.nFee = 11 * SATOSHI;

    fCheckpointsEnabled = false;

    // Simple block creation, nothing special yet:
    BOOST_CHECK(pblocktemplate = AssemblerForTest(config, g_mempool)
                                     .CreateNewBlock(scriptPubKey));

    // We can't make transactions until we have inputs.
    // Therefore, load 100 blocks :)
    int baseheight = 0;
    std::vector<CTransactionRef> txFirst;
    for (size_t i = 0; i < NUM_BLOCKS_TO_MINE; ++i) {
        // pointer for convenience.
        CBlock *pblock = &pblocktemplate->block;
        {
            LOCK(cs_main);
            pblock->nVersion = 1;
            pblock->nTime = ::ChainActive().Tip()->GetMedianTimePast() + 1;
            CMutableTransaction txCoinbase(*pblock->vtx[0]);
            txCoinbase.nVersion = 1;
            txCoinbase.vin[0].scriptSig = CScript();
            txCoinbase.vin[0].scriptSig.push_back(uint8_t(i));
            txCoinbase.vin[0].scriptSig.push_back(::ChainActive().Height());
            // Pad the coinbase up to the minimum transaction size.
            //
            // Without this the block is rejected with bad-txns-undersize, and
            // that — not the proof of work — is what actually stopped this suite
            // running. The coinbase built here is about 62 bytes serialised,
            // while ContextualCheckTransaction enforces a floor of 65 bytes once
            // Upgrade9 is active and 100 before it. Both are active from height 0
            // on our chains (magneticAnomalyHeight is 0), so pad past the larger
            // of the two and the rule is satisfied either way.
            //
            // This is not a workaround. A real coinbase carries an extranonce and
            // a miner tag and is comfortably over 100 bytes; a two-byte scriptSig
            // is the unrealistic thing here. The scriptSig may legally be 2 to 100
            // bytes, so padding to 64 stays well inside that.
            while (txCoinbase.vin[0].scriptSig.size() < 64) {
                txCoinbase.vin[0].scriptSig.push_back(uint8_t(0x00));
            }
            txCoinbase.vout.resize(1);
            txCoinbase.vout[0].scriptPubKey = CScript();
            pblock->vtx[0] = MakeTransactionRef(std::move(txCoinbase));
            if (txFirst.size() == 0) {
                baseheight = ::ChainActive().Height();
            }
            if (txFirst.size() < 4) {
                txFirst.push_back(pblock->vtx[0]);
            }
            pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
            // Grind. On regtest the target is 7fffffff…, so this almost always
            // succeeds on the first nonce; the loop is here for correctness,
            // not because it is expected to spin.
            pblock->nNonce = 0;
            while (!CheckProofOfWork(pblock->GetHash(), pblock->nBits,
                                     chainparams.GetConsensus())) {
                ++pblock->nNonce;
            }
        }
        std::shared_ptr<const CBlock> shared_pblock =
            std::make_shared<const CBlock>(*pblock);
        BOOST_CHECK(ProcessNewBlock(config, shared_pblock, true, nullptr));
        pblock->hashPrevBlock = pblock->GetHash();
    }

    LOCK(cs_main);
    LOCK(::g_mempool.cs);

    // Just to make sure we can still make simple blocks.
    BOOST_CHECK(pblocktemplate = AssemblerForTest(config, g_mempool)
                                     .CreateNewBlock(scriptPubKey));

    const Amount BLOCKSUBSIDY = 50 * COIN;
    const Amount LOWFEE = CENT;
    const Amount HIGHFEE = COIN;
    const Amount HIGHERFEE = 4 * COIN;

    // block size > limit
    //
    // The bulk used to live in the scriptSig: 18 * (520-byte push + OP_DROP)
    // followed by OP_1, about 9433 bytes. That construction predates the 2018
    // Magnetic Anomaly rules and cannot be used on this chain. Magnetic Anomaly
    // is active from height 0 here, which makes SCRIPT_VERIFY_SIGPUSHONLY and
    // SCRIPT_VERIFY_CLEANSTACK consensus (validation.cpp GetBlockScriptFlags),
    // so a scriptSig may contain nothing but pushes and must leave exactly one
    // stack item. OP_DROP breaks the first rule and nineteen pushes break the
    // second; the block was rejected with blk-bad-inputs.
    //
    // The padding therefore moves into an unspendable output, which no script
    // rule constrains. The transaction stays the same order of magnitude, which
    // is all this section needs — its job is to put enough bytes in the mempool
    // that the assembler has to make a size decision, not to exercise scripts.
    // The spending input uses the same push-only form as every other case below.
    tx.vin.resize(1);
    tx.vin[0].scriptSig = MinSizeSpendSig();
    tx.vin[0].prevout = COutPoint(txFirst[0]->GetId(), 0);
    tx.vout.resize(2);
    tx.vout[0].nValue = BLOCKSUBSIDY;
    // ~9400 bytes of padding, carried in a provably-unspendable output so it
    // costs nothing and is never redeemed by the chained transactions below.
    tx.vout[1].nValue = Amount::zero();
    tx.vout[1].scriptPubKey = CScript() << OP_RETURN;
    {
        const std::vector<uint8_t> vchData(520);
        for (unsigned int i = 0; i < 18; ++i) {
            tx.vout[1].scriptPubKey << vchData;
        }
    }
    for (unsigned int i = 0; i < 128; ++i) {
        tx.vout[0].nValue -= LOWFEE;
        const TxId txid = tx.GetId();
        // Only first tx spends coinbase.
        bool spendsCoinbase = i == 0;
        g_mempool.addUnchecked(entry.Fee(LOWFEE)
                                   .Time(GetTime())
                                   .SpendsCoinbase(spendsCoinbase)
                                   .FromTx(tx));
        tx.vin[0].prevout = COutPoint(txid, 0);
    }

    BOOST_CHECK(pblocktemplate = AssemblerForTest(config, g_mempool)
                                     .CreateNewBlock(scriptPubKey));
    g_mempool.clear();

    // Drop the padding output again, so the cases below build the same small
    // transactions they always did rather than inheriting 9 kB of OP_RETURN.
    tx.vout.resize(1);

    // Orphan in mempool, template creation fails.
    g_mempool.addUnchecked(entry.Fee(LOWFEE).Time(GetTime()).FromTx(tx));
    BOOST_CHECK_EXCEPTION(
        AssemblerForTest(config, g_mempool).CreateNewBlock(scriptPubKey),
        std::runtime_error, HasReason("bad-txns-inputs-missingorspent"));
    g_mempool.clear();

    // Child with higher priority than parent.
    tx.vin[0].scriptSig = MinSizeSpendSig();
    tx.vin[0].prevout = COutPoint(txFirst[1]->GetId(), 0);
    tx.vout[0].nValue = BLOCKSUBSIDY - HIGHFEE;
    TxId txid = tx.GetId();
    g_mempool.addUnchecked(
        entry.Fee(HIGHFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    tx.vin[0].prevout = COutPoint(txid, 0);
    tx.vin.resize(2);
    tx.vin[1].scriptSig = MinSizeSpendSig();
    tx.vin[1].prevout = COutPoint(txFirst[0]->GetId(), 0);
    // First txn output + fresh coinbase - new txn fee.
    tx.vout[0].nValue = tx.vout[0].nValue + BLOCKSUBSIDY - HIGHERFEE;
    txid = tx.GetId();
    g_mempool.addUnchecked(
        entry.Fee(HIGHERFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    BOOST_CHECK(pblocktemplate = AssemblerForTest(config, g_mempool)
                                     .CreateNewBlock(scriptPubKey));
    g_mempool.clear();

    // Coinbase in mempool, template creation fails.
    tx.vin.resize(1);
    tx.vin[0].prevout = COutPoint();
    // Padded past the 65-byte floor like every other transaction here. Without
    // it the transaction is dropped as bad-txns-undersize during assembly, so
    // the bad-tx-coinbase check below never runs and no exception is raised —
    // the test would silently stop testing the thing it names.
    tx.vin[0].scriptSig = CScript() << OP_0 << OP_1
                                    << std::vector<uint8_t>(8, 0x01);
    tx.vout[0].nValue = Amount::zero();
    txid = tx.GetId();
    // Give it a fee so it'll get mined.
    g_mempool.addUnchecked(
        entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(false).FromTx(tx));
    // Should throw bad-tx-coinbase
    BOOST_CHECK_EXCEPTION(
        AssemblerForTest(config, g_mempool).CreateNewBlock(scriptPubKey),
        std::runtime_error, HasReason("bad-tx-coinbase"));
    g_mempool.clear();

    // Double spend txn pair in mempool, template creation fails.
    tx.vin[0].prevout = COutPoint(txFirst[0]->GetId(), 0);
    tx.vin[0].scriptSig = MinSizeSpendSig();
    tx.vout[0].nValue = BLOCKSUBSIDY - HIGHFEE;
    tx.vout[0].scriptPubKey = MinSizeSpendSig();
    txid = tx.GetId();
    g_mempool.addUnchecked(
        entry.Fee(HIGHFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    tx.vout[0].scriptPubKey = CScript() << OP_2;
    txid = tx.GetId();
    g_mempool.addUnchecked(
        entry.Fee(HIGHFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    BOOST_CHECK_EXCEPTION(
        AssemblerForTest(config, g_mempool).CreateNewBlock(scriptPubKey),
        std::runtime_error, HasReason("bad-txns-inputs-missingorspent"));
    g_mempool.clear();

    // Subsidy changing.
    int nHeight = ::ChainActive().Height();
    // Create an actual 209999-long block chain (without valid blocks).
    while (::ChainActive().Tip()->nHeight < 209999) {
        CBlockIndex *prev = ::ChainActive().Tip();
        CBlockIndex *next = new CBlockIndex();
        next->phashBlock = new BlockHash(InsecureRand256());
        pcoinsTip->SetBestBlock(next->GetBlockHash());
        next->pprev = prev;
        next->nHeight = prev->nHeight + 1;
        next->BuildSkip();
        ::ChainActive().SetTip(next);
    }
    BOOST_CHECK(pblocktemplate = AssemblerForTest(config, g_mempool)
                                     .CreateNewBlock(scriptPubKey));
    // Extend to a 210000-long block chain.
    while (::ChainActive().Tip()->nHeight < 210000) {
        CBlockIndex *prev = ::ChainActive().Tip();
        CBlockIndex *next = new CBlockIndex();
        next->phashBlock = new BlockHash(InsecureRand256());
        pcoinsTip->SetBestBlock(next->GetBlockHash());
        next->pprev = prev;
        next->nHeight = prev->nHeight + 1;
        next->BuildSkip();
        ::ChainActive().SetTip(next);
    }

    BOOST_CHECK(pblocktemplate = AssemblerForTest(config, g_mempool)
                                     .CreateNewBlock(scriptPubKey));

    // Invalid p2sh txn in mempool, template creation fails
    tx.vin[0].prevout = COutPoint(txFirst[0]->GetId(), 0);
    tx.vin[0].scriptSig = MinSizeSpendSig();
    tx.vout[0].nValue = BLOCKSUBSIDY - LOWFEE;
    script = CScript() << OP_0;
    tx.vout[0].scriptPubKey = GetScriptForDestination(ScriptID(script, false /* no p2sh_32 */));
    txid = tx.GetId();
    g_mempool.addUnchecked(
        entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    tx.vin[0].prevout = COutPoint(txid, 0);
    tx.vin[0].scriptSig = CScript()
                          << std::vector<uint8_t>(script.begin(), script.end());
    tx.vout[0].nValue -= LOWFEE;
    txid = tx.GetId();
    g_mempool.addUnchecked(
        entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(false).FromTx(tx));
    // Should throw blk-bad-inputs
    BOOST_CHECK_EXCEPTION(
        AssemblerForTest(config, g_mempool).CreateNewBlock(scriptPubKey),
        std::runtime_error, HasReason("blk-bad-inputs"));
    g_mempool.clear();

    // Delete the dummy blocks again.
    while (::ChainActive().Tip()->nHeight > nHeight) {
        CBlockIndex *del = ::ChainActive().Tip();
        ::ChainActive().SetTip(del->pprev);
        pcoinsTip->SetBestBlock(del->pprev->GetBlockHash());
        delete del->phashBlock;
        delete del;
    }

    // non-final txs in mempool
    SetMockTime(::ChainActive().Tip()->GetMedianTimePast() + 1);
    uint32_t flags = LOCKTIME_VERIFY_SEQUENCE | LOCKTIME_MEDIAN_TIME_PAST;
    // height map
    std::vector<int> prevheights;

    // Relative height locked.
    tx.nVersion = 2;
    tx.vin.resize(1);
    prevheights.resize(1);
    // Only 1 transaction.
    tx.vin[0].prevout = COutPoint(txFirst[0]->GetId(), 0);
    tx.vin[0].scriptSig = MinSizeSpendSig();
    // txFirst[0] is the 2nd block
    tx.vin[0].nSequence = ::ChainActive().Tip()->nHeight + 1;
    prevheights[0] = baseheight + 1;
    tx.vout.resize(1);
    tx.vout[0].nValue = BLOCKSUBSIDY - HIGHFEE;
    tx.vout[0].scriptPubKey = MinSizeSpendSig();
    tx.nLockTime = 0;
    txid = tx.GetId();
    g_mempool.addUnchecked(
        entry.Fee(HIGHFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));

    const Consensus::Params &params = chainparams.GetConsensus();

    {
        // Locktime passes.
        CValidationState state;
        BOOST_CHECK(ContextualCheckTransactionForCurrentBlock(
            params, CTransaction(tx), state, flags));
    }

    // Sequence locks fail.
    BOOST_CHECK(!TestSequenceLocks(CTransaction(tx), flags));
    // Sequence locks pass on 2nd block.
    BOOST_CHECK(
        SequenceLocks(CTransaction(tx), flags, &prevheights,
                      *CreateBlockIndex(::ChainActive().Tip()->nHeight + 2)));

    // Relative time locked.
    tx.vin[0].prevout = COutPoint(txFirst[1]->GetId(), 0);
    // txFirst[1] is the 3rd block.
    tx.vin[0].nSequence = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG |
                          (((::ChainActive().Tip()->GetMedianTimePast() + 1 -
                             ::ChainActive()[1]->GetMedianTimePast()) >>
                            CTxIn::SEQUENCE_LOCKTIME_GRANULARITY) +
                           1);
    prevheights[0] = baseheight + 2;
    txid = tx.GetId();
    g_mempool.addUnchecked(entry.Time(GetTime()).FromTx(tx));

    {
        // Locktime passes.
        CValidationState state;
        BOOST_CHECK(ContextualCheckTransactionForCurrentBlock(
            params, CTransaction(tx), state, flags));
    }

    // Sequence locks fail.
    BOOST_CHECK(!TestSequenceLocks(CTransaction(tx), flags));

    for (int i = 0; i < CBlockIndex::nMedianTimeSpan; i++) {
        // Trick the MedianTimePast.
        ::ChainActive()
            .Tip()
            ->GetAncestor(::ChainActive().Tip()->nHeight - i)
            ->nTime += 512;
    }
    // Sequence locks pass 512 seconds later.
    BOOST_CHECK(
        SequenceLocks(CTransaction(tx), flags, &prevheights,
                      *CreateBlockIndex(::ChainActive().Tip()->nHeight + 1)));
    for (int i = 0; i < CBlockIndex::nMedianTimeSpan; i++) {
        // Undo tricked MTP.
        ::ChainActive()
            .Tip()
            ->GetAncestor(::ChainActive().Tip()->nHeight - i)
            ->nTime -= 512;
    }

    // Absolute height locked.
    tx.vin[0].prevout = COutPoint(txFirst[2]->GetId(), 0);
    tx.vin[0].nSequence = CTxIn::SEQUENCE_FINAL - 1;
    prevheights[0] = baseheight + 3;
    tx.nLockTime = ::ChainActive().Tip()->nHeight + 1;
    txid = tx.GetId();
    g_mempool.addUnchecked(entry.Time(GetTime()).FromTx(tx));

    {
        // Locktime fails.
        CValidationState state;
        BOOST_CHECK(!ContextualCheckTransactionForCurrentBlock(
            params, CTransaction(tx), state, flags));
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-txns-nonfinal");
    }

    // Sequence locks pass.
    BOOST_CHECK(TestSequenceLocks(CTransaction(tx), flags));

    {
        // Locktime passes on 2nd block.
        CValidationState state;
        int64_t nMedianTimePast = ::ChainActive().Tip()->GetMedianTimePast();
        BOOST_CHECK(ContextualCheckTransaction(
            params, CTransaction(tx), state, ::ChainActive().Tip()->nHeight + 2,
            nMedianTimePast, nMedianTimePast));
    }

    // Absolute time locked.
    tx.vin[0].prevout = COutPoint(txFirst[3]->GetId(), 0);
    tx.nLockTime = ::ChainActive().Tip()->GetMedianTimePast();
    prevheights.resize(1);
    prevheights[0] = baseheight + 4;
    txid = tx.GetId();
    g_mempool.addUnchecked(entry.Time(GetTime()).FromTx(tx));

    {
        // Locktime fails.
        CValidationState state;
        BOOST_CHECK(!ContextualCheckTransactionForCurrentBlock(
            params, CTransaction(tx), state, flags));
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-txns-nonfinal");
    }

    // Sequence locks pass.
    BOOST_CHECK(TestSequenceLocks(CTransaction(tx), flags));

    {
        // Locktime passes 1 second later.
        CValidationState state;
        int64_t nMedianTimePast =
            ::ChainActive().Tip()->GetMedianTimePast() + 1;
        BOOST_CHECK(ContextualCheckTransaction(
            params, CTransaction(tx), state, ::ChainActive().Tip()->nHeight + 1,
            nMedianTimePast, nMedianTimePast));
    }

    // mempool-dependent transactions (not added)
    tx.vin[0].prevout = COutPoint(txid, 0);
    prevheights[0] = ::ChainActive().Tip()->nHeight + 1;
    tx.nLockTime = 0;
    tx.vin[0].nSequence = 0;

    {
        // Locktime passes.
        CValidationState state;
        BOOST_CHECK(ContextualCheckTransactionForCurrentBlock(
            params, CTransaction(tx), state, flags));
    }

    // Sequence locks pass.
    BOOST_CHECK(TestSequenceLocks(CTransaction(tx), flags));
    tx.vin[0].nSequence = 1;
    // Sequence locks fail.
    BOOST_CHECK(!TestSequenceLocks(CTransaction(tx), flags));
    tx.vin[0].nSequence = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG;
    // Sequence locks pass.
    BOOST_CHECK(TestSequenceLocks(CTransaction(tx), flags));
    tx.vin[0].nSequence = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG | 1;
    // Sequence locks fail.
    BOOST_CHECK(!TestSequenceLocks(CTransaction(tx), flags));

    pblocktemplate =
        AssemblerForTest(config, g_mempool).CreateNewBlock(scriptPubKey);
    BOOST_CHECK(pblocktemplate);

    // None of the of the absolute height/time locked tx should have made it
    // into the template because we still check IsFinalTx in CreateNewBlock, but
    // relative locked txs will if inconsistently added to g_mempool. For now
    // these will still generate a valid template until BIP68 soft fork.
    BOOST_CHECK_EQUAL(pblocktemplate->block.vtx.size(), 3UL);
    // However if we advance height by 1 and time by 512, all of them should be
    // mined.
    for (int i = 0; i < CBlockIndex::nMedianTimeSpan; i++) {
        // Trick the MedianTimePast.
        ::ChainActive()
            .Tip()
            ->GetAncestor(::ChainActive().Tip()->nHeight - i)
            ->nTime += 512;
    }
    ::ChainActive().Tip()->nHeight++;
    SetMockTime(::ChainActive().Tip()->GetMedianTimePast() + 1);

    BOOST_CHECK(pblocktemplate = AssemblerForTest(config, g_mempool)
                                     .CreateNewBlock(scriptPubKey));
    BOOST_CHECK_EQUAL(pblocktemplate->block.vtx.size(), 5UL);

    ::ChainActive().Tip()->nHeight--;
    SetMockTime(0);
    g_mempool.clear();

    TestPackageSelection(config, scriptPubKey, txFirst);

    fCheckpointsEnabled = true;
}

static void CallCreateNewBlockOnceToFullyInit(BlockAssembler &ba) {
    ba.CreateNewBlock(CScript() << OP_TRUE, 1e-3);
}

static void CheckBlockMaxSize(Config &config, uint64_t size, uint64_t expected) {
    BOOST_CHECK(config.SetGeneratedBlockSizeBytes(size));

    BlockAssembler ba(config, g_mempool);
    BOOST_CHECK_EQUAL(ba.GetMaxGeneratedBlockSize(), 0); // freshly-constructed class always has 0 here
    CallCreateNewBlockOnceToFullyInit(ba); // must call CreateNewBlock() to have `config` options "take effect"
    BOOST_CHECK_EQUAL(ba.GetMaxGeneratedBlockSize(), expected);
}

static void CheckBlockMaxSizePercent(Config &config, std::optional<double> optPercent, uint64_t expected) {
    if (optPercent) {
        BOOST_CHECK(config.SetGeneratedBlockSizePercent(*optPercent));
    }

    BlockAssembler ba(config, g_mempool);
    BOOST_CHECK_EQUAL(ba.GetMaxGeneratedBlockSize(), 0); // freshly-constructed class always has 0 here
    CallCreateNewBlockOnceToFullyInit(ba); // must call CreateNewBlock() to have `config` options "take effect"
    BOOST_CHECK_EQUAL(ba.GetMaxGeneratedBlockSize(), expected);
}

BOOST_AUTO_TEST_CASE(BlockAssembler_construction) {
    GlobalConfig config;

    // check that generated block size can never exceed conf. max block size
    {
        const auto cmbs = config.GetConfiguredMaxBlockSize();
        BOOST_CHECK_LE(config.GetGeneratedBlockSize(cmbs), cmbs);
        const size_t prevVal = config.GetGeneratedBlockSize(cmbs),
                     badVal = cmbs + 1;
        BOOST_CHECK_NE(prevVal, badVal); // ensure not equal for thoroughness
        // try and set generated block size beyond the conf. max block size (should fail)
        BOOST_CHECK(!config.SetGeneratedBlockSizeBytes(badVal));
        // check that the failure really did not set the value
        BOOST_CHECK_EQUAL(config.GetGeneratedBlockSize(cmbs), prevVal);
        // check bad percentages
        BOOST_CHECK(!config.SetGeneratedBlockSizePercent(101.0));
        BOOST_CHECK(!config.SetGeneratedBlockSizePercent(100.1));
        BOOST_CHECK(!config.SetGeneratedBlockSizePercent(-0.001));
        // check that the failure really did not set the value
        BOOST_CHECK_EQUAL(config.GetGeneratedBlockSize(cmbs), prevVal);
    }

    // We are working on a fake chain and need to protect ourselves.
    LOCK(cs_main);

    // Test around historical 1MB (plus one byte because that's mandatory)
    config.SetConfiguredMaxBlockSize(ONE_MEGABYTE + 1);
    CheckBlockMaxSize(config, 0, 1000);
    CheckBlockMaxSize(config, 1000, 1000);
    CheckBlockMaxSize(config, 1001, 1001);
    CheckBlockMaxSize(config, 12345, 12345);

    CheckBlockMaxSize(config, ONE_MEGABYTE - 1001, ONE_MEGABYTE - 1001);
    CheckBlockMaxSize(config, ONE_MEGABYTE - 1000, ONE_MEGABYTE - 1000);
    CheckBlockMaxSize(config, ONE_MEGABYTE - 999, ONE_MEGABYTE - 999);
    CheckBlockMaxSize(config, ONE_MEGABYTE, ONE_MEGABYTE - 999);

    // Test percent mode
    CheckBlockMaxSizePercent(config, 100.0, ONE_MEGABYTE - 999);
    CheckBlockMaxSizePercent(config, 50.0, ONE_MEGABYTE / 2);
    CheckBlockMaxSizePercent(config, 10.0, ONE_MEGABYTE / 10);
    CheckBlockMaxSizePercent(config, 1.0, ONE_MEGABYTE / 100);
    CheckBlockMaxSizePercent(config, 0.25, ONE_MEGABYTE / 400);
    CheckBlockMaxSizePercent(config, 25.0, ONE_MEGABYTE / 4);
    // Modifying the conf. max block size should preserve the previous percentage setting (25%)
    config.SetConfiguredMaxBlockSize(2 * ONE_MEGABYTE);
    CheckBlockMaxSizePercent(config, std::nullopt, (2 * ONE_MEGABYTE) / 4);

    // Test around default cap
    config.SetConfiguredMaxBlockSize(DEFAULT_CONSENSUS_BLOCK_SIZE);

    // Now we can use the default max block size.
    CheckBlockMaxSize(config, DEFAULT_CONSENSUS_BLOCK_SIZE - 1001,
                      DEFAULT_CONSENSUS_BLOCK_SIZE - 1001);
    CheckBlockMaxSize(config, DEFAULT_CONSENSUS_BLOCK_SIZE - 1000,
                      DEFAULT_CONSENSUS_BLOCK_SIZE - 1000);
    CheckBlockMaxSize(config, DEFAULT_CONSENSUS_BLOCK_SIZE - 999,
                      DEFAULT_CONSENSUS_BLOCK_SIZE - 1000);
    CheckBlockMaxSize(config, DEFAULT_CONSENSUS_BLOCK_SIZE,
                      DEFAULT_CONSENSUS_BLOCK_SIZE - 1000);

    // Test percent mode
    CheckBlockMaxSizePercent(config, 100.0, DEFAULT_CONSENSUS_BLOCK_SIZE - 1000);
    CheckBlockMaxSizePercent(config, 50.0, DEFAULT_CONSENSUS_BLOCK_SIZE / 2);
    CheckBlockMaxSizePercent(config, 10.0, DEFAULT_CONSENSUS_BLOCK_SIZE / 10);
    CheckBlockMaxSizePercent(config, 1.0, DEFAULT_CONSENSUS_BLOCK_SIZE / 100);
    CheckBlockMaxSizePercent(config, 0.25, DEFAULT_CONSENSUS_BLOCK_SIZE / 400);
    CheckBlockMaxSizePercent(config, 25.0, DEFAULT_CONSENSUS_BLOCK_SIZE / 4);
    // Modifying the conf. max block size should preserve the previous percentage setting (25%)
    config.SetConfiguredMaxBlockSize(2 * DEFAULT_CONSENSUS_BLOCK_SIZE);
    CheckBlockMaxSizePercent(config, std::nullopt, (2 * DEFAULT_CONSENSUS_BLOCK_SIZE) / 4);

    // NB: If the generated block size parameter is not specified, the config object just defaults it to the conf. max
    // block size. But in that case the BlockAssembler ends up unconditionally reserving 1000 bytes of space for the
    // coinbase tx.
    constexpr size_t hardCodedCoinbaseReserved = 1000;
    {
        GlobalConfig freshConfig;
        BlockAssembler ba(freshConfig, g_mempool);
        BOOST_CHECK_EQUAL(ba.GetMaxGeneratedBlockSize(), 0);
        CallCreateNewBlockOnceToFullyInit(ba);
        auto cmbs = freshConfig.GetConfiguredMaxBlockSize();
        BOOST_CHECK_EQUAL(ba.GetMaxGeneratedBlockSize(), cmbs - hardCodedCoinbaseReserved);

        // next, ensure that invariants are maintained -- setting conf. max block size should pull down generatedblocksize
        const auto prevVal = freshConfig.GetGeneratedBlockSize(cmbs);
        BOOST_CHECK(freshConfig.SetConfiguredMaxBlockSize(prevVal / 2));
        cmbs = freshConfig.GetConfiguredMaxBlockSize();
        BOOST_CHECK_EQUAL(cmbs, freshConfig.GetGeneratedBlockSize(cmbs));
        BOOST_CHECK_LT(freshConfig.GetGeneratedBlockSize(cmbs), prevVal);
        BlockAssembler ba2(freshConfig, g_mempool);
        BOOST_CHECK_EQUAL(ba2.GetMaxGeneratedBlockSize(), 0);
        CallCreateNewBlockOnceToFullyInit(ba2);
        BOOST_CHECK_EQUAL(ba2.GetMaxGeneratedBlockSize(), freshConfig.GetConfiguredMaxBlockSize() - hardCodedCoinbaseReserved);
    }
}

BOOST_AUTO_TEST_CASE(TestCBlockTemplateEntry) {
    CTransactionRef txRef = MakeTransactionRef();
    CBlockTemplateEntry txEntry(txRef, 1 * SATOSHI, 10);
    BOOST_CHECK_MESSAGE(txEntry.tx == txRef, "Transactions did not match");
    BOOST_CHECK_EQUAL(txEntry.fees, 1 * SATOSHI);
    BOOST_CHECK_EQUAL(txEntry.sigChecks, 10);
}

BOOST_AUTO_TEST_SUITE_END()
