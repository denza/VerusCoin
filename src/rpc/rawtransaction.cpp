// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#include "consensus/upgrades.h"
#include "consensus/validation.h"
#include "core_io.h"
#include "init.h"
#include "deprecation.h"
#include "key_io.h"
#include "keystore.h"
#include "main.h"
#include "merkleblock.h"
#include "net.h"
#include "primitives/transaction.h"
#include "rpc/server.h"
#include "script/script.h"
#include "script/script_error.h"
#include "script/sign.h"
#include "script/standard.h"
#include "uint256.h"

#include "cc/CCinclude.h"
#include "cc/eval.h"
#include "pbaas/notarization.h"

#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif

#include "komodo_defs.h"

#include <stdint.h>

#include <boost/assign/list_of.hpp>

#include <univalue.h>

using namespace std;

extern char ASSETCHAINS_SYMBOL[];

void ScriptPubKeyToJSON(const CScript& scriptPubKey, UniValue& out, bool fIncludeHex, bool fIncludeAsm)
{
    txnouttype type;
    vector<CTxDestination> addresses;

    // needs to be an object
    if (!out.isObject())
    {
        out = UniValue(UniValue::VOBJ);
    }

    int nRequired;
    ExtractDestinations(scriptPubKey, type, addresses, nRequired);
    out.push_back(Pair("type", GetTxnOutputType(type)));

    COptCCParams p;
    if (scriptPubKey.IsPayToCryptoCondition(p) && p.version >= COptCCParams::VERSION_V2)
    {
        switch(p.evalCode)
        {
            case EVAL_PBAASDEFINITION:
            {
                CPBaaSChainDefinition definition;

                if (p.vData.size() && (definition = CPBaaSChainDefinition(p.vData[0])).IsValid())
                {
                    out.push_back(Pair("pbaasChainDefinition", definition.ToUniValue()));
                }
                else
                {
                    out.push_back(Pair("pbaasChainDefinition", "invalid"));
                }
                break;
            }

            case EVAL_SERVICEREWARD:
            {
                CServiceReward reward;

                if (p.vData.size() && (reward = CServiceReward(p.vData[0])).IsValid())
                {
                    out.push_back(Pair("pbaasServiceReward", reward.ToUniValue()));
                }
                else
                {
                    out.push_back(Pair("pbaasServiceReward", "invalid"));
                }
                break;
            }

            case EVAL_EARNEDNOTARIZATION:
            case EVAL_ACCEPTEDNOTARIZATION:
            {
                CPBaaSNotarization notarization;

                if (p.vData.size() && (notarization = CPBaaSNotarization(p.vData[0])).IsValid())
                {
                    out.push_back(Pair("pbaasNotarization", notarization.ToUniValue()));
                }
                else
                {
                    out.push_back(Pair("pbaasNotarization", "invalid"));
                }
                break;
            }

            case EVAL_FINALIZENOTARIZATION:
            {
                CNotarizationFinalization finalization;

                if (p.vData.size())
                {
                    finalization = CNotarizationFinalization(p.vData[0]);
                    out.push_back(Pair("pbaasFinalization", finalization.ToUniValue()));
                }
                break;
            }

            case EVAL_CURRENCYSTATE:
            {
                CCoinbaseCurrencyState cbcs;

                if (p.vData.size() && (cbcs = CCoinbaseCurrencyState(p.vData[0])).IsValid())
                {
                    out.push_back(Pair("currencystate", cbcs.ToUniValue()));
                }
                else
                {
                    out.push_back(Pair("currencystate", "invalid"));
                }
                break;
            }

            case EVAL_RESERVE_TRANSFER:
            {
                CReserveTransfer rt;

                if (p.vData.size() && (rt = CReserveTransfer(p.vData[0])).IsValid())
                {
                    out.push_back(Pair("reservetransfer", rt.ToUniValue()));
                }
                else
                {
                    out.push_back(Pair("reservetransfer", "invalid"));
                }
                break;
            }

            case EVAL_RESERVE_OUTPUT:
            {
                CReserveOutput ro;

                if (p.vData.size() && (ro = CReserveOutput(p.vData[0])).IsValid())
                {
                    out.push_back(Pair("reserveoutput", ro.ToUniValue()));
                }
                else
                {
                    out.push_back(Pair("reserveoutput", "invalid"));
                }
                break;
            }

            case EVAL_RESERVE_EXCHANGE:
            {
                CReserveExchange rex;

                if (p.vData.size() && (rex = CReserveExchange(p.vData[0])).IsValid())
                {
                    out.push_back(Pair("reserveexchange", rex.ToUniValue()));
                }
                else
                {
                    out.push_back(Pair("reserveexchange", "invalid"));
                }
                break;
            }

            case EVAL_RESERVE_DEPOSIT:
            {
                CReserveOutput ro;

                if (p.vData.size() && (ro = CReserveOutput(p.vData[0])).IsValid())
                {
                    out.push_back(Pair("reservedeposit", ro.ToUniValue()));
                }
                else
                {
                    out.push_back(Pair("reservedeposit", "invalid"));
                }
                break;
            }

            case EVAL_CROSSCHAIN_EXPORT:
            {
                CCrossChainExport ccx;

                if (p.vData.size() && (ccx = CCrossChainExport(p.vData[0])).IsValid())
                {
                    out.push_back(Pair("crosschainexport", ccx.ToUniValue()));
                }
                else
                {
                    out.push_back(Pair("crosschainexport", "invalid"));
                }
                break;
            }

            case EVAL_CROSSCHAIN_IMPORT:
            {
                CCrossChainImport cci;

                if (p.vData.size() && (cci = CCrossChainImport(p.vData[0])).IsValid())
                {
                    out.push_back(Pair("crosschainimport", cci.ToUniValue()));
                }
                else
                {
                    out.push_back(Pair("crosschainimport", "invalid"));
                }
                break;
            }

            case EVAL_STAKEGUARD:
                out.push_back(Pair("stakeguard", ""));
                break;

            default:
                out.push_back(Pair("unknown", ""));
        }
    }

    if (p.vKeys.size())
    {
        out.push_back(Pair("reqSigs", p.m == 0 ? 1 : p.m));

        UniValue a(UniValue::VARR);
        for (const CTxDestination& addr : p.vKeys) {
            a.push_back(EncodeDestination(addr));
        }
        out.push_back(Pair("addresses", a));
    }

    if (fIncludeAsm)
    {
        out.push_back(Pair("asm", ScriptToAsmStr(scriptPubKey)));
    }

    if (fIncludeHex)
    {
        out.push_back(Pair("hex", HexStr(scriptPubKey.begin(), scriptPubKey.end())));
    }
}

