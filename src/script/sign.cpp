// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#include "script/sign.h"

#include "primitives/transaction.h"
#include "key.h"
#include "keystore.h"
#include "script/standard.h"
#include "uint256.h"
#include "cc/CCinclude.h"
#include "cc/eval.h"
#include "key_io.h"

#include <boost/foreach.hpp>

using namespace std;

typedef std::vector<unsigned char> valtype;

TransactionSignatureCreator::TransactionSignatureCreator(const CKeyStore* keystoreIn, const CTransaction* txToIn, unsigned int nInIn, const CAmount& amountIn, int nHashTypeIn) : BaseSignatureCreator(keystoreIn), txTo(txToIn), nIn(nInIn), nHashType(nHashTypeIn), amount(amountIn), checker(txTo, nIn, amountIn) {}

bool TransactionSignatureCreator::CreateSig(std::vector<unsigned char>& vchSig, const CKeyID& address, const CScript& scriptCode, uint32_t consensusBranchId, CKey *pprivKey, void *extraData) const
{
    CKey key;
    if (pprivKey)
        key = *pprivKey;
    else if (!keystore || !keystore->GetKey(address, key))
        return false;

    uint256 hash;
    try {
        hash = SignatureHash(scriptCode, *txTo, nIn, nHashType, amount, consensusBranchId);
    } catch (logic_error ex) {
        return false;
    }

    if (scriptCode.IsPayToCryptoCondition())
    {
        CC *cc = (CC *)extraData;
        // assume either 1of1 or 1of2. if the condition created by the
        if (!cc || cc_signTreeSecp256k1Msg32(cc, key.begin(), hash.begin()) == 0)
            return false;
        vchSig = CCSigVec(cc);
        return true;
    }
    else
    {
        if (!key.Sign(hash, vchSig))
            return false;
    }
    vchSig.push_back((unsigned char)nHashType);
    return true;
}

static bool Sign1(const CKeyID& address, const BaseSignatureCreator& creator, const CScript& scriptCode, std::vector<valtype>& ret, uint32_t consensusBranchId)
{
    vector<unsigned char> vchSig;
    if (!creator.CreateSig(vchSig, address, scriptCode, consensusBranchId))
        return false;
    ret.push_back(vchSig);
    return true;
}

static bool SignN(const vector<valtype>& multisigdata, const BaseSignatureCreator& creator, const CScript& scriptCode, std::vector<valtype>& ret, uint32_t consensusBranchId)
{
    int nSigned = 0;
    int nRequired = multisigdata.front()[0];
    for (unsigned int i = 1; i < multisigdata.size()-1 && nSigned < nRequired; i++)
    {
        const valtype& pubkey = multisigdata[i];
        CKeyID keyID = CPubKey(pubkey).GetID();
        if (Sign1(keyID, creator, scriptCode, ret, consensusBranchId))
            ++nSigned;
    }
    return nSigned==nRequired;
}

CC *CCcond1of2(uint8_t evalcode,CPubKey pk1,CPubKey pk2)
{
    std::vector<CC*> pks;
    pks.push_back(CCNewSecp256k1(pk1));
    pks.push_back(CCNewSecp256k1(pk2));
    CC *condCC = CCNewEval(E_MARSHAL(ss << evalcode));
    CC *Sig = CCNewThreshold(1, pks);
    return CCNewThreshold(2, {condCC, Sig});
}

CC *CCcond1(uint8_t evalcode,CPubKey pk)
{
    std::vector<CC*> pks;
    pks.push_back(CCNewSecp256k1(pk));
    CC *condCC = CCNewEval(E_MARSHAL(ss << evalcode));
    CC *Sig = CCNewThreshold(1, pks);
    return CCNewThreshold(2, {condCC, Sig});
}

