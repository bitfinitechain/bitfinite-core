// Copyright (c) 2019-2022 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#define BOOST_TEST_MODULE Bitcoin Seeder Test Suite

#include <arith_uint256.h>
#include <chainparams.h>
#include <clientversion.h>
#include <protocol.h>
#include <seeder/bitcoin.h>
#include <seeder/db.h>
#include <seeder/test/util.h>
#include "serialize.h"
#include <streams.h>
#include <util/system.h>
#include <version.h>
#include <test/setup_common.h>
#include <validation.h>

#include <ctime>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

std::ostream &operator<<(std::ostream &os, const PeerMessagingState &state) {
    os << to_integral(state);
    return os;
}

namespace {
class CSeederNodeTest : public CSeederNode {
public:
    CSeederNodeTest(const CService &service, std::vector<CAddress> *vAddrIn)
        : CSeederNode(service, vAddrIn) {}

    void TestProcessMessage(const std::string &msg_type, CDataStream &message,
                            PeerMessagingState expectedState) {
        PeerMessagingState ret = ProcessMessage(msg_type, message);
        BOOST_CHECK_EQUAL(ret, expectedState);
    }

    CDataStream getSendBuffer() { return vSend; }
};
} // namespace

static const unsigned short SERVICE_PORT = 18444;

struct SeederTestingSetup : public TestChain100Setup {
    SeederTestingSetup() {
        CNetAddr ip;
        ip.SetInternal("bitcoin.test");
        CService service = {ip, SERVICE_PORT};
        vAddr.emplace_back(service, ServiceFlags());
        testNode = std::make_unique<CSeederNodeTest>(service, &vAddr);
    }

    std::vector<CAddress> vAddr;
    std::unique_ptr<CSeederNodeTest> testNode;
};

BOOST_FIXTURE_TEST_SUITE(p2p_messaging_tests, SeederTestingSetup)

static constexpr int OUR_VERSION = PROTOCOL_VERSION;
static constexpr const char *OUR_SUBVERSION = "/custom-useragent/";

static CDataStream
CreateVersionMessage(int64_t now, CAddress addrTo, CAddress addrFrom,
                     int32_t start_height, uint32_t nVersion = OUR_VERSION,
                     uint64_t nonce = 0, std::string user_agent = OUR_SUBVERSION) {
    CDataStream payload(SER_NETWORK, 0);
    payload.SetVersion(INIT_PROTO_VERSION);
    ServiceFlags serviceflags = ServiceFlags(NODE_NETWORK);
    payload << nVersion << uint64_t(serviceflags) << now << addrTo << addrFrom
            << nonce << user_agent << start_height;
    return payload;
}

static const int SEEDER_INIT_VERSION = 0;

BOOST_AUTO_TEST_CASE(process_version_msg) {
    CService serviceFrom;
    CAddress addrFrom(serviceFrom,
                      ServiceFlags(NODE_NETWORK | NODE_BITCOIN_CASH));

    CDataStream versionMessage = CreateVersionMessage(std::time(nullptr), vAddr[0], addrFrom, GetRequireHeight());

    // Verify the version is set as the initial value
    BOOST_CHECK_EQUAL(testNode->CSeederNode::GetClientVersion(),
                      SEEDER_INIT_VERSION);
    BOOST_CHECK_EQUAL(testNode->GetClientSubVersion(), "");
    testNode->TestProcessMessage(NetMsgType::VERSION, versionMessage,
                                 PeerMessagingState::AwaitingMessages);
    // Verify the version has been updated
    BOOST_CHECK_EQUAL(testNode->CSeederNode::GetClientVersion(), OUR_VERSION);
    // Also verify the subversion has been updated
    BOOST_CHECK_EQUAL(testNode->GetClientSubVersion(), OUR_SUBVERSION);

    // Seeder should respond with a SENDADDRV2 message, then a VERACK
    const CMessageHeader::MessageMagic netMagic = Params().NetMagic();
    CMessageHeader header(netMagic);
    CDataStream sendBuffer = testNode->getSendBuffer();
    sendBuffer >> header;
    BOOST_CHECK(header.IsValidWithoutConfig(netMagic));
    BOOST_CHECK_EQUAL(header.GetCommand(), NetMsgType::SENDADDRV2);

    // next, VERACK
    sendBuffer >> header;
    BOOST_CHECK(header.IsValidWithoutConfig(netMagic));
    BOOST_CHECK_EQUAL(header.GetCommand(), NetMsgType::VERACK);
}

