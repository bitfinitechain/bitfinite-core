// Copyright (c) 2017-2022 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
// Copyright (c) 2015 The Bitcoin Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <config.h>
#include <consensus/activation.h>
#include <pow.h>
#include <random.h>
#include <tinyformat.h>
#include <util/system.h>

#include <test/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <cmath>
#include <memory>
#include <string>

BOOST_FIXTURE_TEST_SUITE(pow_tests, TestingSetup)

/* Test calculation of next difficulty target with no constraints applying */
BOOST_AUTO_TEST_CASE(get_next_work) {
    DummyConfig config(CBaseChainParams::MAIN);

    int64_t nLastRetargetTime = 1261130161; // Block #30240
    CBlockIndex pindexLast;
    pindexLast.nHeight = 32255;
    pindexLast.nTime = 1262152739; // Block #32255
    pindexLast.nBits = 0x1d00ffff;
    BOOST_CHECK_EQUAL(
        CalculateNextWorkRequired(&pindexLast, nLastRetargetTime,
                                  config.GetChainParams().GetConsensus()),
        0x1d00d86aU);
}

/* Test the constraint on the upper bound for next work */
BOOST_AUTO_TEST_CASE(get_next_work_pow_limit) {
    DummyConfig config(CBaseChainParams::MAIN);

    int64_t nLastRetargetTime = 1231006505; // Block #0
    CBlockIndex pindexLast;
    pindexLast.nHeight = 2015;
    pindexLast.nTime = 1233061996; // Block #2015
    pindexLast.nBits = 0x1d00ffff;
    BOOST_CHECK_EQUAL(
        CalculateNextWorkRequired(&pindexLast, nLastRetargetTime,
                                  config.GetChainParams().GetConsensus()),
        0x1d00ffffU);
}

/* Test the constraint on the lower bound for actual time taken */
BOOST_AUTO_TEST_CASE(get_next_work_lower_limit_actual) {
    DummyConfig config(CBaseChainParams::MAIN);

    int64_t nLastRetargetTime = 1279008237; // Block #66528
    CBlockIndex pindexLast;
    pindexLast.nHeight = 68543;
    pindexLast.nTime = 1279297671; // Block #68543
    pindexLast.nBits = 0x1c05a3f4;
    BOOST_CHECK_EQUAL(
        CalculateNextWorkRequired(&pindexLast, nLastRetargetTime,
                                  config.GetChainParams().GetConsensus()),
        0x1c0168fdU);
}

/* Test the constraint on the upper bound for actual time taken */
BOOST_AUTO_TEST_CASE(get_next_work_upper_limit_actual) {
    DummyConfig config(CBaseChainParams::MAIN);

    int64_t nLastRetargetTime = 1263163443; // NOTE: Not an actual block time
    CBlockIndex pindexLast;
    pindexLast.nHeight = 46367;
    pindexLast.nTime = 1269211443; // Block #46367
    pindexLast.nBits = 0x1c387f6f;
    BOOST_CHECK_EQUAL(
        CalculateNextWorkRequired(&pindexLast, nLastRetargetTime,
                                  config.GetChainParams().GetConsensus()),
        0x1d00e1fdU);
}

using CBlockIndexPtr = std::unique_ptr<CBlockIndex>;
const auto MkCBlockIndexPtr = &std::make_unique<CBlockIndex>;

BOOST_AUTO_TEST_CASE(GetBlockProofEquivalentTime_test) {
    DummyConfig config(CBaseChainParams::MAIN);

    std::vector<CBlockIndexPtr> blocks(10000);
    for (int i = 0; i < 10000; i++) {
        blocks[i] = MkCBlockIndexPtr();
        blocks[i]->pprev = i ? blocks[i - 1].get() : nullptr;
        blocks[i]->nHeight = i;
        blocks[i]->nTime =
            1269211443 +
            i * config.GetChainParams().GetConsensus().nPowTargetSpacing;
        blocks[i]->nBits = 0x207fffff; /* target 0x7fffff000... */
        blocks[i]->nChainWork =
            i ? blocks[i - 1]->nChainWork + GetBlockProof(*blocks[i])
              : arith_uint256(0);
    }

    for (int j = 0; j < 1000; j++) {
        CBlockIndexPtr& p1 = blocks[InsecureRandRange(10000)];
        CBlockIndexPtr& p2 = blocks[InsecureRandRange(10000)];
        CBlockIndexPtr& p3 = blocks[InsecureRandRange(10000)];

        int64_t tdiff = GetBlockProofEquivalentTime(
            *p1, *p2, *p3, config.GetChainParams().GetConsensus());
        BOOST_CHECK_EQUAL(tdiff, p1->GetBlockTime() - p2->GetBlockTime());
    }
}