CC *CCcond1(uint8_t evalcode, CTxDestination dest)
{
    CPubKey pk = boost::apply_visitor<GetPubKeyForPubKey>(GetPubKeyForPubKey(), dest);
    std::vector<CC*> pks;
    if (pk.IsValid())
    {
        pks.push_back(CCNewSecp256k1(pk));
    }
    else
    {
        pks.push_back(CCNewHashedSecp256k1(CKeyID(GetDestinationID(dest))));
    }
    CC *condCC = CCNewEval(E_MARSHAL(ss << evalcode));
    CC *Sig = CCNewThreshold(1, pks);
    return CCNewThreshold(2, {condCC, Sig});
}

CC *CCcondAny(uint8_t evalcode, std::vector<CTxDestination> dests)
{
    std::vector<CC*> pks;
    for (auto dest : dests)
    {
        CPubKey pk = boost::apply_visitor<GetPubKeyForPubKey>(GetPubKeyForPubKey(), dest);
        if (pk.IsValid())
        {
            pks.push_back(CCNewSecp256k1(pk));
        }
        else
        {
            pks.push_back(CCNewHashedSecp256k1(CKeyID(GetDestinationID(dest))));
        }
    }

    CC *condCC = CCNewEval(E_MARSHAL(ss << evalcode));
    CC *Sig = CCNewThreshold(1, pks);
    return CCNewThreshold(2, {condCC, Sig});
}

CScript _CCPubKey(const CC *cond)
{
    unsigned char buf[1000];
    size_t len = cc_conditionBinary(cond, buf);
    return CScript() << std::vector<unsigned char>(buf, buf+len) << OP_CHECKCRYPTOCONDITION;
}