BOOST_AUTO_TEST_CASE(process_verack_msg) {
    CDataStream verackMessage(SER_NETWORK, 0);
    verackMessage.SetVersion(OUR_VERSION);
    testNode->TestProcessMessage(NetMsgType::VERACK, verackMessage,
                                 PeerMessagingState::AwaitingMessages);

    // Seeder should respond with a GETADDR message
    const CMessageHeader::MessageMagic netMagic = Params().NetMagic();
    CMessageHeader header(netMagic);
    CDataStream sendBuffer = testNode->getSendBuffer();
    sendBuffer >> header;
    BOOST_CHECK(header.IsValidWithoutConfig(netMagic));
    BOOST_CHECK_EQUAL(header.GetCommand(), NetMsgType::GETADDR);

    // Next message should be GETHEADERS
    sendBuffer >> header;
    BOOST_CHECK(header.IsValidWithoutConfig(netMagic));
    BOOST_CHECK_EQUAL(header.GetCommand(), NetMsgType::GETHEADERS);

    CBlockLocator locator;
    uint256 hashStop;
    sendBuffer >> locator >> hashStop;
    std::vector<BlockHash> expectedLocator = {
        Params().Checkpoints().mapCheckpoints.rbegin()->second};
    BOOST_CHECK(locator.vHave == expectedLocator);
    BOOST_CHECK(hashStop == uint256());
}

static CDataStream CreateHeadersMessage(const std::vector<CBlockHeader> &sendHeaders, int clientVersion) {
    CDataStream payload(SER_NETWORK, 0);
    payload.SetVersion(clientVersion);
    payload << sendHeaders;
    return payload;
}

BOOST_AUTO_TEST_CASE(process_headers_msg) {
    CService serviceFrom;
    CAddress addrFrom(serviceFrom,
                      ServiceFlags(NODE_NETWORK | NODE_BITCOIN_CASH));

    CDataStream versionMessage = CreateVersionMessage(std::time(nullptr), vAddr[0], addrFrom, GetRequireHeight() + 1);

    testNode->TestProcessMessage(NetMsgType::VERSION, versionMessage,
                                 PeerMessagingState::AwaitingMessages);

    BOOST_CHECK(!testNode->IsCheckpointVerified());

    auto blockOneHeader = ::ChainActive()[1]->GetBlockHeader();

    CDataStream headersMessage = CreateHeadersMessage({blockOneHeader}, testNode->GetClientVersion());

    testNode->TestProcessMessage(NetMsgType::HEADERS, headersMessage,
                                 PeerMessagingState::AwaitingMessages);
    BOOST_CHECK(testNode->GetBan() == 0);
    BOOST_CHECK(testNode->IsCheckpointVerified());

    auto badBlockOneHeader = CBlockHeader();

    CDataStream badHeadersMessage = CreateHeadersMessage({badBlockOneHeader}, testNode->GetClientVersion());

    testNode->TestProcessMessage(NetMsgType::HEADERS, badHeadersMessage,
                                 PeerMessagingState::Finished);
    BOOST_CHECK(testNode->GetBan() > 0);
}

// Regression tests for the checkpoint-gate bypass (BUG-R3-S2-A6-H1).
//
// The gate is the seeder's only chain-authenticity control, and the seeder
// chooses which hosts a fresh node connects to first. It used to read
// `nStartingHeight > checkpointHeight && hashPrevBlock != checkpointHash`,
// where nStartingHeight is an integer the peer states about itself. A peer
// claiming to be at or below the checkpoint therefore skipped the hash
// comparison. With the checkpoint at genesis that meant claiming height 0, so
// any host speaking the wire protocol was recorded as checkpoint-verified and
// served in the DNS answers, at the cost of one arbitrary 80-byte blob.
//
// What must hold now: nothing becomes checkpoint-verified without presenting a
// header that connects to the checkpoint AND carries valid proof of work.

