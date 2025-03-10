// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#include "rpc/client.h"
#include "rpc/protocol.h"
#include "pbaas/crosschainrpc.h"
#include "util.h"

#include <set>
#include <stdint.h>

#include <univalue.h>

using namespace std;

class CRPCConvertParam
{
public:
    std::string methodName; //!< method whose params want conversion
    int paramIdx;           //!< 0-based idx of param to convert
};

// DUMMY for compile - only used in server
UniValue RPCCallRoot(const string& strMethod, const UniValue& params, int timeout)
{
    assert(false);
}

bool SetThisChain(UniValue &chainDefinition)
{}

int32_t uni_get_int(UniValue uv, int32_t def)
{
    try
    {
        return uv.get_int();
    }
    catch(const std::exception& e)
    {
        return def;
    }
}

int64_t uni_get_int64(UniValue uv, int64_t def)
{
    try
    {
        return uv.get_int64();
    }
    catch(const std::exception& e)
    {
        return def;
    }
}

std::string uni_get_str(UniValue uv, std::string def)
{
    try
    {
        return uv.get_str();
    }
    catch(const std::exception& e)
    {
        return def;
    }
}

std::vector<UniValue> uni_getValues(UniValue uv, std::vector<UniValue> def)
{
    try
    {
        return uv.getValues();
    }
    catch(const std::exception& e)
    {
        return def;
    }
}

static const CRPCConvertParam vRPCConvertParams[] =
{
    { "stop", 0 },
    { "setmocktime", 0 },
    { "getaddednodeinfo", 0 },
    { "setgenerate", 0 },
    { "setgenerate", 1 },
    { "generate", 0 },
    { "getnetworkhashps", 0 },
    { "getnetworkhashps", 1 },
    { "getnetworksolps", 0 },
    { "getnetworksolps", 1 },
    { "sendtoaddress", 1 },
    { "sendtoaddress", 4 },
    { "settxfee", 0 },
    { "getreceivedbyaddress", 1 },
    { "getreceivedbyaccount", 1 },
    { "listreceivedbyaddress", 0 },
    { "listreceivedbyaddress", 1 },
    { "listreceivedbyaddress", 2 },
    { "listreceivedbyaccount", 0 },
    { "listreceivedbyaccount", 1 },
    { "listreceivedbyaccount", 2 },
    { "getbalance", 1 },
    { "getbalance", 2 },
    { "getblockhash", 0 },
    { "move", 2 },
    { "move", 3 },
    { "sendfrom", 2 },
    { "sendfrom", 3 },
    { "listtransactions", 1 },
    { "listtransactions", 2 },
    { "listtransactions", 3 },
    { "listaccounts", 0 },
    { "listaccounts", 1 },
    { "walletpassphrase", 1 },
    { "getblocktemplate", 0 },
    { "listsinceblock", 1 },
    { "listsinceblock", 2 },
    { "sendmany", 1 },
    { "sendmany", 2 },
    { "sendmany", 4 },
    { "addmultisigaddress", 0 },
    { "addmultisigaddress", 1 },
    { "createmultisig", 0 },
    { "createmultisig", 1 },
    { "listunspent", 0 },
    { "listunspent", 1 },
    { "listunspent", 2 },
    { "getblock", 1 },
    { "getblockheader", 1 },
    { "gettransaction", 1 },
    { "getrawtransaction", 1 },
    { "createrawtransaction", 0 },
    { "createrawtransaction", 1 },
    { "createrawtransaction", 2 },
    { "createrawtransaction", 3 },
    { "signrawtransaction", 1 },
    { "signrawtransaction", 2 },
    { "sendrawtransaction", 1 },
    { "fundrawtransaction", 1 },
    { "gettxout", 1 },
    { "gettxout", 2 },
    { "gettxoutproof", 0 },
    { "lockunspent", 0 },
    { "lockunspent", 1 },
    { "importprivkey", 2 },
    { "importaddress", 2 },
    { "verifychain", 0 },
    { "verifychain", 1 },
    { "keypoolrefill", 0 },
    { "getrawmempool", 0 },
    { "estimatefee", 0 },
    { "estimatepriority", 0 },
    { "prioritisetransaction", 1 },
    { "prioritisetransaction", 2 },
    { "setban", 2 },
    { "setban", 3 },
    { "getspentinfo", 0},
    { "getaddresstxids", 0},
    { "getaddressbalance", 0},
    { "getaddressdeltas", 0},
    { "getaddressutxos", 0},
    { "getaddressmempool", 0},
    { "getblockhashes", 0},
    { "getblockhashes", 1},
    { "getblockhashes", 2},
    { "getblockdeltas", 0},
    { "zcrawjoinsplit", 1 },
    { "zcrawjoinsplit", 2 },
    { "zcrawjoinsplit", 3 },
    { "zcrawjoinsplit", 4 },
    { "zcbenchmark", 1 },
    { "zcbenchmark", 2 },
    { "getblocksubsidy", 0},
    { "z_listaddresses", 0},
    { "z_listreceivedbyaddress", 1},
    { "z_listunspent", 0 },
    { "z_listunspent", 1 },
    { "z_listunspent", 2 },
    { "z_listunspent", 3 },
    { "z_getbalance", 1},
    { "z_gettotalbalance", 0},
    { "z_gettotalbalance", 1},
    { "z_gettotalbalance", 2},
    { "z_mergetoaddress", 0},
    { "z_mergetoaddress", 2},
    { "z_mergetoaddress", 3},
    { "z_mergetoaddress", 4},
    { "z_sendmany", 1},
    { "z_sendmany", 2},
    { "z_sendmany", 3},
    { "z_shieldcoinbase", 2},
    { "z_shieldcoinbase", 3},
    { "z_getoperationstatus", 0},
    { "z_getoperationresult", 0},
    { "z_importkey", 1 },
    { "paxprice", 4 },
    { "paxprices", 3 },
    { "paxpending", 0 },
    { "notaries", 2 },
    { "minerids", 1 },
    { "kvsearch", 1 },
    { "kvupdate", 4 },
    { "z_importkey", 2 },
    { "z_importviewingkey", 2 },
    { "z_getpaymentdisclosure", 1},
    { "z_getpaymentdisclosure", 2},
    // crosschain
    { "assetchainproof", 1},
    { "crosschainproof", 1},
    { "getproofroot", 2},
    { "height_MoM", 1},
    { "calc_MoM", 2},
    // pbaas
    { "definechain", 0},
    { "getdefinedchains", 0},
    { "sendreserve", 0},
    { "z_setmigration", 0},
};