static bool SignStepCC(const BaseSignatureCreator& creator, const CScript& scriptPubKey, vector<valtype> &vSolutions,
                       vector<valtype>& ret, uint32_t consensusBranchId)
{
    CScript subScript;
    vector<CTxDestination> vPK;
    vector<valtype> vParams = vector<valtype>();
    COptCCParams p;

    // get information to sign with
    CCcontract_info C;

    if (scriptPubKey.IsPayToCryptoCondition(p) && p.IsValid() && p.n >= 1 && p.vKeys.size() >= p.n)
    {
        bool is0ofAny = (p.m == 0 && p.n >= 1);
        bool is1ofn = (p.m == 1 && p.n >= 2);
        CKey privKey;

        // must be a valid cc eval code
        if (CCinit(&C, p.evalCode))
        {
            // pay to cc address is a valid tx
            if (is0ofAny)
            {
                CPubKey pubk = CPubKey(ParseHex(C.CChexstr));
                CKeyID keyID = pubk.GetID();
                bool havePriv = false;

                // loop through and sign with the first of either the private key or a match on the CCs private key, otherwise, fail
                for (auto dest : p.vKeys)
                {
                    uint160 keyID = GetDestinationID(dest);
                    if (creator.IsKeystoreValid() && creator.KeyStore().GetKey(keyID, privKey))
                    {
                        havePriv = true;
                        break;
                    }
                    CPubKey tempPub = boost::apply_visitor<GetPubKeyForPubKey>(GetPubKeyForPubKey(), dest);
                    if ((tempPub.IsValid() && tempPub == pubk) || (keyID == tempPub.GetID()))
                    {
                        // found the pub key for this crypto condition, so use the private key
                        std::vector<unsigned char> vch(&(C.CCpriv[0]), C.CCpriv + sizeof(C.CCpriv));
                        privKey.Set(vch.begin(), vch.end(), true);
                        havePriv = true;
                        break;
                    }
                }

                if (!havePriv)
                {
                    fprintf(stderr,"Do not have or cannot locate private key for %s\n", EncodeDestination(p.vKeys[0]).c_str());
                    return false;
                }

                CC *cc = CCcondAny(p.evalCode, p.vKeys);

                if (cc)
                {
                    vector<unsigned char> vch;
                    if (creator.CreateSig(vch, GetDestinationID(p.vKeys[0]), _CCPubKey(cc), consensusBranchId, &privKey, (void *)cc))
                    {
                        ret.push_back(vch);
                    }
                    else
                    {
                        fprintf(stderr,"vin has 1ofAny CC signing error with address.(%s)\n", EncodeDestination(p.vKeys[0]).c_str());
                    }

                    cc_free(cc);
                    return ret.size() != 0;
                }
            }
            else if (!is1ofn)
            {
                uint160 keyID = GetDestinationID(p.vKeys[0]);
                bool havePriv = creator.IsKeystoreValid() && creator.KeyStore().GetKey(keyID, privKey);
                CPubKey pubk;

                // if we don't have the private key, it must be the unspendable address
                if (havePriv)
                {
                    std::vector<unsigned char> vkch = GetDestinationBytes(p.vKeys[0]);
                    if (vkch.size() == 33)
                    {
                        pubk = CPubKey(vkch);
                    }
                    else
                    {
                        creator.KeyStore().GetPubKey(keyID, pubk);
                    }
                }
                else
                {
                    privKey = CKey();
                    std::vector<unsigned char> vch(&(C.CCpriv[0]), C.CCpriv + sizeof(C.CCpriv));

                    privKey.Set(vch.begin(), vch.end(), true);
                    pubk = CPubKey(ParseHex(C.CChexstr));
                }

                CC *cc = CCcond1(p.evalCode, pubk);

                if (cc)
                {
                    vector<unsigned char> vch;
                    if (creator.CreateSig(vch, GetDestinationID(p.vKeys[0]), _CCPubKey(cc), consensusBranchId, &privKey, (void *)cc))
                    {
                        ret.push_back(vch);
                    }
                    else
                    {
                        fprintf(stderr,"vin has 1of1 CC signing error with address.(%s)\n", keyID.ToString().c_str());
                    }

                    cc_free(cc);
                    return ret.size() != 0;
                }
            }
            else
            {
                // first of priv key in our key store or contract address is what we sign with if we have it
                std::vector<CPubKey> keys;
                for (auto pk : p.vKeys)
                {
                    uint160 keyID = GetDestinationID(pk);
                    CPubKey foundKey;
                    if (!(creator.IsKeystoreValid() && creator.KeyStore().GetPubKey(keyID, foundKey)))
                    {
                        std::vector<unsigned char> vkch = GetDestinationBytes(pk);
                        if (vkch.size() == 33)
                        {
                            foundKey = CPubKey(vkch);
                        }
                    }

                    if (foundKey.IsFullyValid())
                    {
                        keys.push_back(foundKey);
                    }
                }

                // if we only have one key, and this is version 2, add the cc pub key
                if (keys.size() <= 1 && p.version == p.VERSION_V2)
                {
                    keys.push_back(CPubKey(ParseHex(C.CChexstr)));
                }

                // we need something to sign with
                if (!keys.size())
                {
                    return false;
                }

                for (auto pk : keys)
                {
                    if (creator.IsKeystoreValid() && creator.KeyStore().GetKey(pk.GetID(), privKey) && privKey.IsValid())
                    {
                        break;
                    }

                    if (pk == CPubKey(ParseHex(C.CChexstr)))
                    {
                        privKey = CKey();
                        std::vector<unsigned char> vch(&(C.CCpriv[0]), C.CCpriv + sizeof(C.CCpriv));
                        privKey.Set(vch.begin(), vch.end(), true);
                        break;
                    }
                }

                if (!privKey.IsValid())
                    return false;

                CC *cc;
                if (keys.size() > 1)
                {
                    cc = CCcond1of2(p.evalCode, keys[0], keys[1]);
                }
                else
                {
                    cc = CCcond1(p.evalCode, keys[0]);
                }

                if (cc)
                {
                    vector<unsigned char> vch;
                    if (creator.CreateSig(vch, keys[0].GetID(), _CCPubKey(cc), consensusBranchId, &privKey, (void *)cc))
                    {
                        ret.push_back(vch);
                    }
                    else
                    {
                        fprintf(stderr,"vin has 1ofn CC signing error with addresses.(%s)\n(%s)\n", keys[0].GetID().ToString().c_str(), keys[1].GetID().ToString().c_str());
                    }

                    cc_free(cc);
                    return ret.size() != 0;
                }
            }
        }
    }
    return false;
}