UniValue TxJoinSplitToJSON(const CTransaction& tx) {
    bool useGroth = tx.fOverwintered && tx.nVersion >= SAPLING_TX_VERSION;
    UniValue vJoinSplit(UniValue::VARR);
    for (unsigned int i = 0; i < tx.vJoinSplit.size(); i++) {
        const JSDescription& jsdescription = tx.vJoinSplit[i];
        UniValue joinsplit(UniValue::VOBJ);

        joinsplit.push_back(Pair("vpub_old", ValueFromAmount(jsdescription.vpub_old)));
        joinsplit.push_back(Pair("vpub_oldZat", jsdescription.vpub_old));
        joinsplit.push_back(Pair("vpub_new", ValueFromAmount(jsdescription.vpub_new)));
        joinsplit.push_back(Pair("vpub_newZat", jsdescription.vpub_new));

        joinsplit.push_back(Pair("anchor", jsdescription.anchor.GetHex()));

        {
            UniValue nullifiers(UniValue::VARR);
            BOOST_FOREACH(const uint256 nf, jsdescription.nullifiers) {
                nullifiers.push_back(nf.GetHex());
            }
            joinsplit.push_back(Pair("nullifiers", nullifiers));
        }

        {
            UniValue commitments(UniValue::VARR);
            BOOST_FOREACH(const uint256 commitment, jsdescription.commitments) {
                commitments.push_back(commitment.GetHex());
            }
            joinsplit.push_back(Pair("commitments", commitments));
        }

        joinsplit.push_back(Pair("onetimePubKey", jsdescription.ephemeralKey.GetHex()));
        joinsplit.push_back(Pair("randomSeed", jsdescription.randomSeed.GetHex()));

        {
            UniValue macs(UniValue::VARR);
            BOOST_FOREACH(const uint256 mac, jsdescription.macs) {
                macs.push_back(mac.GetHex());
            }
            joinsplit.push_back(Pair("macs", macs));
        }

        CDataStream ssProof(SER_NETWORK, PROTOCOL_VERSION);
        auto ps = SproutProofSerializer<CDataStream>(ssProof, useGroth);
        boost::apply_visitor(ps, jsdescription.proof);
        joinsplit.push_back(Pair("proof", HexStr(ssProof.begin(), ssProof.end())));

        {
            UniValue ciphertexts(UniValue::VARR);
            for (const ZCNoteEncryption::Ciphertext ct : jsdescription.ciphertexts) {
                ciphertexts.push_back(HexStr(ct.begin(), ct.end()));
            }
            joinsplit.push_back(Pair("ciphertexts", ciphertexts));
        }

        vJoinSplit.push_back(joinsplit);
    }
    return vJoinSplit;
}

uint64_t komodo_accrued_interest(int32_t *txheightp,uint32_t *locktimep,uint256 hash,int32_t n,int32_t checkheight,uint64_t checkvalue,int32_t tipheight);

UniValue TxShieldedSpendsToJSON(const CTransaction& tx) {
    UniValue vdesc(UniValue::VARR);
    for (const SpendDescription& spendDesc : tx.vShieldedSpend) {
        UniValue obj(UniValue::VOBJ);
        obj.push_back(Pair("cv", spendDesc.cv.GetHex()));
        obj.push_back(Pair("anchor", spendDesc.anchor.GetHex()));
        obj.push_back(Pair("nullifier", spendDesc.nullifier.GetHex()));
        obj.push_back(Pair("rk", spendDesc.rk.GetHex()));
        obj.push_back(Pair("proof", HexStr(spendDesc.zkproof.begin(), spendDesc.zkproof.end())));
        obj.push_back(Pair("spendAuthSig", HexStr(spendDesc.spendAuthSig.begin(), spendDesc.spendAuthSig.end())));
        vdesc.push_back(obj);
    }
    return vdesc;
}

UniValue TxShieldedOutputsToJSON(const CTransaction& tx) {
    UniValue vdesc(UniValue::VARR);
    for (const OutputDescription& outputDesc : tx.vShieldedOutput) {
        UniValue obj(UniValue::VOBJ);
        obj.push_back(Pair("cv", outputDesc.cv.GetHex()));
        obj.push_back(Pair("cmu", outputDesc.cm.GetHex()));
        obj.push_back(Pair("ephemeralKey", outputDesc.ephemeralKey.GetHex()));
        obj.push_back(Pair("encCiphertext", HexStr(outputDesc.encCiphertext.begin(), outputDesc.encCiphertext.end())));
        obj.push_back(Pair("outCiphertext", HexStr(outputDesc.outCiphertext.begin(), outputDesc.outCiphertext.end())));
        obj.push_back(Pair("proof", HexStr(outputDesc.zkproof.begin(), outputDesc.zkproof.end())));
        vdesc.push_back(obj);
    }
    return vdesc;
}

int32_t myIsutxo_spent(uint256 &spenttxid,uint256 txid,int32_t vout)
{
    CSpentIndexValue spentInfo; CSpentIndexKey spentKey(txid,vout);
    if ( GetSpentIndex(spentKey,spentInfo) )
    {
        spenttxid = spentInfo.txid;
        return((int32_t)spentInfo.inputIndex);
        // out.push_back(Pair("spentHeight", spentInfo.blockHeight));
    }
    memset(&spenttxid,0,sizeof(spenttxid));
    return(-1);
}

void TxToJSONExpanded(const CTransaction& tx, const uint256 hashBlock, UniValue& entry, int nHeight = 0, int nConfirmations = 0, int nBlockTime = 0)
{
    uint256 txid = tx.GetHash();
    entry.push_back(Pair("txid", txid.GetHex()));
    entry.push_back(Pair("overwintered", tx.fOverwintered));
    entry.push_back(Pair("version", tx.nVersion));
    if (tx.fOverwintered) {
        entry.push_back(Pair("versiongroupid", HexInt(tx.nVersionGroupId)));
    }
    entry.push_back(Pair("locktime", (int64_t)tx.nLockTime));
    if (tx.fOverwintered) {
        entry.push_back(Pair("expiryheight", (int64_t)tx.nExpiryHeight));
    }
    UniValue vin(UniValue::VARR);
    BOOST_FOREACH(const CTxIn& txin, tx.vin) {
        UniValue in(UniValue::VOBJ);
        if (tx.IsCoinBase())
            in.push_back(Pair("coinbase", HexStr(txin.scriptSig.begin(), txin.scriptSig.end())));
        else if (tx.IsCoinImport()) {
            in.push_back(Pair("is_import", "1"));
        }
        else {
            in.push_back(Pair("txid", txin.prevout.hash.GetHex()));
            in.push_back(Pair("vout", (int64_t)txin.prevout.n));
            {
                uint256 hash; CTransaction tx; CTxDestination address;
                if (GetTransaction(txin.prevout.hash,tx,hash,false))
                {
                    if (ExtractDestination(tx.vout[txin.prevout.n].scriptPubKey, address))
                        in.push_back(Pair("address", CBitcoinAddress(address).ToString()));
                }
            }
            UniValue o(UniValue::VOBJ);
            o.push_back(Pair("asm", ScriptToAsmStr(txin.scriptSig, true)));
            o.push_back(Pair("hex", HexStr(txin.scriptSig.begin(), txin.scriptSig.end())));
            in.push_back(Pair("scriptSig", o));

            // Add address and value info if spentindex enabled
            CSpentIndexValue spentInfo;
            CSpentIndexKey spentKey(txin.prevout.hash, txin.prevout.n);
            if (GetSpentIndex(spentKey, spentInfo)) {
                in.push_back(Pair("value", ValueFromAmount(spentInfo.satoshis)));
                in.push_back(Pair("valueSat", spentInfo.satoshis));
                if (spentInfo.addressType == 1) {
                    in.push_back(Pair("address", CBitcoinAddress(CKeyID(spentInfo.addressHash)).ToString()));
                }
                else if (spentInfo.addressType == 2)  {
                    in.push_back(Pair("address", CBitcoinAddress(CScriptID(spentInfo.addressHash)).ToString()));
                }
            }
        }
        in.push_back(Pair("sequence", (int64_t)txin.nSequence));
        vin.push_back(in);
    }
    entry.push_back(Pair("vin", vin));
    BlockMap::iterator it = mapBlockIndex.find(pcoinsTip->GetBestBlock());
    CBlockIndex *tipindex,*pindex = it->second;
    uint64_t interest;
    UniValue vout(UniValue::VARR);
    for (unsigned int i = 0; i < tx.vout.size(); i++)
    {
        const CTxOut& txout = tx.vout[i];
        UniValue out(UniValue::VOBJ);
        out.push_back(Pair("value", ValueFromAmount(txout.nValue)));
        if ( ASSETCHAINS_SYMBOL[0] == 0 && pindex != 0 && tx.nLockTime >= 500000000 && (tipindex= chainActive.LastTip()) != 0 )
        {
            int64_t interest; int32_t txheight; uint32_t locktime;
            interest = komodo_accrued_interest(&txheight,&locktime,tx.GetHash(),i,0,txout.nValue,(int32_t)tipindex->GetHeight());
            out.push_back(Pair("interest", ValueFromAmount(interest)));
        }
        out.push_back(Pair("valueSat", txout.nValue)); // [+] Decker
        out.push_back(Pair("n", (int64_t)i));
        UniValue o(UniValue::VOBJ);
        ScriptPubKeyToJSON(txout.scriptPubKey, o, true);
        out.push_back(Pair("scriptPubKey", o));

        // Add spent information if spentindex is enabled
        CSpentIndexValue spentInfo;
        CSpentIndexKey spentKey(txid, i);
        if (GetSpentIndex(spentKey, spentInfo)) {
            out.push_back(Pair("spentTxId", spentInfo.txid.GetHex()));
            out.push_back(Pair("spentIndex", (int)spentInfo.inputIndex));
            out.push_back(Pair("spentHeight", spentInfo.blockHeight));
        }
        vout.push_back(out);
    }
    entry.push_back(Pair("vout", vout));

    UniValue vjoinsplit = TxJoinSplitToJSON(tx);
    entry.push_back(Pair("vjoinsplit", vjoinsplit));

    if (tx.fOverwintered && tx.nVersion >= SAPLING_TX_VERSION) {
        entry.push_back(Pair("valueBalance", ValueFromAmount(tx.valueBalance)));
        UniValue vspenddesc = TxShieldedSpendsToJSON(tx);
        entry.push_back(Pair("vShieldedSpend", vspenddesc));
        UniValue voutputdesc = TxShieldedOutputsToJSON(tx);
        entry.push_back(Pair("vShieldedOutput", voutputdesc));
        if (!(vspenddesc.empty() && voutputdesc.empty())) {
            entry.push_back(Pair("bindingSig", HexStr(tx.bindingSig.begin(), tx.bindingSig.end())));
        }
    }

    if (!hashBlock.IsNull()) {
        entry.push_back(Pair("blockhash", hashBlock.GetHex()));

        if (nConfirmations > 0) {
            entry.push_back(Pair("height", nHeight));
            entry.push_back(Pair("confirmations", nConfirmations));
            entry.push_back(Pair("time", nBlockTime));
            entry.push_back(Pair("blocktime", nBlockTime));
        } else {
            entry.push_back(Pair("height", -1));
            entry.push_back(Pair("confirmations", 0));
        }
    }
}