static CBlockIndexPtr GetBlockIndex(CBlockIndex *pindexPrev, int64_t nTimeInterval,
                                    uint32_t nBits) {
    CBlockIndexPtr block = MkCBlockIndexPtr();
    block->pprev = pindexPrev;
    block->nHeight = pindexPrev->nHeight + 1;
    block->nTime = pindexPrev->nTime + nTimeInterval;
    block->nBits = nBits;

    block->BuildSkip();
    block->nChainWork = pindexPrev->nChainWork + GetBlockProof(*block);
    return block;
}


double TargetFromBits(const uint32_t nBits) {
    return (nBits & 0xff'ff'ff) * pow(256, (nBits >> 24)-3);
}

// The reference floating-point ASERT, used to bound the integer implementation's
// approximation error. Upstream hardcoded 600 and 2*24*3600 here — BCH's spacing
// and half-life — so on a chain with different values it compared our integer
// result against the wrong curve entirely, and reported 50-60% error that was
// really just a parameter mismatch. Both are arguments now.
double GetASERTApproximationError(const CBlockIndex *pindexPrev,
                                  const uint32_t finalBits,
                                  const CBlockIndex *pindexAnchorBlock,
                                  const int64_t nSpacing,
                                  const int64_t nHalfLife) {
    const int64_t nHeightDiff = pindexPrev->nHeight - pindexAnchorBlock->nHeight;
    const int64_t nTimeDiff   = pindexPrev->GetBlockTime()   - pindexAnchorBlock->pprev->GetBlockTime();
    const uint32_t initialBits = pindexAnchorBlock->nBits;

    BOOST_CHECK(nHeightDiff >= 0);
    double dInitialPow = TargetFromBits(initialBits);
    double dFinalPow   = TargetFromBits(finalBits);

    double dExponent = double(nTimeDiff - (nHeightDiff+1) * nSpacing) / double(nHalfLife);
    double dTarget = dInitialPow * pow(2, dExponent);

    return (dFinalPow - dTarget) / dTarget;
}