/**
 * Sign scriptPubKey using signature made with creator.
 * Signatures are returned in scriptSigRet (or returns false if scriptPubKey can't be signed),
 * unless whichTypeRet is TX_SCRIPTHASH, in which case scriptSigRet is the redemption script.
 * Returns false if scriptPubKey could not be completely satisfied.
 */
static bool SignStep(const BaseSignatureCreator& creator, const CScript& scriptPubKey,
                     std::vector<valtype>& ret, txnouttype& whichTypeRet, uint32_t consensusBranchId)
{
    CScript scriptRet;
    uint160 h160;
    ret.clear();

    vector<valtype> vSolutions;

    if (!Solver(scriptPubKey, whichTypeRet, vSolutions))
    {
        // if this is a CLTV script, solve for the destination after CLTV
        if (scriptPubKey.IsCheckLockTimeVerify())
        {
            uint8_t pushOp = scriptPubKey[0];
            uint32_t scriptStart = pushOp + 3;

            // check post CLTV script
            CScript postfix = CScript(scriptPubKey.size() > scriptStart ? scriptPubKey.begin() + scriptStart : scriptPubKey.end(), scriptPubKey.end());

            // check again with only postfix subscript
            if (!Solver(postfix, whichTypeRet, vSolutions))
                return false;
        }
        else
            return false;
    }

    CKeyID keyID;

    switch (whichTypeRet)
    {
    case TX_NONSTANDARD:
    case TX_NULL_DATA:
        return false;
    case TX_PUBKEY:
        keyID = CPubKey(vSolutions[0]).GetID();
        return Sign1(keyID, creator, scriptPubKey, ret, consensusBranchId);
    case TX_PUBKEYHASH:
        keyID = CKeyID(uint160(vSolutions[0]));
        if (!Sign1(keyID, creator, scriptPubKey, ret, consensusBranchId))
            return false;
        else
        {
            CPubKey vch;
            creator.KeyStore().GetPubKey(keyID, vch);
            ret.push_back(ToByteVector(vch));
        }
        return true;
    case TX_SCRIPTHASH:
        if (creator.KeyStore().GetCScript(uint160(vSolutions[0]), scriptRet)) {
            ret.push_back(std::vector<unsigned char>(scriptRet.begin(), scriptRet.end()));
            return true;
        }
        return false;
    
    case TX_CRYPTOCONDITION:
        return SignStepCC(creator, scriptPubKey, vSolutions, ret, consensusBranchId);

    case TX_MULTISIG:
        ret.push_back(valtype()); // workaround CHECKMULTISIG bug
        return (SignN(vSolutions, creator, scriptPubKey, ret, consensusBranchId));

    default:
        return false;
    }
}

static CScript PushAll(const vector<valtype>& values)
{
    CScript result;
    BOOST_FOREACH(const valtype& v, values) {
        if (v.size() == 0) {
            result << OP_0;
        } else if (v.size() == 1 && v[0] >= 1 && v[0] <= 16) {
            result << CScript::EncodeOP_N(v[0]);
        } else {
            result << v;
        }
    }
    return result;
}

bool ProduceSignature(const BaseSignatureCreator& creator, const CScript& fromPubKey, SignatureData& sigdata, uint32_t consensusBranchId)
{
    CScript script = fromPubKey;
    bool solved;
    std::vector<valtype> result;
    txnouttype whichType;
    solved = SignStep(creator, script, result, whichType, consensusBranchId);
    CScript subscript;

    if (solved && whichType == TX_SCRIPTHASH)
    {
        // Solver returns the subscript that needs to be evaluated;
        // the final scriptSig is the signatures from that
        // and then the serialized subscript:
        script = subscript = CScript(result[0].begin(), result[0].end());
        solved = solved && SignStep(creator, script, result, whichType, consensusBranchId) && whichType != TX_SCRIPTHASH;
        result.push_back(std::vector<unsigned char>(subscript.begin(), subscript.end()));
    }

    sigdata.scriptSig = PushAll(result);

    // Test solution
    return solved && VerifyScript(sigdata.scriptSig, fromPubKey, STANDARD_SCRIPT_VERIFY_FLAGS, creator.Checker(), consensusBranchId);
}