void TxToJSON(const CTransaction& tx, const uint256 hashBlock, UniValue& entry)
{
    const uint256 txid = tx.GetHash();
    entry.push_back(Pair("txid", txid.GetHex()));
    entry.push_back(Pair("overwintered", tx.fOverwintered));
    entry.push_back(Pair("version", tx.nVersion));
    if (tx.fOverwintered) {
        entry.push_back(Pair("versiongroupid", HexInt(tx.nVersionGroupId)));
    }
    entry.push_back(Pair("locktime", (int64_t)tx.nLockTime));
    if (tx.fOverwintered) {
        entry.push_back(Pair("expiryheight", (int64_t)tx.nExpiryHeight));
    }
    UniValue vin(UniValue::VARR);
    BOOST_FOREACH(const CTxIn& txin, tx.vin) {
        UniValue in(UniValue::VOBJ);
        if (tx.IsCoinBase())
            in.push_back(Pair("coinbase", HexStr(txin.scriptSig.begin(), txin.scriptSig.end())));
        else {
            in.push_back(Pair("txid", txin.prevout.hash.GetHex()));
            in.push_back(Pair("vout", (int64_t)txin.prevout.n));
            UniValue o(UniValue::VOBJ);
            o.push_back(Pair("asm", ScriptToAsmStr(txin.scriptSig, true)));
            o.push_back(Pair("hex", HexStr(txin.scriptSig.begin(), txin.scriptSig.end())));
            in.push_back(Pair("scriptSig", o));

            // Add address and value info if spentindex enabled
            CSpentIndexValue spentInfo;
            CSpentIndexKey spentKey(txin.prevout.hash, txin.prevout.n);
            if (fSpentIndex && GetSpentIndex(spentKey, spentInfo)) {
                in.push_back(Pair("value", ValueFromAmount(spentInfo.satoshis)));
                in.push_back(Pair("valueSat", spentInfo.satoshis));

                CTxDestination dest =
                    DestFromAddressHash(spentInfo.addressType, spentInfo.addressHash);
                if (IsValidDestination(dest)) {
                    in.push_back(Pair("address", EncodeDestination(dest)));
                }
            }
        }
        in.push_back(Pair("sequence", (int64_t)txin.nSequence));
        vin.push_back(in);
    }
    entry.push_back(Pair("vin", vin));
    UniValue vout(UniValue::VARR);
    BlockMap::iterator it = mapBlockIndex.find(pcoinsTip->GetBestBlock());
    CBlockIndex *tipindex,*pindex = it->second;
    uint64_t interest;
    for (unsigned int i = 0; i < tx.vout.size(); i++) {
        const CTxOut& txout = tx.vout[i];
        UniValue out(UniValue::VOBJ);
        out.push_back(Pair("value", ValueFromAmount(txout.nValue)));
        if ( ASSETCHAINS_SYMBOL[0] == 0 && pindex != 0 && tx.nLockTime >= 500000000 && (tipindex= chainActive.LastTip()) != 0 )
        {
            int64_t interest; int32_t txheight; uint32_t locktime;
            interest = komodo_accrued_interest(&txheight,&locktime,tx.GetHash(),i,0,txout.nValue,(int32_t)tipindex->GetHeight());
            out.push_back(Pair("interest", ValueFromAmount(interest)));
        }        
        out.push_back(Pair("valueZat", txout.nValue));
        out.push_back(Pair("valueSat", txout.nValue));
        out.push_back(Pair("n", (int64_t)i));
        UniValue o(UniValue::VOBJ);
        ScriptPubKeyToJSON(txout.scriptPubKey, o, true);
        out.push_back(Pair("scriptPubKey", o));

        // Add spent information if spentindex is enabled
        CSpentIndexValue spentInfo;
        CSpentIndexKey spentKey(txid, i);
        if (fSpentIndex && GetSpentIndex(spentKey, spentInfo)) {
            out.push_back(Pair("spentTxId", spentInfo.txid.GetHex()));
            out.push_back(Pair("spentIndex", (int)spentInfo.inputIndex));
            out.push_back(Pair("spentHeight", spentInfo.blockHeight));
        }
        vout.push_back(out);
    }
    entry.push_back(Pair("vout", vout));

    UniValue vjoinsplit = TxJoinSplitToJSON(tx);
    entry.push_back(Pair("vjoinsplit", vjoinsplit));

    if (tx.fOverwintered && tx.nVersion >= SAPLING_TX_VERSION) {
        entry.push_back(Pair("valueBalance", ValueFromAmount(tx.valueBalance)));
        entry.push_back(Pair("valueBalanceZat", tx.valueBalance));
        UniValue vspenddesc = TxShieldedSpendsToJSON(tx);
        entry.push_back(Pair("vShieldedSpend", vspenddesc));
        UniValue voutputdesc = TxShieldedOutputsToJSON(tx);
        entry.push_back(Pair("vShieldedOutput", voutputdesc));
        if (!(vspenddesc.empty() && voutputdesc.empty())) {
            entry.push_back(Pair("bindingSig", HexStr(tx.bindingSig.begin(), tx.bindingSig.end())));
        }
    }

    if (!hashBlock.IsNull()) {
        entry.push_back(Pair("blockhash", hashBlock.GetHex()));
        BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
        if (mi != mapBlockIndex.end() && (*mi).second) {
            CBlockIndex* pindex = (*mi).second;
            if (chainActive.Contains(pindex)) {
                entry.push_back(Pair("height", pindex->GetHeight()));
                entry.push_back(Pair("confirmations", 1 + chainActive.Height() - pindex->GetHeight()));
                entry.push_back(Pair("time", pindex->GetBlockTime()));
                entry.push_back(Pair("blocktime", pindex->GetBlockTime()));
            } else {
                entry.push_back(Pair("height", -1));
                entry.push_back(Pair("confirmations", 0));
            }
        }
    }
}