// The bypass itself: claim height 0, send a header that does not connect.
// Before the fix this set checkpointVerified and the host entered the good set.
BOOST_AUTO_TEST_CASE(spoofed_low_height_does_not_verify_checkpoint) {
    CService serviceFrom;
    CAddress addrFrom(serviceFrom, ServiceFlags(NODE_NETWORK | NODE_BITCOIN_CASH));

    // Path A from the report: nStartingHeight = 0, which is <= the checkpoint
    // height and so used to disable the hash comparison entirely.
    CDataStream versionMessage = CreateVersionMessage(std::time(nullptr), vAddr[0], addrFrom, 0);
    testNode->TestProcessMessage(NetMsgType::VERSION, versionMessage, PeerMessagingState::AwaitingMessages);
    BOOST_CHECK(!testNode->IsCheckpointVerified());

    // An arbitrary header. hashPrevBlock is null, so it connects to nothing.
    CDataStream headersMessage = CreateHeadersMessage({CBlockHeader()}, testNode->GetClientVersion());
    testNode->TestProcessMessage(NetMsgType::HEADERS, headersMessage, PeerMessagingState::Finished);

    // THE ASSERTION THAT USED TO FAIL.
    BOOST_CHECK(!testNode->IsCheckpointVerified());
    // Not banned: a peer claiming to sit below the checkpoint is most likely
    // still syncing, and answered our getheaders from genesis. Staying
    // unverified is what keeps it out of the DNS answers; the ban is a separate
    // matter. Reading the self-reported height only to be MORE lenient is safe,
    // because lying about it buys the attacker leniency, not trust.
    BOOST_CHECK_EQUAL(testNode->GetBan(), 0);
}

// The wrong-chain case the gate was written for still bans.
BOOST_AUTO_TEST_CASE(wrong_chain_above_checkpoint_is_banned) {
    CService serviceFrom;
    CAddress addrFrom(serviceFrom, ServiceFlags(NODE_NETWORK | NODE_BITCOIN_CASH));

    CDataStream versionMessage =
        CreateVersionMessage(std::time(nullptr), vAddr[0], addrFrom, GetRequireHeight() + 1);
    testNode->TestProcessMessage(NetMsgType::VERSION, versionMessage, PeerMessagingState::AwaitingMessages);

    CDataStream headersMessage = CreateHeadersMessage({CBlockHeader()}, testNode->GetClientVersion());
    testNode->TestProcessMessage(NetMsgType::HEADERS, headersMessage, PeerMessagingState::Finished);

    BOOST_CHECK(!testNode->IsCheckpointVerified());
    BOOST_CHECK(testNode->GetBan() > 0);
}

// Connecting to the checkpoint is no longer enough on its own: the header has
// to satisfy proof of work for its own nBits. Without this, forging a header
// that connects costs nothing, since hashPrevBlock is public data.
BOOST_AUTO_TEST_CASE(header_without_proof_of_work_does_not_verify_checkpoint) {
    CService serviceFrom;
    CAddress addrFrom(serviceFrom, ServiceFlags(NODE_NETWORK | NODE_BITCOIN_CASH));

    CDataStream versionMessage =
        CreateVersionMessage(std::time(nullptr), vAddr[0], addrFrom, GetRequireHeight() + 1);
    testNode->TestProcessMessage(NetMsgType::VERSION, versionMessage, PeerMessagingState::AwaitingMessages);

    // Start from the real block 1, which does connect to the checkpoint, and
    // break only the work. nBits = 0 makes the decoded target zero, which fails
    // the range check deterministically.
    CBlockHeader noWork = ::ChainActive()[1]->GetBlockHeader();
    BOOST_REQUIRE(noWork.hashPrevBlock == Params().Checkpoints().mapCheckpoints.rbegin()->second);
    noWork.nBits = 0;

    CDataStream headersMessage = CreateHeadersMessage({noWork}, testNode->GetClientVersion());
    testNode->TestProcessMessage(NetMsgType::HEADERS, headersMessage, PeerMessagingState::Finished);

    BOOST_CHECK(!testNode->IsCheckpointVerified());
    BOOST_CHECK(testNode->GetBan() > 0);
}