BOOST_AUTO_TEST_CASE(asert_difficulty_test) {
    DummyConfig config(CBaseChainParams::MAIN);

    std::vector<CBlockIndexPtr> blocks(3000 + 2*24*3600);

    Consensus::Params mutableParams = config.GetChainParams().GetConsensus(); // copy params
    mutableParams.asertAnchorParams.reset();  // clear hard-coded anchor block so that we may perform these below tests
    const Consensus::Params &params = mutableParams; // take a const reference
    // Derived, not literal — see the note on GetASERTApproximationError. Upstream's
    // 600 / 1050 / 150 / 2*24*3600 encode BCH's spacing and half-life.
    const int64_t nSpacing  = params.nPowTargetSpacing;
    const int64_t nHalfLife = params.nASERTHalfLife;

    const arith_uint256 powLimit = UintToArith256(params.powLimit);
    arith_uint256 currentPow = powLimit >> 3;
    uint32_t initialBits = currentPow.GetCompact();
    double dMaxErr = 0.0001166792656486;

    // Genesis block, and parent of ASERT anchor block in this test case.
    blocks[0] = MkCBlockIndexPtr();
    blocks[0]->nHeight = 0;
    blocks[0]->nTime = 1269211443;
    // The pre-anchor block's nBits should never be used, so we set it to a nonsense value in order to
    // trigger an error if it is ever accessed
    blocks[0]->nBits = 0x0dedbeef;

    blocks[0]->nChainWork = GetBlockProof(*blocks[0]);

    // Block counter.
    size_t i = 1;

    // ASERT anchor block. We give this one a solvetime of 150 seconds to ensure that
    // the solvetime between the pre-anchor and the anchor blocks is actually used.
    blocks[1] = GetBlockIndex(blocks[0].get(), nSpacing/4, initialBits);
    // The nBits for the next block should not be equal to the anchor block's nBits
    CBlockHeader blkHeaderDummy;
    uint32_t nBits = GetNextASERTWorkRequired(blocks[i++].get(), &blkHeaderDummy, params, blocks[1].get());
    BOOST_CHECK(fabs(GetASERTApproximationError(blocks[i-1].get(), nBits, blocks[1].get(), nSpacing, nHalfLife)) < dMaxErr);
    BOOST_CHECK(nBits != initialBits);

    // If we add another block at 1050 seconds, we should return to the anchor block's nBits
    blocks[i] = GetBlockIndex(blocks[i-1].get(), nSpacing + (nSpacing*3)/4, nBits);
    nBits = GetNextASERTWorkRequired(blocks[i++].get(), &blkHeaderDummy, params, blocks[1].get());
    BOOST_CHECK(nBits == initialBits);
    BOOST_CHECK(fabs(GetASERTApproximationError(blocks[i-1].get(), nBits, blocks[1].get(), nSpacing, nHalfLife)) < dMaxErr);

    currentPow = arith_uint256().SetCompact(nBits);
    // Before we do anything else, check that timestamps *before* the anchor block work fine.
    // Jumping 2 days into the past will give a timestamp before the achnor, and should halve the target
    blocks[i] = GetBlockIndex(blocks[i-1].get(), nSpacing - nHalfLife, nBits);
    nBits = GetNextASERTWorkRequired(blocks[i++].get(), &blkHeaderDummy, params, blocks[1].get());
    currentPow = arith_uint256().SetCompact(nBits);
    // Because nBits truncates target, we don't end up with exactly 1/2 the target
    BOOST_CHECK(currentPow <= arith_uint256().SetCompact(initialBits  ) / 2);
    BOOST_CHECK(currentPow >= arith_uint256().SetCompact(initialBits-1) / 2);
    BOOST_CHECK(fabs(GetASERTApproximationError(blocks[i-1].get(), nBits, blocks[1].get(), nSpacing, nHalfLife)) < dMaxErr);

    // Jumping forward 2 days should return the target to the initial value
    blocks[i] = GetBlockIndex(blocks[i-1].get(), nSpacing + nHalfLife, nBits);
    nBits = GetNextASERTWorkRequired(blocks[i++].get(), &blkHeaderDummy, params, blocks[1].get());
    currentPow = arith_uint256().SetCompact(nBits);
    BOOST_CHECK(nBits == initialBits);
    BOOST_CHECK(fabs(GetASERTApproximationError(blocks[i-1].get(), nBits, blocks[1].get(), nSpacing, nHalfLife)) < dMaxErr);

    // Pile up some blocks every 10 mins to establish some history.
    for (; i < 150; i++) {
        blocks[i] = GetBlockIndex(blocks[i - 1].get(), nSpacing, nBits);
        BOOST_CHECK_EQUAL(blocks[i]->nBits, nBits);
    }

    nBits = GetNextASERTWorkRequired(blocks[i - 1].get(), &blkHeaderDummy, params, blocks[1].get());

    BOOST_CHECK_EQUAL(nBits, initialBits);

    // Difficulty stays the same as long as we produce a block every 10 mins.
    for (size_t j = 0; j < 10; i++, j++) {
        blocks[i] = GetBlockIndex(blocks[i - 1].get(), nSpacing, nBits);
        BOOST_CHECK_EQUAL(
            GetNextASERTWorkRequired(blocks[i].get(), &blkHeaderDummy, params, blocks[1].get()),
            nBits);
    }

    // If we add a two blocks whose solvetimes together add up to 1200s,
    // then the next block's target should be the same as the one before these blocks
    // (at this point, equal to initialBits).
    blocks[i] = GetBlockIndex(blocks[i - 1].get(), nSpacing/2, nBits);
    nBits = GetNextASERTWorkRequired(blocks[i++].get(), &blkHeaderDummy, params, blocks[1].get());
    BOOST_CHECK(fabs(GetASERTApproximationError(blocks[i-1].get(), nBits, blocks[ 1 ].get(), nSpacing, nHalfLife)) < dMaxErr);
    BOOST_CHECK(fabs(GetASERTApproximationError(blocks[i-1].get(), nBits, blocks[i-2].get(), nSpacing, nHalfLife)) < dMaxErr); // relative
    blocks[i] = GetBlockIndex(blocks[i - 1].get(), (nSpacing*3)/2, nBits);
    nBits = GetNextASERTWorkRequired(blocks[i++].get(), &blkHeaderDummy, params, blocks[1].get());
    BOOST_CHECK(fabs(GetASERTApproximationError(blocks[i-1].get(), nBits, blocks[ 1 ].get(), nSpacing, nHalfLife)) < dMaxErr); // absolute
    BOOST_CHECK(fabs(GetASERTApproximationError(blocks[i-1].get(), nBits, blocks[i-2].get(), nSpacing, nHalfLife)) < dMaxErr); // relative
    BOOST_CHECK_EQUAL(nBits, initialBits);
    BOOST_CHECK(nBits != blocks[i-1]->nBits);

    // Same in reverse - this time slower block first, followed by faster block.
    blocks[i] = GetBlockIndex(blocks[i - 1].get(), (nSpacing*3)/2, nBits);
    nBits = GetNextASERTWorkRequired(blocks[i++].get(), &blkHeaderDummy, params, blocks[1].get());
    BOOST_CHECK(fabs(GetASERTApproximationError(blocks[i-1].get(), nBits, blocks[ 1 ].get(), nSpacing, nHalfLife)) < dMaxErr); // absolute
    BOOST_CHECK(fabs(GetASERTApproximationError(blocks[i-1].get(), nBits, blocks[i-2].get(), nSpacing, nHalfLife)) < dMaxErr); // relative
    blocks[i] = GetBlockIndex(blocks[i - 1].get(), nSpacing/2, nBits);
    nBits = GetNextASERTWorkRequired(blocks[i++].get(), &blkHeaderDummy, params, blocks[1].get());
    BOOST_CHECK(fabs(GetASERTApproximationError(blocks[i-1].get(), nBits, blocks[ 1 ].get(), nSpacing, nHalfLife)) < dMaxErr); // absolute
    BOOST_CHECK(fabs(GetASERTApproximationError(blocks[i-1].get(), nBits, blocks[i-2].get(), nSpacing, nHalfLife)) < dMaxErr); // relative
    BOOST_CHECK_EQUAL(nBits, initialBits);
    BOOST_CHECK(nBits != blocks[i-1]->nBits);

    // Jumping forward 2 days should double the target (halve the difficulty)
    blocks[i] = GetBlockIndex(blocks[i - 1].get(), nSpacing + nHalfLife, nBits);
    nBits = GetNextASERTWorkRequired(blocks[i++].get(), &blkHeaderDummy, params, blocks[1].get());
    BOOST_CHECK(fabs(GetASERTApproximationError(blocks[i-1].get(), nBits, blocks[ 1 ].get(), nSpacing, nHalfLife)) < dMaxErr); // absolute
    BOOST_CHECK(fabs(GetASERTApproximationError(blocks[i-1].get(), nBits, blocks[i-2].get(), nSpacing, nHalfLife)) < dMaxErr); // relative
    currentPow = arith_uint256().SetCompact(nBits) / 2;
    BOOST_CHECK_EQUAL(currentPow.GetCompact(), initialBits);

    // Jumping backward 2 days should bring target back to where we started
    blocks[i] = GetBlockIndex(blocks[i - 1].get(), nSpacing - nHalfLife, nBits);
    nBits = GetNextASERTWorkRequired(blocks[i++].get(), &blkHeaderDummy, params, blocks[1].get());
    BOOST_CHECK(fabs(GetASERTApproximationError(blocks[i-1].get(), nBits, blocks[ 1 ].get(), nSpacing, nHalfLife)) < dMaxErr); // absolute
    BOOST_CHECK(fabs(GetASERTApproximationError(blocks[i-1].get(), nBits, blocks[i-2].get(), nSpacing, nHalfLife)) < dMaxErr); // relative
    BOOST_CHECK_EQUAL(nBits, initialBits);

    // Jumping backward 2 days should halve the target (double the difficulty)
    blocks[i] = GetBlockIndex(blocks[i - 1].get(), nSpacing - nHalfLife, nBits);
    nBits = GetNextASERTWorkRequired(blocks[i++].get(), &blkHeaderDummy, params, blocks[1].get());
    BOOST_CHECK(fabs(GetASERTApproximationError(blocks[i-1].get(), nBits, blocks[ 1 ].get(), nSpacing, nHalfLife)) < dMaxErr); // absolute
    BOOST_CHECK(fabs(GetASERTApproximationError(blocks[i-1].get(), nBits, blocks[i-2].get(), nSpacing, nHalfLife)) < dMaxErr); // relative
    currentPow = arith_uint256().SetCompact(nBits);
    // Because nBits truncates target, we don't end up with exactly 1/2 the target
    BOOST_CHECK(currentPow <= arith_uint256().SetCompact(initialBits  ) / 2);
    BOOST_CHECK(currentPow >= arith_uint256().SetCompact(initialBits-1) / 2);

    // And forward again
    blocks[i] = GetBlockIndex(blocks[i - 1].get(), nSpacing + nHalfLife, nBits);
    nBits = GetNextASERTWorkRequired(blocks[i++].get(), &blkHeaderDummy, params, blocks[1].get());
    BOOST_CHECK(fabs(GetASERTApproximationError(blocks[i-1].get(), nBits, blocks[ 1 ].get(), nSpacing, nHalfLife)) < dMaxErr); // absolute
    BOOST_CHECK(fabs(GetASERTApproximationError(blocks[i-1].get(), nBits, blocks[i-2].get(), nSpacing, nHalfLife)) < dMaxErr); // relative
    BOOST_CHECK_EQUAL(nBits, initialBits);
    blocks[i] = GetBlockIndex(blocks[i - 1].get(), nSpacing + nHalfLife, nBits);
    nBits = GetNextASERTWorkRequired(blocks[i++].get(), &blkHeaderDummy, params, blocks[1].get());
    BOOST_CHECK(fabs(GetASERTApproximationError(blocks[i-1].get(), nBits, blocks[ 1 ].get(), nSpacing, nHalfLife)) < dMaxErr); // absolute
    BOOST_CHECK(fabs(GetASERTApproximationError(blocks[i-1].get(), nBits, blocks[i-2].get(), nSpacing, nHalfLife)) < dMaxErr); // relative
    currentPow = arith_uint256().SetCompact(nBits) / 2;
    BOOST_CHECK_EQUAL(currentPow.GetCompact(), initialBits);

    // Sweep one half-life either side of the anchor to check that our integer
    // approximation:
    //   1. Should be monotonic
    //   2. Should change target at least once every 8 seconds (worst-case: 15-bit precision on nBits)
    //   3. Should never change target by more than XXXX per 1-second step
    //   4. Never exceeds dMaxError in absolute error vs a double float calculation
    //   5. Has almost exactly the dMax and dMin errors we expect for the formula
    double dMin = 0;
    double dMax = 0;
    double dErr;
    double dRelMin = 0;
    double dRelMax = 0;
    double dRelErr;
    double dMaxStep = 0;
    // One nBits ulp plus one second of curve movement — see the check below.
    const double dMaxStepAllowed = std::pow(2.0, -15) + (std::pow(2.0, 1.0/double(nHalfLife)) - 1.0);
    uint32_t nBitsRingBuffer[8];
    double dStep = 0;
    blocks[i] = GetBlockIndex(blocks[i - 1].get(), -nHalfLife - 30, nBits);
    // Upstream's 4*24*3600 is exactly 2 * its half-life, i.e. a +/- one half-life
    // sweep. Left literal it runs to +15 half-lives on a six-hour chain, the
    // target clamps at powLimit, and every check below fails on the clamp rather
    // than on the arithmetic.
    for (size_t j = 0; j < size_t(2*nHalfLife) + 660; j++) {
        blocks[i]->nTime++;
        nBits = GetNextASERTWorkRequired(blocks[i].get(), &blkHeaderDummy, params, blocks[1].get());

        if (j > 8) {
            // 1: Monotonic
            BOOST_CHECK(arith_uint256().SetCompact(nBits) >= arith_uint256().SetCompact(nBitsRingBuffer[(j-1)%8]));
            // 2: Changes at least once every 8 seconds (worst case: nBits = 1d008000 to 1d008001)
            BOOST_CHECK(arith_uint256().SetCompact(nBits) > arith_uint256().SetCompact(nBitsRingBuffer[j%8]));
            // 3: Check 1-sec step size
            dStep = (TargetFromBits(nBits) - TargetFromBits(nBitsRingBuffer[(j-1)%8])) / TargetFromBits(nBits);
            if (dStep > dMaxStep) dMaxStep = dStep;
            // Upstream's literal is one ulp of the nBits mantissa (1d008000 to
            // 1d008001, 2^-15). That is a valid bound only while the ASERT curve
            // itself moves less than one ulp per second, which holds at a
            // two-day half-life and does not at six hours: the curve alone moves
            // 2^(1/halfLife)-1 per second, and at 21600s that already exceeds
            // one ulp. A one-second step can therefore cross more than one
            // quantisation level, legitimately. The bound is the sum of the two.
            BOOST_CHECK(dStep < dMaxStepAllowed);
        }
        nBitsRingBuffer[j%8] = nBits;

        // 4 and 5: check error vs double precision float calculation
        dErr    = GetASERTApproximationError(blocks[i].get(), nBits, blocks[1].get(), nSpacing, nHalfLife);
        dRelErr = GetASERTApproximationError(blocks[i].get(), nBits, blocks[i-1].get(), nSpacing, nHalfLife);
        if (dErr    < dMin)    dMin    = dErr;
        if (dErr    > dMax)    dMax    = dErr;
        if (dRelErr < dRelMin) dRelMin = dRelErr;
        if (dRelErr > dRelMax) dRelMax = dRelErr;
        BOOST_CHECK_MESSAGE(fabs(dErr) < dMaxErr,
                            strprintf("solveTime: %d\tStep size: %.8f%%\tdErr: %.8f%%\tnBits: %0x\n",
                                      int64_t(blocks[i]->nTime) - blocks[i-1]->nTime, dStep*100, dErr*100, nBits));
        BOOST_CHECK_MESSAGE(fabs(dRelErr) < dMaxErr,
                            strprintf("solveTime: %d\tStep size: %.8f%%\tdRelErr: %.8f%%\tnBits: %0x\n",
                                      int64_t(blocks[i]->nTime) - blocks[i-1]->nTime, dStep*100, dRelErr*100, nBits));
    }
    // Upstream pins these to a 1e-16 window around the cubic approximation's
    // theoretical extremes. Those extremes are real, but whether a sweep ATTAINS
    // them to sixteen digits depends on which fractional exponents the
    // one-second grid happens to land on, and that grid maps differently under a
    // different half-life. Pinned literally, the case fails on the sampling
    // rather than on the arithmetic.
    //
    // The two properties actually worth asserting are kept, expressed against
    // dMaxErr so they hold on any parameters:
    //   * the error never exceeds the approximation's guaranteed bound, and
    //   * the sweep comes close to it, which is what shows the sweep really did
    //     cover the worst case instead of wandering through a benign region.
    // The per-iteration checks above already enforce the bound on every sample;
    // these confirm the extremes were reached.
    const double dMinExpected = -0.0001013168981059 / 0.0001166792656486;  // ~-0.868 of the bound
    auto failMsg = strprintf("Min error: %16.14f%%\tMax error: %16.14f%%\tMax step: %16.14f%%\n", dMin*100, dMax*100, dMaxStep*100);
    BOOST_CHECK_MESSAGE(   dMin <= dMinExpected * dMaxErr * 0.98
                        && dMin >= -dMaxErr
                        && dMax >   0.95 * dMaxErr
                        && dMax <=  dMaxErr,
                        failMsg);
    failMsg = strprintf("Min relError: %16.14f%%\tMax relError: %16.14f%%\n", dRelMin*100, dRelMax*100);
    BOOST_CHECK_MESSAGE(   dRelMin <= dMinExpected * dMaxErr * 0.98
                        && dRelMin >= -dMaxErr
                        && dRelMax >   0.95 * dMaxErr
                        && dRelMax <=  dMaxErr,
                        failMsg);

    // Difficulty increases as long as we produce fast blocks
    for (size_t j = 0; j < 100; i++, j++) {
        uint32_t nextBits;
        arith_uint256 currentTarget;
        currentTarget.SetCompact(nBits);

        // A FAST block: upstream's 500 is 5/6 of its 600s spacing. Written as a
        // literal it becomes a SLOW block on a 300s chain, difficulty falls, and
        // the monotonicity check below inverts.
        blocks[i] = GetBlockIndex(blocks[i - 1].get(), (nSpacing*5)/6, nBits);
        nextBits = GetNextASERTWorkRequired(blocks[i].get(), &blkHeaderDummy, params, blocks[1].get());
        arith_uint256 nextTarget;
        nextTarget.SetCompact(nextBits);

        // Make sure that target is decreased
        BOOST_CHECK(nextTarget <= currentTarget);

        nBits = nextBits;
    }

}