UniValue getrawtransaction(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getrawtransaction \"txid\" ( verbose )\n"
            "\nNOTE: By default this function only works sometimes. This is when the tx is in the mempool\n"
            "or there is an unspent output in the utxo for this transaction. To make it always work,\n"
            "you need to maintain a transaction index, using the -txindex command line option.\n"
            "\nReturn the raw transaction data.\n"
            "\nIf verbose=0, returns a string that is serialized, hex-encoded data for 'txid'.\n"
            "If verbose is non-zero, returns an Object with information about 'txid'.\n"

            "\nArguments:\n"
            "1. \"txid\"      (string, required) The transaction id\n"
            "2. verbose       (numeric, optional, default=0) If 0, return a string, other return a json object\n"

            "\nResult (if verbose is not set or set to 0):\n"
            "\"data\"      (string) The serialized, hex-encoded data for 'txid'\n"

            "\nResult (if verbose > 0):\n"
            "{\n"
            "  \"hex\" : \"data\",       (string) The serialized, hex-encoded data for 'txid'\n"
            "  \"txid\" : \"id\",        (string) The transaction id (same as provided)\n"
            "  \"version\" : n,          (numeric) The version\n"
            "  \"locktime\" : ttt,       (numeric) The lock time\n"
            "  \"expiryheight\" : ttt,   (numeric, optional) The block height after which the transaction expires\n"
            "  \"vin\" : [               (array of json objects)\n"
            "     {\n"
            "       \"txid\": \"id\",    (string) The transaction id\n"
            "       \"vout\": n,         (numeric) \n"
            "       \"scriptSig\": {     (json object) The script\n"
            "         \"asm\": \"asm\",  (string) asm\n"
            "         \"hex\": \"hex\"   (string) hex\n"
            "       },\n"
            "       \"sequence\": n      (numeric) The script sequence number\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"vout\" : [              (array of json objects)\n"
            "     {\n"
            "       \"value\" : x.xxx,            (numeric) The value in " + CURRENCY_UNIT + "\n"
            "       \"n\" : n,                    (numeric) index\n"
            "       \"scriptPubKey\" : {          (json object)\n"
            "         \"asm\" : \"asm\",          (string) the asm\n"
            "         \"hex\" : \"hex\",          (string) the hex\n"
            "         \"reqSigs\" : n,            (numeric) The required sigs\n"
            "         \"type\" : \"pubkeyhash\",  (string) The type, eg 'pubkeyhash'\n"
            "         \"addresses\" : [           (json array of string)\n"
            "           \"komodoaddress\"          (string) Komodo address\n"
            "           ,...\n"
            "         ]\n"
            "       }\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"vjoinsplit\" : [        (array of json objects, only for version >= 2)\n"
            "     {\n"
            "       \"vpub_old\" : x.xxx,         (numeric) public input value in KMD\n"
            "       \"vpub_new\" : x.xxx,         (numeric) public output value in KMD\n"
            "       \"anchor\" : \"hex\",         (string) the anchor\n"
            "       \"nullifiers\" : [            (json array of string)\n"
            "         \"hex\"                     (string) input note nullifier\n"
            "         ,...\n"
            "       ],\n"
            "       \"commitments\" : [           (json array of string)\n"
            "         \"hex\"                     (string) output note commitment\n"
            "         ,...\n"
            "       ],\n"
            "       \"onetimePubKey\" : \"hex\",  (string) the onetime public key used to encrypt the ciphertexts\n"
            "       \"randomSeed\" : \"hex\",     (string) the random seed\n"
            "       \"macs\" : [                  (json array of string)\n"
            "         \"hex\"                     (string) input note MAC\n"
            "         ,...\n"
            "       ],\n"
            "       \"proof\" : \"hex\",          (string) the zero-knowledge proof\n"
            "       \"ciphertexts\" : [           (json array of string)\n"
            "         \"hex\"                     (string) output note ciphertext\n"
            "         ,...\n"
            "       ]\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"blockhash\" : \"hash\",   (string) the block hash\n"
            "  \"confirmations\" : n,      (numeric) The confirmations\n"
            "  \"time\" : ttt,             (numeric) The transaction time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"blocktime\" : ttt         (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("getrawtransaction", "\"mytxid\"")
            + HelpExampleCli("getrawtransaction", "\"mytxid\" 1")
            + HelpExampleRpc("getrawtransaction", "\"mytxid\", 1")
        );

    uint256 hash = ParseHashV(params[0], "parameter 1");

    bool fVerbose = false;
    if (params.size() > 1)
        fVerbose = (params[1].get_int() != 0);

    LOCK(cs_main);

    CTransaction tx;
    uint256 hashBlock;
    int nHeight = 0;
    int nConfirmations = 0;
    int nBlockTime = 0;

    {
        LOCK(cs_main);
        if (!GetTransaction(hash, tx, hashBlock, true))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available about transaction");

        BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
        if (mi != mapBlockIndex.end() && (*mi).second) {
            CBlockIndex* pindex = (*mi).second;
            if (chainActive.Contains(pindex)) {
                nHeight = pindex->GetHeight();
                nConfirmations = 1 + chainActive.Height() - pindex->GetHeight();
                nBlockTime = pindex->GetBlockTime();
            } else {
                nHeight = -1;
                nConfirmations = 0;
                nBlockTime = pindex->GetBlockTime();
            }
        }
    }

    string strHex = EncodeHexTx(tx);

    if (!fVerbose)
        return strHex;

    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("hex", strHex));
    TxToJSONExpanded(tx, hashBlock, result, nHeight, nConfirmations, nBlockTime);
    return result;
}

int32_t gettxout_scriptPubKey(uint8_t *scriptPubKey,int32_t maxsize,uint256 txid,int32_t n)
{
    int32_t i,m; uint8_t *ptr;
    LOCK(cs_main);
    /*CCoins coins;
     for (iter=0; iter<2; iter++)
     {
     if ( iter == 0 )
     {
     LOCK(mempool.cs);
     CCoinsViewMemPool view(pcoinsTip,mempool);
     if ( view.GetCoins(txid,coins) == 0 )
     {
     //fprintf(stderr,"cant get view\n");
     continue;
     }
     mempool.pruneSpent(txid, coins); // TODO: this should be done by the CCoinsViewMemPool
     }
     else if ( pcoinsTip->GetCoins(txid,coins) == 0 )
     {
     //fprintf(stderr,"cant get pcoinsTip->GetCoins\n");
     continue;
     }
     if ( n < 0 || (unsigned int)n >= coins.vout.size() || coins.vout[n].IsNull() )
     {
     fprintf(stderr,"iter.%d n.%d vs voutsize.%d\n",iter,n,(int32_t)coins.vout.size());
     continue;
     }
     ptr = (uint8_t *)coins.vout[n].scriptPubKey.data();
     m = coins.vout[n].scriptPubKey.size();
     for (i=0; i<maxsize&&i<m; i++)
     scriptPubKey[i] = ptr[i];
     return(i);
     }*/
    CTransaction tx;
    uint256 hashBlock;
    if ( GetTransaction(txid,tx,hashBlock,false) == 0 )
        return(-1);
    else if ( n <= tx.vout.size() ) // vout.size() seems off by 1
    {
        ptr = (uint8_t *)&tx.vout[n].scriptPubKey[0];
        m = tx.vout[n].scriptPubKey.size();
        for (i=0; i<maxsize&&i<m; i++)
            scriptPubKey[i] = ptr[i];
        //fprintf(stderr,"got scriptPubKey via rawtransaction\n");
        return(i);
    }
    return(-1);
}