// Same, with a target the header's hash does not actually meet.
BOOST_AUTO_TEST_CASE(header_above_its_target_does_not_verify_checkpoint) {
    CService serviceFrom;
    CAddress addrFrom(serviceFrom, ServiceFlags(NODE_NETWORK | NODE_BITCOIN_CASH));

    CDataStream versionMessage =
        CreateVersionMessage(std::time(nullptr), vAddr[0], addrFrom, GetRequireHeight() + 1);
    testNode->TestProcessMessage(NetMsgType::VERSION, versionMessage, PeerMessagingState::AwaitingMessages);

    // A mainnet-grade target on a regtest header. Nudge the nonce until the
    // hash genuinely exceeds it, so the test asserts the comparison rather than
    // relying on it being overwhelmingly likely.
    CBlockHeader tooEasy = ::ChainActive()[1]->GetBlockHeader();
    tooEasy.nBits = 0x1d00ffff;
    arith_uint256 bnTarget;
    bnTarget.SetCompact(tooEasy.nBits);
    while (UintToArith256(tooEasy.GetHash()) <= bnTarget) {
        ++tooEasy.nNonce;
    }
    BOOST_REQUIRE(tooEasy.hashPrevBlock == Params().Checkpoints().mapCheckpoints.rbegin()->second);

    CDataStream headersMessage = CreateHeadersMessage({tooEasy}, testNode->GetClientVersion());
    testNode->TestProcessMessage(NetMsgType::HEADERS, headersMessage, PeerMessagingState::Finished);

    BOOST_CHECK(!testNode->IsCheckpointVerified());
    BOOST_CHECK(testNode->GetBan() > 0);
}

// The flag is persisted in dnsseed.dat. Entries written by a pre-fix seeder
// carry checkpointVerified = true granted by the broken gate, so a straight
// reload would keep a poisoned good set across the upgrade with no attacker
// action. Reading a v6 record must drop the flag and force re-verification.
BOOST_AUTO_TEST_CASE(stale_checkpoint_flag_is_not_trusted_on_reload) {
    // Hand-assemble a v6 CAddrInfo record: same field order as the current
    // serializer, version byte 6, and checkpointVerified set to true.
    CService service{vAddr[0]};
    CDataStream v6(SER_DISK, CLIENT_VERSION);
    v6 << uint8_t(6) << service << uint64_t(NODE_NETWORK) << int64_t(1);
    v6 << uint8_t(1);                    // tried
    v6 << int64_t(1);                    // ourLastTry
    CAddrStat blank{};
    v6 << blank << blank << blank << blank << blank;
    v6 << 1 << 1 << int(PROTOCOL_VERSION);  // total, success, clientVersion
    v6 << std::string("/BitFinite:3.1.2/");  // clientSubVersion
    v6 << int(0);                        // blocks, as the spoof reported it
    v6 << int64_t(1);                    // ourLastSuccess
    v6 << int64_t(1);                    // lastAddressRequest
    v6 << uint8_t(1);                    // checkpointVerified == true

    CAddrInfo info;
    v6 >> info;
    // The whole record must have been consumed. If the v6 branch forgot to read
    // the flag byte the stream would still hold it, and every later record in a
    // real dnsseed.dat would be parsed at the wrong offset.
    BOOST_CHECK(v6.empty());

    // CAddrInfo keeps its fields private, so assert through the serializer.
    // Writing it back out now emits version 7, and the last byte is
    // checkpointVerified.
    CDataStream out(SER_DISK, CLIENT_VERSION);
    out << info;
    BOOST_REQUIRE(out.size() > 1);
    BOOST_CHECK_EQUAL(int(uint8_t(out[0])), 7);
    // THE ASSERTION THAT USED TO FAIL: the old reader honoured the stored flag,
    // so this byte came back as 1 and the poisoned entry stayed in the good set.
    BOOST_CHECK_EQUAL(int(uint8_t(out[out.size() - 1])), 0);

    // And the reloaded entry is not servable, whichever gate catches it.
    BOOST_CHECK(!info.IsReliable());
}