class CRPCConvertTable
{
private:
    std::set<std::pair<std::string, int> > members;

public:
    CRPCConvertTable();

    bool convert(const std::string& method, int idx) {
        return (members.count(std::make_pair(method, idx)) > 0);
    }
};

CRPCConvertTable::CRPCConvertTable()
{
    const unsigned int n_elem =
        (sizeof(vRPCConvertParams) / sizeof(vRPCConvertParams[0]));

    for (unsigned int i = 0; i < n_elem; i++) {
        members.insert(std::make_pair(vRPCConvertParams[i].methodName,
                                      vRPCConvertParams[i].paramIdx));
    }
}

static CRPCConvertTable rpcCvtTable;

/** Non-RFC4627 JSON parser, accepts internal values (such as numbers, true, false, null)
 * as well as objects and arrays.
 */
UniValue ParseNonRFCJSONValue(const std::string& strVal)
{
    UniValue jVal;
    if (!jVal.read(std::string("[")+strVal+std::string("]")) ||
        !jVal.isArray() || jVal.size()!=1)
        throw runtime_error(string("Error JSON:")+strVal);
    return jVal[0];
}

/** Convert strings to command-specific RPC representation */
UniValue RPCConvertValues(const std::string &strMethod, const std::vector<std::string> &strParams)
{
    UniValue params(UniValue::VARR);

    for (unsigned int idx = 0; idx < strParams.size(); idx++) {
        const std::string& strVal = strParams[idx];
        if (!rpcCvtTable.convert(strMethod, idx)) {
            // insert string value directly
            params.push_back(strVal);
        } else {
            // parse string as JSON, insert bool/number/object/etc. value
            params.push_back(ParseNonRFCJSONValue(strVal));
        }
    }

    return params;
}