UniValue gettxoutproof(const UniValue& params, bool fHelp)
{
    if (fHelp || (params.size() != 1 && params.size() != 2))
        throw runtime_error(
            "gettxoutproof [\"txid\",...] ( blockhash )\n"
            "\nReturns a hex-encoded proof that \"txid\" was included in a block.\n"
            "\nNOTE: By default this function only works sometimes. This is when there is an\n"
            "unspent output in the utxo for this transaction. To make it always work,\n"
            "you need to maintain a transaction index, using the -txindex command line option or\n"
            "specify the block in which the transaction is included in manually (by blockhash).\n"
            "\nReturn the raw transaction data.\n"
            "\nArguments:\n"
            "1. \"txids\"       (string) A json array of txids to filter\n"
            "    [\n"
            "      \"txid\"     (string) A transaction hash\n"
            "      ,...\n"
            "    ]\n"
            "2. \"block hash\"  (string, optional) If specified, looks for txid in the block with this hash\n"
            "\nResult:\n"
            "\"data\"           (string) A string that is a serialized, hex-encoded data for the proof.\n"
        );

    set<uint256> setTxids;
    uint256 oneTxid;
    UniValue txids = params[0].get_array();
    for (size_t idx = 0; idx < txids.size(); idx++) {
        const UniValue& txid = txids[idx];
        if (txid.get_str().length() != 64 || !IsHex(txid.get_str()))
            throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid txid ")+txid.get_str());
        uint256 hash(uint256S(txid.get_str()));
        if (setTxids.count(hash))
            throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, duplicated txid: ")+txid.get_str());
       setTxids.insert(hash);
       oneTxid = hash;
    }

    LOCK(cs_main);

    CBlockIndex* pblockindex = NULL;

    uint256 hashBlock;
    if (params.size() > 1)
    {
        hashBlock = uint256S(params[1].get_str());
        if (!mapBlockIndex.count(hashBlock))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        pblockindex = mapBlockIndex[hashBlock];
    } else {
        CCoins coins;
        if (pcoinsTip->GetCoins(oneTxid, coins) && coins.nHeight > 0 && coins.nHeight <= chainActive.Height())
            pblockindex = chainActive[coins.nHeight];
    }

    if (pblockindex == NULL)
    {
        CTransaction tx;
        if (!GetTransaction(oneTxid, tx, Params().GetConsensus(), hashBlock, false) || hashBlock.IsNull())
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not yet in block");
        if (!mapBlockIndex.count(hashBlock))
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Transaction index corrupt");
        pblockindex = mapBlockIndex[hashBlock];
    }

    CBlock block;
    if(!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus(), 1))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");

    unsigned int ntxFound = 0;
    BOOST_FOREACH(const CTransaction&tx, block.vtx)
        if (setTxids.count(tx.GetHash()))
            ntxFound++;
    if (ntxFound != setTxids.size())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "(Not all) transactions not found in specified block");

    CDataStream ssMB(SER_NETWORK, PROTOCOL_VERSION);
    CMerkleBlock mb(block, setTxids);
    ssMB << mb;
    std::string strHex = HexStr(ssMB.begin(), ssMB.end());
    return strHex;
}

UniValue verifytxoutproof(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "verifytxoutproof \"proof\"\n"
            "\nVerifies that a proof points to a transaction in a block, returning the transaction it commits to\n"
            "and throwing an RPC error if the block is not in our best chain\n"
            "\nArguments:\n"
            "1. \"proof\"    (string, required) The hex-encoded proof generated by gettxoutproof\n"
            "\nResult:\n"
            "[\"txid\"]      (array, strings) The txid(s) which the proof commits to, or empty array if the proof is invalid\n"
        );

    CDataStream ssMB(ParseHexV(params[0], "proof"), SER_NETWORK, PROTOCOL_VERSION);
    CMerkleBlock merkleBlock;
    ssMB >> merkleBlock;

    UniValue res(UniValue::VARR);

    vector<uint256> vMatch;
    if (merkleBlock.txn.ExtractMatches(vMatch) != merkleBlock.header.hashMerkleRoot)
        return res;

    LOCK(cs_main);
    uint256 idx = merkleBlock.header.GetHash();
    if (!mapBlockIndex.count(merkleBlock.header.GetHash()) || (mapBlockIndex.count(idx) && !chainActive.Contains(mapBlockIndex[idx])))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found in chain");

    BOOST_FOREACH(const uint256& hash, vMatch)
        res.push_back(hash.GetHex());
    return res;
}

UniValue createrawtransaction(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 4)
        throw runtime_error(
            "createrawtransaction [{\"txid\":\"id\",\"vout\":n},...] {\"address\":amount,...} ( locktime ) ( expiryheight )\n"
            "\nCreate a transaction spending the given inputs and sending to the given addresses.\n"
            "Returns hex-encoded raw transaction.\n"
            "Note that the transaction's inputs are not signed, and\n"
            "it is not stored in the wallet or transmitted to the network.\n"

            "\nArguments:\n"
            "1. \"transactions\"        (string, required) A json array of json objects\n"
            "     [\n"
            "       {\n"
            "         \"txid\":\"id\",    (string, required) The transaction id\n"
            "         \"vout\":n        (numeric, required) The output number\n"
            "         \"sequence\":n    (numeric, optional) The sequence number\n"
            "       }\n"
            "       ,...\n"
            "     ]\n"
            "2. \"addresses\"           (string, required) a json object with addresses as keys and amounts as values\n"
            "    {\n"
            "      \"address\": x.xxx   (numeric, required) The key is the Komodo address, the value is the " + CURRENCY_UNIT + " amount\n"
            "      ,...\n"
            "    }\n"
            "3. locktime              (numeric, optional, default=0) Raw locktime. Non-0 value also locktime-activates inputs\n"
            "4. expiryheight          (numeric, optional, default="
                + strprintf("nextblockheight+%d (pre-Blossom) or nextblockheight+%d (post-Blossom)", DEFAULT_PRE_BLOSSOM_TX_EXPIRY_DELTA, DEFAULT_POST_BLOSSOM_TX_EXPIRY_DELTA) + ") "
                "Expiry height of transaction (if Overwinter is active)\n"
            "\nResult:\n"
            "\"transaction\"            (string) hex string of the transaction\n"

            "\nExamples\n"
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\" \"{\\\"address\\\":0.01}\"")
            + HelpExampleRpc("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\", \"{\\\"address\\\":0.01}\"")
        );

    LOCK(cs_main);
    RPCTypeCheck(params, boost::assign::list_of(UniValue::VARR)(UniValue::VOBJ)(UniValue::VNUM)(UniValue::VNUM), true);
    if (params[0].isNull() || params[1].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, arguments 1 and 2 must be non-null");

    UniValue inputs = params[0].get_array();
    UniValue sendTo = params[1].get_obj();

    int nextBlockHeight = chainActive.Height() + 1;
    CMutableTransaction rawTx = CreateNewContextualCMutableTransaction(
        Params().GetConsensus(), nextBlockHeight);

    if (params.size() > 2 && !params[2].isNull()) {
        int64_t nLockTime = params[2].get_int64();
        if (nLockTime < 0 || nLockTime > std::numeric_limits<uint32_t>::max())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, locktime out of range");
        rawTx.nLockTime = nLockTime;
    }
    
    if (params.size() > 3 && !params[3].isNull()) {
        if (Params().GetConsensus().NetworkUpgradeActive(nextBlockHeight, Consensus::UPGRADE_OVERWINTER)) {
            int64_t nExpiryHeight = params[3].get_int64();
            if (nExpiryHeight < 0 || nExpiryHeight >= TX_EXPIRY_HEIGHT_THRESHOLD) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid parameter, expiryheight must be nonnegative and less than %d.", TX_EXPIRY_HEIGHT_THRESHOLD));
            }
            // DoS mitigation: reject transactions expiring soon
            if (nextBlockHeight + TX_EXPIRING_SOON_THRESHOLD > nExpiryHeight) {
                throw JSONRPCError(RPC_INVALID_PARAMETER,
                    strprintf("Invalid parameter, expiryheight should be at least %d to avoid transaction expiring soon",
                    nextBlockHeight + TX_EXPIRING_SOON_THRESHOLD));
            }
            rawTx.nExpiryHeight = nExpiryHeight;
        } else {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expiryheight can only be used if Overwinter is active when the transaction is mined");
        }
    }

    for (size_t idx = 0; idx < inputs.size(); idx++) {
        const UniValue& input = inputs[idx];
        const UniValue& o = input.get_obj();

        uint256 txid = ParseHashO(o, "txid");

        const UniValue& vout_v = find_value(o, "vout");
        if (!vout_v.isNum())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing vout key");
        int nOutput = vout_v.get_int();
        if (nOutput < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, vout must be positive");

        uint32_t nSequence = (rawTx.nLockTime ? std::numeric_limits<uint32_t>::max() - 1 : std::numeric_limits<uint32_t>::max());

        // set the sequence number if passed in the parameters object
        const UniValue& sequenceObj = find_value(o, "sequence");
        if (sequenceObj.isNum())
            nSequence = sequenceObj.get_int();

        CTxIn in(COutPoint(txid, nOutput), CScript(), nSequence);

        rawTx.vin.push_back(in);
    }

    std::set<CTxDestination> destinations;
    vector<string> addrList = sendTo.getKeys();
    for (const std::string& name_ : addrList) {
        CTxDestination destination = DecodeDestination(name_);
        if (!IsValidDestination(destination)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Komodo address: ") + name_);
        }

        if (!destinations.insert(destination).second) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, duplicated address: ") + name_);
        }

        CScript scriptPubKey = GetScriptForDestination(destination);
        CAmount nAmount = AmountFromValue(sendTo[name_]);

        CTxOut out(nAmount, scriptPubKey);
        rawTx.vout.push_back(out);
    }

    return EncodeHexTx(rawTx);
}