BOOST_AUTO_TEST_CASE(ban_too_many_headers) {
    auto blockOneHeader = ::ChainActive()[1]->GetBlockHeader();

    CDataStream tooManyHeadersMessage(SER_NETWORK, 0);
    tooManyHeadersMessage.SetVersion(testNode->GetClientVersion());
    WriteCompactSize(tooManyHeadersMessage, 2001);
    for (size_t i = 0; i < 2001; i++) {
        tooManyHeadersMessage << blockOneHeader;
        WriteCompactSize(tooManyHeadersMessage, 0);
    }

    testNode->TestProcessMessage(NetMsgType::HEADERS, tooManyHeadersMessage,
                                 PeerMessagingState::Finished);
    BOOST_CHECK(testNode->GetBan() > 0);
}

static CDataStream CreateAddrMessage(const std::vector<CAddress> &sendAddrs, bool isAddrV2) {
    CDataStream payload(SER_NETWORK, 0);
    payload.SetVersion(isAddrV2 ? OUR_VERSION | ADDRV2_FORMAT : OUR_VERSION);
    payload << sendAddrs;
    return payload;
}

// Test that seeder responds to both ADDR and ADDRV2 messages
BOOST_AUTO_TEST_CASE(process_addr_msg) {
    // First, must send headers to satisfy the criteria that both ADDR/ADDRV2 *and* HEADERS must arrive before TestNode
    // can advance to the Finished state
    auto headersMsg = CreateHeadersMessage({::ChainActive()[1]->GetBlockHeader()}, testNode->GetClientVersion());
    BOOST_CHECK(!testNode->IsCheckpointVerified()); // sanity check: node is expecting headers
    testNode->TestProcessMessage(NetMsgType::HEADERS, headersMsg, PeerMessagingState::AwaitingMessages);
    BOOST_CHECK_EQUAL(testNode->GetBan(), 0);
    BOOST_CHECK(testNode->IsCheckpointVerified()); // node got the checkpointed header; it can advance to Finished after addr message

    for (auto [msg_type, isV2] : {std::pair(NetMsgType::ADDR, false), std::pair(NetMsgType::ADDRV2, true)}) {
        // vAddrs starts with 1 entry.
        std::vector<CAddress> sendAddrs(ADDR_SOFT_CAP - 1, vAddr[0]);

        // Happy path
        // addrs are added normally to vAddr until ADDR_SOFT_CAP is reached.
        // Add addrs up to the soft cap.
        CDataStream addrMessage = CreateAddrMessage(sendAddrs, isV2);
        BOOST_CHECK_EQUAL(1, vAddr.size());
        testNode->TestProcessMessage(msg_type, addrMessage,
                                     PeerMessagingState::AwaitingMessages);
        BOOST_CHECK_EQUAL(ADDR_SOFT_CAP, vAddr.size());

        // ADDR_SOFT_CAP is exceeded
        sendAddrs.resize(1);
        addrMessage = CreateAddrMessage(sendAddrs, isV2);
        testNode->TestProcessMessage(msg_type, addrMessage,
                                     PeerMessagingState::Finished);
        BOOST_CHECK_EQUAL(ADDR_SOFT_CAP + 1, vAddr.size());

        // Test the seeder's behavior after ADDR_SOFT_CAP addrs
        // Only one addr per ADDR message will be added, the rest are ignored
        size_t expectedSize = vAddr.size() + 1;
        for (size_t i = 1; i < 10; i++) {
            sendAddrs.resize(i, sendAddrs[0]);
            addrMessage = CreateAddrMessage(sendAddrs, isV2);
            testNode->TestProcessMessage(msg_type, addrMessage,
                                         PeerMessagingState::Finished);
            BOOST_CHECK_EQUAL(expectedSize, vAddr.size());
            ++expectedSize;
        }

        // reset vAddr for next iteration
        vAddr.resize(1);
    }
}

BOOST_AUTO_TEST_SUITE_END()