SignatureData DataFromTransaction(const CMutableTransaction& tx, unsigned int nIn)
{
    SignatureData data;
    assert(tx.vin.size() > nIn);
    data.scriptSig = tx.vin[nIn].scriptSig;
    return data;
}

void UpdateTransaction(CMutableTransaction& tx, unsigned int nIn, const SignatureData& data)
{
    assert(tx.vin.size() > nIn);
    tx.vin[nIn].scriptSig = data.scriptSig;
}

bool SignSignature(
    const CKeyStore &keystore,
    const CScript& fromPubKey,
    CMutableTransaction& txTo,
    unsigned int nIn,
    const CAmount& amount,
    int nHashType,
    uint32_t consensusBranchId)
{
    assert(nIn < txTo.vin.size());

    CTransaction txToConst(txTo);
    TransactionSignatureCreator creator(&keystore, &txToConst, nIn, amount, nHashType);

    SignatureData sigdata;
    bool ret = ProduceSignature(creator, fromPubKey, sigdata, consensusBranchId);
    UpdateTransaction(txTo, nIn, sigdata);
    return ret;
}

bool SignSignature(
    const CKeyStore &keystore,
    const CTransaction& txFrom,
    CMutableTransaction& txTo,
    unsigned int nIn,
    int nHashType,
    uint32_t consensusBranchId)
{
    assert(nIn < txTo.vin.size());
    CTxIn& txin = txTo.vin[nIn];
    assert(txin.prevout.n < txFrom.vout.size());
    const CTxOut& txout = txFrom.vout[txin.prevout.n];

    return SignSignature(keystore, txout.scriptPubKey, txTo, nIn, txout.nValue, nHashType, consensusBranchId);
}

static vector<valtype> CombineMultisig(const CScript& scriptPubKey, const BaseSignatureChecker& checker,
                               const vector<valtype>& vSolutions,
                               const vector<valtype>& sigs1, const vector<valtype>& sigs2, uint32_t consensusBranchId)
{
    // Combine all the signatures we've got:
    set<valtype> allsigs;
    BOOST_FOREACH(const valtype& v, sigs1)
    {
        if (!v.empty())
            allsigs.insert(v);
    }
    BOOST_FOREACH(const valtype& v, sigs2)
    {
        if (!v.empty())
            allsigs.insert(v);
    }

    // Build a map of pubkey -> signature by matching sigs to pubkeys:
    assert(vSolutions.size() > 1);
    unsigned int nSigsRequired = vSolutions.front()[0];
    unsigned int nPubKeys = vSolutions.size()-2;
    map<valtype, valtype> sigs;
    BOOST_FOREACH(const valtype& sig, allsigs)
    {
        for (unsigned int i = 0; i < nPubKeys; i++)
        {
            const valtype& pubkey = vSolutions[i+1];
            if (sigs.count(pubkey))
                continue; // Already got a sig for this pubkey

            if (checker.CheckSig(sig, pubkey, scriptPubKey, consensusBranchId))
            {
                sigs[pubkey] = sig;
                break;
            }
        }
    }
    // Now build a merged CScript:
    unsigned int nSigsHave = 0;
    std::vector<valtype> result; result.push_back(valtype()); // pop-one-too-many workaround
    for (unsigned int i = 0; i < nPubKeys && nSigsHave < nSigsRequired; i++)
    {
        if (sigs.count(vSolutions[i+1]))
        {
            result.push_back(sigs[vSolutions[i+1]]);
            ++nSigsHave;
        }
    }
    // Fill any missing with OP_0:
    for (unsigned int i = nSigsHave; i < nSigsRequired; i++)
        result.push_back(valtype());

    return result;
}

namespace
{
struct Stacks
{
    std::vector<valtype> script;