UniValue decoderawtransaction(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "decoderawtransaction \"hexstring\"\n"
            "\nReturn a JSON object representing the serialized, hex-encoded transaction.\n"

            "\nArguments:\n"
            "1. \"hex\"      (string, required) The transaction hex string\n"

            "\nResult:\n"
            "{\n"
            "  \"txid\" : \"id\",        (string) The transaction id\n"
            "  \"overwintered\" : bool   (boolean) The Overwintered flag\n"
            "  \"version\" : n,          (numeric) The version\n"
            "  \"versiongroupid\": \"hex\"   (string, optional) The version group id (Overwintered txs)\n"
            "  \"locktime\" : ttt,       (numeric) The lock time\n"
            "  \"expiryheight\" : n,     (numeric, optional) Last valid block height for mining transaction (Overwintered txs)\n"
            "  \"vin\" : [               (array of json objects)\n"
            "     {\n"
            "       \"txid\": \"id\",    (string) The transaction id\n"
            "       \"vout\": n,         (numeric) The output number\n"
            "       \"scriptSig\": {     (json object) The script\n"
            "         \"asm\": \"asm\",  (string) asm\n"
            "         \"hex\": \"hex\"   (string) hex\n"
            "       },\n"
            "       \"sequence\": n     (numeric) The script sequence number\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"vout\" : [             (array of json objects)\n"
            "     {\n"
            "       \"value\" : x.xxx,            (numeric) The value in " + CURRENCY_UNIT + "\n"
            "       \"n\" : n,                    (numeric) index\n"
            "       \"scriptPubKey\" : {          (json object)\n"
            "         \"asm\" : \"asm\",          (string) the asm\n"
            "         \"hex\" : \"hex\",          (string) the hex\n"
            "         \"reqSigs\" : n,            (numeric) The required sigs\n"
            "         \"type\" : \"pubkeyhash\",  (string) The type, eg 'pubkeyhash'\n"
            "         \"addresses\" : [           (json array of string)\n"
            "           \"RTZMZHDFSTFQst8XmX2dR4DaH87cEUs3gC\"   (string) komodo address\n"
            "           ,...\n"
            "         ]\n"
            "       }\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"vjoinsplit\" : [        (array of json objects, only for version >= 2)\n"
            "     {\n"
            "       \"vpub_old\" : x.xxx,         (numeric) public input value in KMD\n"
            "       \"vpub_new\" : x.xxx,         (numeric) public output value in KMD\n"
            "       \"anchor\" : \"hex\",         (string) the anchor\n"
            "       \"nullifiers\" : [            (json array of string)\n"
            "         \"hex\"                     (string) input note nullifier\n"
            "         ,...\n"
            "       ],\n"
            "       \"commitments\" : [           (json array of string)\n"
            "         \"hex\"                     (string) output note commitment\n"
            "         ,...\n"
            "       ],\n"
            "       \"onetimePubKey\" : \"hex\",  (string) the onetime public key used to encrypt the ciphertexts\n"
            "       \"randomSeed\" : \"hex\",     (string) the random seed\n"
            "       \"macs\" : [                  (json array of string)\n"
            "         \"hex\"                     (string) input note MAC\n"
            "         ,...\n"
            "       ],\n"
            "       \"proof\" : \"hex\",          (string) the zero-knowledge proof\n"
            "       \"ciphertexts\" : [           (json array of string)\n"
            "         \"hex\"                     (string) output note ciphertext\n"
            "         ,...\n"
            "       ]\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("decoderawtransaction", "\"hexstring\"")
            + HelpExampleRpc("decoderawtransaction", "\"hexstring\"")
        );

    LOCK(cs_main);
    RPCTypeCheck(params, boost::assign::list_of(UniValue::VSTR));

    CTransaction tx;

    if (!DecodeHexTx(tx, params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");

    UniValue result(UniValue::VOBJ);
    TxToJSON(tx, uint256(), result);

    return result;
}

UniValue decodescript(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "decodescript \"hex\"\n"
            "\nDecode a hex-encoded script.\n"
            "\nArguments:\n"
            "1. \"hex\"     (string) the hex encoded script\n"
            "\nResult:\n"
            "{\n"
            "  \"asm\":\"asm\",   (string) Script public key\n"
            "  \"hex\":\"hex\",   (string) hex encoded public key\n"
            "  \"type\":\"type\", (string) The output type\n"
            "  \"reqSigs\": n,    (numeric) The required signatures\n"
            "  \"addresses\": [   (json array of string)\n"
            "     \"address\"     (string) Komodo address\n"
            "     ,...\n"
            "  ],\n"
            "  \"p2sh\",\"address\" (string) script address\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("decodescript", "\"hexstring\"")
            + HelpExampleRpc("decodescript", "\"hexstring\"")
        );

    LOCK(cs_main);
    RPCTypeCheck(params, boost::assign::list_of(UniValue::VSTR));

    UniValue r(UniValue::VOBJ);
    CScript script;
    if (params[0].get_str().size() > 0){
        vector<unsigned char> scriptData(ParseHexV(params[0], "argument"));
        script = CScript(scriptData.begin(), scriptData.end());
    } else {
        // Empty scripts are valid
    }
    ScriptPubKeyToJSON(script, r, false);

    r.push_back(Pair("p2sh", EncodeDestination(CScriptID(script))));
    return r;
}

/** Pushes a JSON object for script verification or signing errors to vErrorsRet. */
static void TxInErrorToJSON(const CTxIn& txin, UniValue& vErrorsRet, const std::string& strMessage)
{
    UniValue entry(UniValue::VOBJ);
    entry.push_back(Pair("txid", txin.prevout.hash.ToString()));
    entry.push_back(Pair("vout", (uint64_t)txin.prevout.n));
    entry.push_back(Pair("scriptSig", HexStr(txin.scriptSig.begin(), txin.scriptSig.end())));
    entry.push_back(Pair("sequence", (uint64_t)txin.nSequence));
    entry.push_back(Pair("error", strMessage));
    vErrorsRet.push_back(entry);
}

