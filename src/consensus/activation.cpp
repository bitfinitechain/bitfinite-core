// Copyright (c) 2018-2023 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/activation.h>

#include <chain.h>
#include <consensus/params.h>
#include <util/system.h>

static bool IsUAHFenabled(const Consensus::Params &params, int nHeight) {
    return nHeight >= params.uahfHeight;
}

bool IsUAHFenabled(const Consensus::Params &params,
                   const CBlockIndex *pindexPrev) {
    if (pindexPrev == nullptr) {
        return false;
    }

    return IsUAHFenabled(params, pindexPrev->nHeight);
}

static bool IsDAAEnabled(const Consensus::Params &params, int nHeight) {
    return nHeight >= params.daaHeight;
}

bool IsDAAEnabled(const Consensus::Params &params,
                  const CBlockIndex *pindexPrev) {
    if (pindexPrev == nullptr) {
        return false;
    }

    return IsDAAEnabled(params, pindexPrev->nHeight);
}

bool IsMagneticAnomalyEnabled(const Consensus::Params &params,
                              int32_t nHeight) {
    return nHeight >= params.magneticAnomalyHeight;
}

bool IsMagneticAnomalyEnabled(const Consensus::Params &params,
                              const CBlockIndex *pindexPrev) {
    if (pindexPrev == nullptr) {
        return false;
    }

    return IsMagneticAnomalyEnabled(params, pindexPrev->nHeight);
}

static bool IsGravitonEnabled(const Consensus::Params &params,
                              int32_t nHeight) {
    return nHeight >= params.gravitonHeight;
}

bool IsGravitonEnabled(const Consensus::Params &params,
                       const CBlockIndex *pindexPrev) {
    if (pindexPrev == nullptr) {
        return false;
    }

    return IsGravitonEnabled(params, pindexPrev->nHeight);
}

static bool IsPhononEnabled(const Consensus::Params &params, int32_t nHeight) {
    return nHeight >= params.phononHeight;
}

bool IsPhononEnabled(const Consensus::Params &params,
                     const CBlockIndex *pindexPrev) {
    if (pindexPrev == nullptr) {
        return false;
    }

    return IsPhononEnabled(params, pindexPrev->nHeight);
}

bool IsAxionEnabled(const Consensus::Params &params,
                    const CBlockIndex *pindexPrev) {
    if (pindexPrev == nullptr) {
        return false;
    }

    if (params.asertAnchorParams) {
        // This chain has a checkpointed anchor block, do simple height check
        return pindexPrev->nHeight >= params.asertAnchorParams->nHeight;
    }

    // Otherwise, do the MTP check
    return pindexPrev->GetMedianTimePast() >=
           gArgs.GetArg("-axionactivationtime", params.axionActivationTime);
}

bool IsUpgrade8Enabled(const Consensus::Params &params, const CBlockIndex *pindexPrev) {
    if (pindexPrev == nullptr) {
        return false;
    }

    return pindexPrev->nHeight >= params.upgrade8Height;
}

std::optional<int32_t> g_Upgrade9HeightOverride;

int32_t GetUpgrade9ActivationHeight(const Consensus::Params &params) {
    return g_Upgrade9HeightOverride.value_or(params.upgrade9Height);
}

bool IsUpgrade9EnabledForHeightPrev(const Consensus::Params &params, const int32_t nHeightPrev) {
    return nHeightPrev >= GetUpgrade9ActivationHeight(params);
}

bool IsUpgrade9Enabled(const Consensus::Params &params, const CBlockIndex *pindexPrev) {
    if (pindexPrev == nullptr) {
        return false;
    }

    return IsUpgrade9EnabledForHeightPrev(params, pindexPrev->nHeight);
}

static bool IsUpgrade10Enabled(const Consensus::Params &params, const int64_t nMedianTimePast) {
    return nMedianTimePast >= gArgs.GetArg("-upgrade10activationtime", params.upgrade10ActivationTime);
}

bool IsUpgrade10Enabled(const Consensus::Params &params, const CBlockIndex *pindexPrev) {
    if (pindexPrev == nullptr) {
        return false;
    }

    return IsUpgrade10Enabled(params, pindexPrev->GetMedianTimePast());
}

// IsUpgrade11Enabled was removed 2026-08-22. BitFinite has not adopted BCH's
// Upgrade 11 (the VM Limits and BigInt CHIPs) — those change which scripts are
// valid, so adopting them is a hard fork of this chain and a decision to take
// deliberately, not a backport. The gate itself was never wired to any rule
// here: it was declared, implemented, returned true from 15 May 2025 onward
// because the activation time was inherited from BCH, and nothing in the node
// ever asked. Its only caller was its own unit test.
//
// Keeping it cost nothing at runtime and cost credibility everywhere else: the
// source asserted an upgrade this chain does not implement. If Upgrade 11 is
// ever adopted, re-add the gate together with the rules it gates.
// See doc/upstream-divergence.md.