std::string StrPrintCalcArgs(const arith_uint256 refTarget,
                             const int64_t targetSpacing,
                             const int64_t timeDiff,
                             const int64_t heightDiff,
                             const arith_uint256 expectedTarget,
                             const uint32_t expectednBits) {
    return strprintf("\n"
                     "ref=         %s\n"
                     "spacing=     %d\n"
                     "timeDiff=    %d\n"
                     "heightDiff=  %d\n"
                     "expTarget=   %s\n"
                     "exp nBits=   0x%08x\n",
                     refTarget.ToString(),
                     targetSpacing,
                     timeDiff,
                     heightDiff,
                     expectedTarget.ToString(),
                     expectednBits);
}

// Tests of the CalculateASERT function.
BOOST_AUTO_TEST_CASE(calculate_asert_test) {
    DummyConfig config(CBaseChainParams::MAIN);
    const Consensus::Params &params = config.GetChainParams().GetConsensus();
    const int64_t nHalfLife = params.nASERTHalfLife;

    const arith_uint256 powLimit = UintToArith256(params.powLimit);
    arith_uint256 initialTarget = powLimit >> 4;
    int64_t height = 0;

    // Everything below is derived from the chain's own spacing and half-life
    // rather than written as literals. Upstream hardcodes 600 and 288*1200,
    // which encode BCH's 600-second spacing and two-day half-life; BitFinite
    // mainnet uses 300 seconds and six hours, so every one of those constants is
    // wrong here. The RELATIONSHIPS they express are what the test is actually
    // about, and those hold on any parameters:
    //
    //   ASERT exponent = (nTimeDiff - spacing * (heightDiff + 1)) / halfLife
    //
    // so the target doubles when the chain is exactly one half-life ahead of
    // schedule and halves when it is one half-life behind. Expressed that way the
    // case tests the algorithm instead of a particular chain's numbers.
    const int64_t spacing = params.nPowTargetSpacing;

    // CalculateASERT adds +1 to the height difference it receives, so the time
    // difference must reach back to the PARENT of the reference block. Assume
    // that parent was ideally spaced.
    const int64_t parent_time_diff = spacing;

    // Steady: one ideally-spaced block leaves the target untouched.
    arith_uint256 nextTarget = CalculateASERT(initialTarget, spacing, parent_time_diff + spacing, ++height, powLimit, nHalfLife);
    BOOST_CHECK(nextTarget == initialTarget);

    // A block that arrives in half the expected time tightens the target.
    nextTarget = CalculateASERT(initialTarget, spacing, parent_time_diff + spacing + spacing/2, ++height, powLimit, nHalfLife);
    BOOST_CHECK(nextTarget < initialTarget);

    // A block that makes up the shortfall restores it exactly.
    arith_uint256 prevTarget = nextTarget;
    nextTarget = CalculateASERT(initialTarget, spacing, parent_time_diff + spacing + spacing/2 + (spacing*3)/2, ++height, powLimit, nHalfLife);
    BOOST_CHECK(nextTarget > prevTarget);
    BOOST_CHECK(nextTarget == initialTarget);

    // One half-life AHEAD of schedule doubles the target (halves difficulty).
    // Solving (t - spacing*(N+1)) / halfLife == 1 with t = parent_time_diff + X
    // and parent_time_diff == spacing gives X = spacing*N + halfLife.
    const int64_t N = 288;
    prevTarget = nextTarget;
    nextTarget = CalculateASERT(prevTarget, spacing, parent_time_diff + spacing*N + nHalfLife, N, powLimit, nHalfLife);
    BOOST_CHECK(nextTarget == prevTarget * 2);

    // One half-life BEHIND schedule halves it again, back to where we started.
    prevTarget = nextTarget;
    nextTarget = CalculateASERT(prevTarget, spacing, parent_time_diff + spacing*N - nHalfLife, N, powLimit, nHalfLife);
    BOOST_CHECK(nextTarget == prevTarget / 2);
    BOOST_CHECK(nextTarget == initialTarget);

    // Ramp up from initialTarget to PowLimit - should only take 4 doublings...
    uint32_t powLimit_nBits = powLimit.GetCompact();
    uint32_t next_nBits;
    for (size_t k = 0; k < 3; k++) {
        prevTarget = nextTarget;
        nextTarget = CalculateASERT(prevTarget, spacing, parent_time_diff + spacing*N + nHalfLife, N, powLimit, nHalfLife);
        BOOST_CHECK(nextTarget == prevTarget * 2);
        BOOST_CHECK(nextTarget < powLimit);
        next_nBits = nextTarget.GetCompact();
        BOOST_CHECK(next_nBits != powLimit_nBits);
    }

    prevTarget = nextTarget;
    nextTarget = CalculateASERT(prevTarget, spacing, parent_time_diff + spacing*N + nHalfLife, N, powLimit, nHalfLife);
    next_nBits = nextTarget.GetCompact();
    BOOST_CHECK(nextTarget == prevTarget * 2);
    BOOST_CHECK(next_nBits == powLimit_nBits);

    // Fast periods now cannot increase target beyond POW limit, even if we try to overflow nextTarget.
    // prevTarget is a uint256, so 256*2 = 512 days would overflow nextTarget unless CalculateASERT
    // correctly detects this error
    // 512*144*600 upstream is 256 half-lives at BCH's parameters; written that
    // way it stays 256 half-lives on ours.
    nextTarget = CalculateASERT(prevTarget, spacing, parent_time_diff + 256*nHalfLife, 0, powLimit, nHalfLife);
    next_nBits = nextTarget.GetCompact();
    BOOST_CHECK(next_nBits == powLimit_nBits);

    // We also need to watch for underflows on nextTarget. We need to withstand an extra ~446 days worth of blocks.
    // This should bring down a powLimit target to the a minimum target of 1.
    // Likewise 2*(256-33)*144 blocks is (256-33) half-lives behind schedule at
    // BCH's spacing. heightDiff = K*halfLife/spacing keeps that meaning here.
    nextTarget = CalculateASERT(powLimit, spacing, 0, (256-33)*nHalfLife/spacing, powLimit, nHalfLife);
    next_nBits = nextTarget.GetCompact();
    BOOST_CHECK_EQUAL(next_nBits, arith_uint256(1).GetCompact());

    // Define a structure holding parameters to pass to CalculateASERT.
    // We are going to check some expected results  against a vector of
    // possible arguments.
    struct calc_params {
        arith_uint256 refTarget;
        int64_t targetSpacing;
        int64_t timeDiff;
        int64_t heightDiff;
        arith_uint256 expectedTarget;
        uint32_t expectednBits;
    };

    // Define some named input argument values
    const arith_uint256 SINGLE_300_TARGET { "00000000ffb1ffffffffffffffffffffffffffffffffffffffffffffffffffff" };
    const arith_uint256 FUNNY_REF_TARGET { "000000008000000000000000000fffffffffffffffffffffffffffffffffffff" };

    // Define our expected input and output values.
    // The timeDiff entries exclude the `parent_time_diff` - this is
    // added in the call to CalculateASERT in the test loop.
    const std::vector<calc_params> calculate_args = {

        /* refTarget, targetSpacing, timeDiff, heightDiff, expectedTarget, expectednBits */

        { powLimit, 600, 0, 2*144, powLimit >> 1, 0x1c7fffff },
        { powLimit, 600, 0, 4*144, powLimit >> 2, 0x1c3fffff },
        { powLimit >> 1, 600, 0, 2*144, powLimit >> 2, 0x1c3fffff },
        { powLimit >> 2, 600, 0, 2*144, powLimit >> 3, 0x1c1fffff },
        { powLimit >> 3, 600, 0, 2*144, powLimit >> 4, 0x1c0fffff },
        { powLimit, 600, 0, 2*(256-34)*144, 3, 0x01030000 },
        { powLimit, 600, 0, 2*(256-34)*144 + 119, 3, 0x01030000 },
        { powLimit, 600, 0, 2*(256-34)*144 + 120, 2, 0x01020000 },
        { powLimit, 600, 0, 2*(256-33)*144-1, 2, 0x01020000 },
        { powLimit, 600, 0, 2*(256-33)*144, 1, 0x01010000 },  // 1 bit less since we do not need to shift to 0
        { powLimit, 600, 0, 2*(256-32)*144, 1, 0x01010000 },  // more will not decrease below 1
        { 1, 600, 0, 2*(256-32)*144, 1, 0x01010000 },
        { powLimit, 600, 2*(512-32)*144, 0, powLimit, powLimit_nBits },
        { 1, 600, (512-64)*144*600, 0, powLimit, powLimit_nBits },
        { powLimit, 600, 300, 1, SINGLE_300_TARGET, 0x1d00ffb1 },  // clamps to powLimit
        { FUNNY_REF_TARGET, 600, 600*2*33*144, 0, powLimit, powLimit_nBits }, // confuses any attempt to detect overflow by inspecting result
        { 1, 600, 600*2*256*144, 0, powLimit, powLimit_nBits }, // overflow to exactly 2^256
        { 1, 600, 600*2*224*144 - 1, 0, arith_uint256(0xffff8) << 204, powLimit_nBits }, // just under powlimit (not clamped) yet over powlimit_nbits
    };

    // These eighteen vectors are upstream's, and every expected target in them
    // was computed for a 600-second spacing and a two-day half-life — the table
    // passes targetSpacing = 600 explicitly, but took the half-life from
    // chainparams, so on a chain with a different half-life all eighteen break.
    //
    // They are run against the parameters they were derived for, deliberately.
    // CalculateASERT is a pure function whose contract is parameterised by
    // spacing and half-life; proving the arithmetic with a known-good vector set
    // is the point, and recomputing eighteen expected uint256 targets for our
    // half-life would mean deriving them from our own implementation — which
    // would assert only that our code does what our code does. BitFinite's own
    // half-life is exercised by the chainparams-driven assertions above.
    const int64_t vectorHalfLife = 2 * 24 * 60 * 60;  // what the table was built for
    const int64_t vectorParentTimeDiff = 600;         // ditto, matches targetSpacing 600
    for (auto& v : calculate_args) {
        nextTarget = CalculateASERT(v.refTarget, v.targetSpacing, vectorParentTimeDiff + v.timeDiff, v.heightDiff, powLimit, vectorHalfLife);
        next_nBits = nextTarget.GetCompact();
        const auto failMsg =
            StrPrintCalcArgs(v.refTarget, v.targetSpacing, vectorParentTimeDiff + v.timeDiff, v.heightDiff, v.expectedTarget, v.expectednBits)
            + strprintf("nextTarget=  %s\nnext nBits=  0x%08x\n", nextTarget.ToString(), next_nBits);
        BOOST_CHECK_MESSAGE(nextTarget == v.expectedTarget && next_nBits == v.expectednBits, failMsg);
    }

    // And assert the chain's own half-life is what the derivations above assume,
    // so a change to chainparams shows up here rather than silently altering
    // what this case tests.
    BOOST_CHECK_EQUAL(params.nASERTHalfLife, 6 * 60 * 60);
    BOOST_CHECK_EQUAL(params.nPowTargetSpacing, 5 * 60);
}

BOOST_AUTO_TEST_SUITE_END()
