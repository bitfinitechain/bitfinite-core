// Copyright (c) 2017-2022 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <key_io.h>

#include <chainparams.h>
#include <config.h>
#include <script/standard.h>

#include <test/setup_common.h>

#include <boost/test/unit_test.hpp>

namespace {

class DummyCashAddrConfig : public DummyConfig {
public:
    DummyCashAddrConfig()
        : DummyConfig(CBaseChainParams::MAIN), useCashAddr(false) {}
    void SetCashAddrEncoding(bool b) override { useCashAddr = b; }
    bool UseCashAddrEncoding() const override { return useCashAddr; }

private:
    bool useCashAddr;
};

} // namespace

BOOST_FIXTURE_TEST_SUITE(dstencode_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(test_addresses) {
    std::vector<uint8_t> hash = {118, 160, 64,  83,  189, 160, 168,
                                 139, 218, 81,  119, 184, 106, 21,
                                 195, 178, 159, 85,  152, 115};
    std::vector<uint8_t> hash32 = ParseHex("80e10d3e13f5bf4e743aecd910c04e5dd9fee4184c4877163d0cc4c76b78d8f5");

    const CTxDestination dstKey = CKeyID(uint160(hash));
    const CTxDestination dstScript = ScriptID(uint160(hash));
    const CTxDestination dstScript32 = ScriptID(uint256(hash32)); // p2sh_32

    // Derived from the cashaddr spec for prefix "bfx" and our base32 charset
    // (which swaps q<->f, see src/cashaddr.cpp). The previous values were
    // upstream's BCH vectors with "bitcoincash" find-replaced to "bitfinite" —
    // wrong on both counts, since the prefix is neither ours nor checksum-neutral.
    // The generator was validated by reproducing upstream's published vectors
    // for this same hash byte-for-byte before our parameters were applied, so
    // these are an independent cross-check of the implementation rather than a
    // copy of its output.
    std::string cashaddr_pubkey =
        "bfx:fpm2fsznhks23z7629mms6s4cweq74vcwvzype89aj";
    std::string cashaddr_script =
        "bfx:ppm2fsznhks23z7629mms6s4cweq74vcwv4pukfxx0";
    std::string cashaddr_script_32 =
        "bfx:pwfwzrq7z06m7nn58tkdjyxfqewanlhyrpxysack85xvq3mt0rv02tygkxlyc"; // p2sh_32
    std::string base58_pubkey = "1BpEi6DfDAUFd7GtittLSdBeYJvcoaVggu";
    std::string base58_script = "3CWFddi6m4ndiGyKqzYvsFYagqDLPVMTzC";
    std::string base58_script_32 = "BhUvhkQ6JwtVEdHguJo6M5BJYWn5ftW9vM9WaDDFcjrWiU2KMZ"; // p2sh_32

    DummyCashAddrConfig config;

    // Check encoding
    config.SetCashAddrEncoding(true);
    BOOST_CHECK_EQUAL(cashaddr_pubkey, EncodeDestination(dstKey, config));
    BOOST_CHECK_EQUAL(cashaddr_script, EncodeDestination(dstScript, config));
    BOOST_CHECK_EQUAL(cashaddr_script_32, EncodeDestination(dstScript32, config));
    config.SetCashAddrEncoding(false);
    BOOST_CHECK_EQUAL(base58_pubkey, EncodeDestination(dstKey, config));
    BOOST_CHECK_EQUAL(base58_script, EncodeDestination(dstScript, config));
    BOOST_CHECK_EQUAL(base58_script_32, EncodeDestination(dstScript32, config));

    // Check decoding
    const CChainParams &params = config.GetChainParams();
    BOOST_CHECK(dstKey == DecodeDestination(cashaddr_pubkey, params));
    BOOST_CHECK(dstScript == DecodeDestination(cashaddr_script, params));
    BOOST_CHECK(dstScript32 == DecodeDestination(cashaddr_script_32, params));
    BOOST_CHECK(dstKey == DecodeDestination(base58_pubkey, params));
    BOOST_CHECK(dstScript == DecodeDestination(base58_script, params));
    BOOST_CHECK(dstScript32 == DecodeDestination(base58_script_32, params));

    // Validation
    BOOST_CHECK(IsValidDestinationString(cashaddr_pubkey, params));
    BOOST_CHECK(IsValidDestinationString(cashaddr_script, params));
    BOOST_CHECK(IsValidDestinationString(cashaddr_script_32, params));
    BOOST_CHECK(IsValidDestinationString(base58_pubkey, params));
    BOOST_CHECK(IsValidDestinationString(base58_script, params));
    BOOST_CHECK(IsValidDestinationString(base58_script_32, params));
    BOOST_CHECK(!IsValidDestinationString("notvalid", params));
}

BOOST_AUTO_TEST_SUITE_END()