    Stacks() {}
    explicit Stacks(const std::vector<valtype>& scriptSigStack_) : script(scriptSigStack_) {}
    explicit Stacks(const SignatureData& data, uint32_t consensusBranchId) {
        EvalScript(script, data.scriptSig, SCRIPT_VERIFY_STRICTENC, BaseSignatureChecker(), consensusBranchId);
    }

    SignatureData Output() const {
        SignatureData result;
        result.scriptSig = PushAll(script);
        return result;
    }
};
}

static Stacks CombineSignatures(const CScript& scriptPubKey, const BaseSignatureChecker& checker,
                                 const txnouttype txType, const vector<valtype>& vSolutions,
                                 Stacks sigs1, Stacks sigs2, uint32_t consensusBranchId)
{
    switch (txType)
    {
    case TX_NONSTANDARD:
    case TX_NULL_DATA:
        // Don't know anything about this, assume bigger one is correct:
        if (sigs1.script.size() >= sigs2.script.size())
            return sigs1;
        return sigs2;
    case TX_PUBKEY:
    case TX_PUBKEYHASH:
    case TX_CRYPTOCONDITION:
        // Signatures are bigger than placeholders or empty scripts:
        if (sigs1.script.empty() || sigs1.script[0].empty())
            return sigs2;
        return sigs1;
    case TX_SCRIPTHASH:
        if (sigs1.script.empty() || sigs1.script.back().empty())
            return sigs2;
        else if (sigs2.script.empty() || sigs2.script.back().empty())
            return sigs1;
        else
        {
            // Recur to combine:
            valtype spk = sigs1.script.back();
            CScript pubKey2(spk.begin(), spk.end());

            txnouttype txType2;
            vector<vector<unsigned char> > vSolutions2;
            Solver(pubKey2, txType2, vSolutions2);
            sigs1.script.pop_back();
            sigs2.script.pop_back();
            Stacks result = CombineSignatures(pubKey2, checker, txType2, vSolutions2, sigs1, sigs2, consensusBranchId);
            result.script.push_back(spk);
            return result;
        }
    case TX_MULTISIG:
        return Stacks(CombineMultisig(scriptPubKey, checker, vSolutions, sigs1.script, sigs2.script, consensusBranchId));
    default:
        return Stacks();
    }
}

SignatureData CombineSignatures(const CScript& scriptPubKey, const BaseSignatureChecker& checker,
                          const SignatureData& scriptSig1, const SignatureData& scriptSig2,
                          uint32_t consensusBranchId)
{
    txnouttype txType;
    vector<vector<unsigned char> > vSolutions;
    Solver(scriptPubKey, txType, vSolutions);

    return CombineSignatures(
        scriptPubKey, checker, txType, vSolutions,
        Stacks(scriptSig1, consensusBranchId),
        Stacks(scriptSig2, consensusBranchId),
        consensusBranchId).Output();
}

namespace {
/** Dummy signature checker which accepts all signatures. */
class DummySignatureChecker : public BaseSignatureChecker
{
public:
    DummySignatureChecker() {}

    bool CheckSig(
        const std::vector<unsigned char>& scriptSig,
        const std::vector<unsigned char>& vchPubKey,
        const CScript& scriptCode,
        uint32_t consensusBranchId) const
    {
        return true;
    }
};
const DummySignatureChecker dummyChecker;
}

const BaseSignatureChecker& DummySignatureCreator::Checker() const
{
    return dummyChecker;
}

bool DummySignatureCreator::CreateSig(
    std::vector<unsigned char>& vchSig,
    const CKeyID& keyid,
    const CScript& scriptCode,
    uint32_t consensusBranchId, 
    CKey *key,
    void *extraData) const
{
    // Create a dummy signature that is a valid DER-encoding
    vchSig.assign(72, '\000');
    vchSig[0] = 0x30;
    vchSig[1] = 69;
    vchSig[2] = 0x02;
    vchSig[3] = 33;
    vchSig[4] = 0x01;
    vchSig[4 + 33] = 0x02;
    vchSig[5 + 33] = 32;
    vchSig[6 + 33] = 0x01;
    vchSig[6 + 33 + 32] = SIGHASH_ALL;
    return true;
}