UniValue signrawtransaction(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 5)
        throw runtime_error(
            "signrawtransaction \"hexstring\" ( [{\"txid\":\"id\",\"vout\":n,\"scriptPubKey\":\"hex\",\"redeemScript\":\"hex\"},...] [\"privatekey1\",...] sighashtype )\n"
            "\nSign inputs for raw transaction (serialized, hex-encoded).\n"
            "The second optional argument (may be null) is an array of previous transaction outputs that\n"
            "this transaction depends on but may not yet be in the block chain.\n"
            "The third optional argument (may be null) is an array of base58-encoded private\n"
            "keys that, if given, will be the only keys used to sign the transaction.\n"
#ifdef ENABLE_WALLET
            + HelpRequiringPassphrase() + "\n"
#endif

            "\nArguments:\n"
            "1. \"hexstring\"     (string, required) The transaction hex string\n"
            "2. \"prevtxs\"       (string, optional) An json array of previous dependent transaction outputs\n"
            "     [               (json array of json objects, or 'null' if none provided)\n"
            "       {\n"
            "         \"txid\":\"id\",             (string, required) The transaction id\n"
            "         \"vout\":n,                  (numeric, required) The output number\n"
            "         \"scriptPubKey\": \"hex\",   (string, required) script key\n"
            "         \"redeemScript\": \"hex\",   (string, required for P2SH) redeem script\n"
            "         \"amount\": value            (numeric, required) The amount spent\n"
            "       }\n"
            "       ,...\n"
            "    ]\n"
            "3. \"privatekeys\"     (string, optional) A json array of base58-encoded private keys for signing\n"
            "    [                  (json array of strings, or 'null' if none provided)\n"
            "      \"privatekey\"   (string) private key in base58-encoding\n"
            "      ,...\n"
            "    ]\n"
            "4. \"sighashtype\"     (string, optional, default=ALL) The signature hash type. Must be one of\n"
            "       \"ALL\"\n"
            "       \"NONE\"\n"
            "       \"SINGLE\"\n"
            "       \"ALL|ANYONECANPAY\"\n"
            "       \"NONE|ANYONECANPAY\"\n"
            "       \"SINGLE|ANYONECANPAY\"\n"
            "5.  \"branchid\"       (string, optional) The hex representation of the consensus branch id to sign with."
            " This can be used to force signing with consensus rules that are ahead of the node's current height.\n"

            "\nResult:\n"
            "{\n"
            "  \"hex\" : \"value\",           (string) The hex-encoded raw transaction with signature(s)\n"
            "  \"complete\" : true|false,   (boolean) If the transaction has a complete set of signatures\n"
            "  \"errors\" : [                 (json array of objects) Script verification errors (if there are any)\n"
            "    {\n"
            "      \"txid\" : \"hash\",           (string) The hash of the referenced, previous transaction\n"
            "      \"vout\" : n,                (numeric) The index of the output to spent and used as input\n"
            "      \"scriptSig\" : \"hex\",       (string) The hex-encoded signature script\n"
            "      \"sequence\" : n,            (numeric) Script sequence number\n"
            "      \"error\" : \"text\"           (string) Verification or signing error related to the input\n"
            "    }\n"
            "    ,...\n"
            "  ]\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("signrawtransaction", "\"myhex\"")
            + HelpExampleRpc("signrawtransaction", "\"myhex\"")
        );

#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwalletMain ? &pwalletMain->cs_wallet : NULL);
#else
    LOCK(cs_main);
#endif
    RPCTypeCheck(params, boost::assign::list_of(UniValue::VSTR)(UniValue::VARR)(UniValue::VARR)(UniValue::VSTR)(UniValue::VSTR), true);

    vector<unsigned char> txData(ParseHexV(params[0], "argument 1"));
    CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
    vector<CMutableTransaction> txVariants;
    while (!ssData.empty()) {
        try {
            CMutableTransaction tx;
            ssData >> tx;
            txVariants.push_back(tx);
        }
        catch (const std::exception&) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
        }
    }

    if (txVariants.empty())
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Missing transaction");

    // mergedTx will end up with all the signatures; it
    // starts as a clone of the rawtx:
    CMutableTransaction mergedTx(txVariants[0]);

    // Fetch previous transactions (inputs):
    CCoinsView viewDummy;
    CCoinsViewCache view(&viewDummy);
    {
        LOCK(mempool.cs);
        CCoinsViewCache &viewChain = *pcoinsTip;
        CCoinsViewMemPool viewMempool(&viewChain, mempool);
        view.SetBackend(viewMempool); // temporarily switch cache backend to db+mempool view

        BOOST_FOREACH(const CTxIn& txin, mergedTx.vin) {
            const uint256& prevHash = txin.prevout.hash;
            CCoins coins;
            view.AccessCoins(prevHash); // this is certainly allowed to fail
        }

        view.SetBackend(viewDummy); // switch back to avoid locking mempool for too long
    }

    bool fGivenKeys = false;
    CBasicKeyStore tempKeystore;
    if (params.size() > 2 && !params[2].isNull()) {
        fGivenKeys = true;
        UniValue keys = params[2].get_array();
        for (size_t idx = 0; idx < keys.size(); idx++) {
            UniValue k = keys[idx];
            CKey key = DecodeSecret(k.get_str());
            if (!key.IsValid())
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key");
            tempKeystore.AddKey(key);
        }
    }
#ifdef ENABLE_WALLET
    else if (pwalletMain)
        EnsureWalletIsUnlocked();
#endif

    // Add previous txouts given in the RPC call:
    if (params.size() > 1 && !params[1].isNull()) {
        UniValue prevTxs = params[1].get_array();
        for (size_t idx = 0; idx < prevTxs.size(); idx++) {
            const UniValue& p = prevTxs[idx];
            if (!p.isObject())
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "expected object with {\"txid'\",\"vout\",\"scriptPubKey\"}");

            UniValue prevOut = p.get_obj();

            RPCTypeCheckObj(prevOut, boost::assign::map_list_of("txid", UniValue::VSTR)("vout", UniValue::VNUM)("scriptPubKey", UniValue::VSTR));

            uint256 txid = ParseHashO(prevOut, "txid");

            int nOut = find_value(prevOut, "vout").get_int();
            if (nOut < 0)
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "vout must be positive");

            vector<unsigned char> pkData(ParseHexO(prevOut, "scriptPubKey"));
            CScript scriptPubKey(pkData.begin(), pkData.end());

            {
                CCoinsModifier coins = view.ModifyCoins(txid);
                if (coins->IsAvailable(nOut) && coins->vout[nOut].scriptPubKey != scriptPubKey) {
                    string err("Previous output scriptPubKey mismatch:\n");
                    err = err + ScriptToAsmStr(coins->vout[nOut].scriptPubKey) + "\nvs:\n"+
                        ScriptToAsmStr(scriptPubKey);
                    throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
                }
                if ((unsigned int)nOut >= coins->vout.size())
                    coins->vout.resize(nOut+1);
                coins->vout[nOut].scriptPubKey = scriptPubKey;
                coins->vout[nOut].nValue = 0;
                if (prevOut.exists("amount")) {
                    coins->vout[nOut].nValue = AmountFromValue(find_value(prevOut, "amount"));
                }
            }

            // if redeemScript given and not using the local wallet (private keys
            // given), add redeemScript to the tempKeystore so it can be signed:
            if (fGivenKeys && scriptPubKey.IsPayToScriptHash()) {
                RPCTypeCheckObj(prevOut, boost::assign::map_list_of("txid", UniValue::VSTR)("vout", UniValue::VNUM)("scriptPubKey", UniValue::VSTR)("redeemScript",UniValue::VSTR));
                UniValue v = find_value(prevOut, "redeemScript");
                if (!v.isNull()) {
                    vector<unsigned char> rsData(ParseHexV(v, "redeemScript"));
                    CScript redeemScript(rsData.begin(), rsData.end());
                    tempKeystore.AddCScript(redeemScript);
                }
            }
        }
    }

#ifdef ENABLE_WALLET
    const CKeyStore& keystore = ((fGivenKeys || !pwalletMain) ? tempKeystore : *pwalletMain);
#else
    const CKeyStore& keystore = tempKeystore;
#endif

    int nHashType = SIGHASH_ALL;
    if (params.size() > 3 && !params[3].isNull()) {
        static map<string, int> mapSigHashValues =
            boost::assign::map_list_of
            (string("ALL"), int(SIGHASH_ALL))
            (string("ALL|ANYONECANPAY"), int(SIGHASH_ALL|SIGHASH_ANYONECANPAY))
            (string("NONE"), int(SIGHASH_NONE))
            (string("NONE|ANYONECANPAY"), int(SIGHASH_NONE|SIGHASH_ANYONECANPAY))
            (string("SINGLE"), int(SIGHASH_SINGLE))
            (string("SINGLE|ANYONECANPAY"), int(SIGHASH_SINGLE|SIGHASH_ANYONECANPAY))
            ;
        string strHashType = params[3].get_str();
        if (mapSigHashValues.count(strHashType))
            nHashType = mapSigHashValues[strHashType];
        else
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid sighash param");
    }

    bool fHashSingle = ((nHashType & ~SIGHASH_ANYONECANPAY) == SIGHASH_SINGLE);
    // Use the approximate release height if it is greater so offline nodes 
    // have a better estimation of the current height and will be more likely to
    // determine the correct consensus branch ID.  Regtest mode ignores release height.
    int chainHeight = chainActive.Height() + 1;

    // Grab the current consensus branch ID
    auto consensusBranchId = CurrentEpochBranchId(chainHeight, Params().GetConsensus());

    if (params.size() > 4 && !params[4].isNull()) {
        consensusBranchId = ParseHexToUInt32(params[4].get_str());
        if (!IsConsensusBranchId(consensusBranchId)) {
            throw runtime_error(params[4].get_str() + " is not a valid consensus branch id");
        }
    } 
    
    // Script verification errors
    UniValue vErrors(UniValue::VARR);

    // Use CTransaction for the constant parts of the
    // transaction to avoid rehashing.
    const CTransaction txConst(mergedTx);
    // Sign what we can:
    for (unsigned int i = 0; i < mergedTx.vin.size(); i++) {
        CTxIn& txin = mergedTx.vin[i];
        const CCoins* coins = view.AccessCoins(txin.prevout.hash);
        if (coins == NULL || !coins->IsAvailable(txin.prevout.n)) {
            TxInErrorToJSON(txin, vErrors, "Input not found or already spent");
            continue;
        }
        const CScript& prevPubKey = CCoinsViewCache::GetSpendFor(coins, txin);
        const CAmount& amount = coins->vout[txin.prevout.n].nValue;

        SignatureData sigdata;
        // Only sign SIGHASH_SINGLE if there's a corresponding output:
        if (!fHashSingle || (i < mergedTx.vout.size()))
            ProduceSignature(MutableTransactionSignatureCreator(&keystore, &mergedTx, i, amount, nHashType), prevPubKey, sigdata, consensusBranchId);

        // ... and merge in other signatures:
        BOOST_FOREACH(const CMutableTransaction& txv, txVariants) {
            sigdata = CombineSignatures(prevPubKey, TransactionSignatureChecker(&txConst, i, amount), sigdata, DataFromTransaction(txv, i), consensusBranchId);
        }

        UpdateTransaction(mergedTx, i, sigdata);

        ScriptError serror = SCRIPT_ERR_OK;
        if (!VerifyScript(txin.scriptSig, prevPubKey, STANDARD_SCRIPT_VERIFY_FLAGS, TransactionSignatureChecker(&txConst, i, amount), consensusBranchId, &serror)) {
            TxInErrorToJSON(txin, vErrors, ScriptErrorString(serror));
        }
    }
    bool fComplete = vErrors.empty();

    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("hex", EncodeHexTx(mergedTx)));
    result.push_back(Pair("complete", fComplete));
    if (!vErrors.empty()) {
        result.push_back(Pair("errors", vErrors));
    }

    return result;
}

UniValue sendrawtransaction(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "sendrawtransaction \"hexstring\" ( allowhighfees )\n"
            "\nSubmits raw transaction (serialized, hex-encoded) to local node and network.\n"
            "\nAlso see createrawtransaction and signrawtransaction calls.\n"
            "\nArguments:\n"
            "1. \"hexstring\"    (string, required) The hex string of the raw transaction)\n"
            "2. allowhighfees    (boolean, optional, default=false) Allow high fees\n"
            "\nResult:\n"
            "\"hex\"             (string) The transaction hash in hex\n"
            "\nExamples:\n"
            "\nCreate a transaction\n"
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\" : \\\"mytxid\\\",\\\"vout\\\":0}]\" \"{\\\"myaddress\\\":0.01}\"") +
            "Sign the transaction, and get back the hex\n"
            + HelpExampleCli("signrawtransaction", "\"myhex\"") +
            "\nSend the transaction (signed hex)\n"
            + HelpExampleCli("sendrawtransaction", "\"signedhex\"") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("sendrawtransaction", "\"signedhex\"")
        );

    LOCK(cs_main);
    RPCTypeCheck(params, boost::assign::list_of(UniValue::VSTR)(UniValue::VBOOL));

    // parse hex string from parameter
    CTransaction tx;
    if (!DecodeHexTx(tx, params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    uint256 hashTx = tx.GetHash();

    // DoS mitigation: reject transactions expiring soon
    if (tx.nExpiryHeight > 0) {
        int nextBlockHeight = chainActive.Height() + 1;
        if (Params().GetConsensus().NetworkUpgradeActive(nextBlockHeight, Consensus::UPGRADE_OVERWINTER)) {
            if (nextBlockHeight + TX_EXPIRING_SOON_THRESHOLD > tx.nExpiryHeight) {
                throw JSONRPCError(RPC_TRANSACTION_REJECTED,
                    strprintf("tx-expiring-soon: expiryheight is %d but should be at least %d to avoid transaction expiring soon",
                    tx.nExpiryHeight,
                    nextBlockHeight + TX_EXPIRING_SOON_THRESHOLD));
            }
        }
    }

    bool fOverrideFees = false;
    if (params.size() > 1)
        fOverrideFees = params[1].get_bool();

    CCoinsViewCache &view = *pcoinsTip;
    const CCoins* existingCoins = view.AccessCoins(hashTx);
    bool fHaveMempool = mempool.exists(hashTx);
    bool fHaveChain = existingCoins && existingCoins->nHeight < 1000000000;
    if (!fHaveMempool && !fHaveChain) {
        // push to local node and sync with wallets
        CValidationState state;
        bool fMissingInputs;
        if (!AcceptToMemoryPool(mempool, state, tx, false, &fMissingInputs, !fOverrideFees)) {
            if (state.IsInvalid()) {
                throw JSONRPCError(RPC_TRANSACTION_REJECTED, strprintf("%i: %s", state.GetRejectCode(), state.GetRejectReason()));
            } else {
                if (fMissingInputs) {
                    throw JSONRPCError(RPC_TRANSACTION_ERROR, "Missing inputs");
                }
                throw JSONRPCError(RPC_TRANSACTION_ERROR, state.GetRejectReason());
            }
        }
    } else if (fHaveChain) {
        throw JSONRPCError(RPC_TRANSACTION_ALREADY_IN_CHAIN, "transaction already in block chain");
    }    
    RelayTransaction(tx);

    return hashTx.GetHex();
}

static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         okSafeMode
  //  --------------------- ------------------------  -----------------------  ----------
    { "rawtransactions",    "getrawtransaction",      &getrawtransaction,      true  },
    { "rawtransactions",    "createrawtransaction",   &createrawtransaction,   true  },
    { "rawtransactions",    "decoderawtransaction",   &decoderawtransaction,   true  },
    { "rawtransactions",    "decodescript",           &decodescript,           true  },
    { "rawtransactions",    "sendrawtransaction",     &sendrawtransaction,     false },
    { "rawtransactions",    "signrawtransaction",     &signrawtransaction,     false }, /* uses wallet if enabled */

    { "blockchain",         "gettxoutproof",          &gettxoutproof,          true  },
    { "blockchain",         "verifytxoutproof",       &verifytxoutproof,       true  },
};

void RegisterRawTransactionRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
