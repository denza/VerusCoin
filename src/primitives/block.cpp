// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#include "primitives/block.h"

#include "hash.h"
#include "tinyformat.h"
#include "utilstrencodings.h"
#include "crypto/common.h"
#include "mmr.h"

extern uint32_t ASSETCHAINS_ALGO, ASSETCHAINS_VERUSHASH;
extern uint160 ASSETCHAINS_CHAINID;
extern uint160 VERUS_CHAINID;

// default hash algorithm for block
uint256 (CBlockHeader::*CBlockHeader::hashFunction)() const = &CBlockHeader::GetSHA256DHash;

// does not check for height / sapling upgrade, etc. this should not be used to get block proofs
// on a pre-VerusPoP chain
arith_uint256 GetCompactPower(const uint256 &nNonce, uint32_t nBits, int32_t version)
{
    arith_uint256 bnWork, bnStake = arith_uint256(0);
    arith_uint256 BIG_ZERO = bnStake;

    bool fNegative;
    bool fOverflow;
    bnWork.SetCompact(nBits, &fNegative, &fOverflow);

    if (fNegative || fOverflow || bnWork == 0)
        return BIG_ZERO;

    // if POS block, add stake
    CPOSNonce nonce(nNonce);
    if (nonce.IsPOSNonce(version))
    {
        bnStake.SetCompact(nonce.GetPOSTarget(), &fNegative, &fOverflow);
        if (fNegative || fOverflow || bnStake == 0)
            return BIG_ZERO;

        // as the nonce has a fixed definition for a POS block, add the random amount of "work" from the nonce, so there will
        // statistically always be a deterministic winner in POS
        arith_uint256 aNonce;

        // random amount of additional stake added is capped to 1/2 the current stake target
        aNonce = UintToArith256(nNonce) | (bnStake << (uint64_t)1);

        // We need to compute 2**256 / (bnTarget+1), but we can't represent 2**256
        // as it's too large for a arith_uint256. However, as 2**256 is at least as large
        // as bnTarget+1, it is equal to ((2**256 - bnTarget - 1) / (bnTarget+1)) + 1,
        // or ~bnTarget / (nTarget+1) + 1.
        bnWork = (~bnWork / (bnWork + 1)) + 1;
        bnStake = ((~bnStake / (bnStake + 1)) + 1) + ((~aNonce / (aNonce + 1)) + 1);
        if (!(bnWork >> 128 == BIG_ZERO && bnStake >> 128 == BIG_ZERO))
        {
            return BIG_ZERO;
        }
        return bnWork + (bnStake << 128);
    }
    else
    {
        bnWork = (~bnWork / (bnWork + 1)) + 1;

        // this would be overflow
        if (!((bnWork >> 128) == BIG_ZERO))
        {
            printf("Overflow\n");
            return BIG_ZERO;
        }
        return bnWork;
    }
}

CPBaaSPreHeader::CPBaaSPreHeader(const CBlockHeader &bh)
{
    hashPrevBlock = bh.hashPrevBlock;
    hashMerkleRoot = bh.hashMerkleRoot;
    hashFinalSaplingRoot = bh.hashFinalSaplingRoot;
    nNonce = bh.nNonce;
    nBits = bh.nBits;
}

CMMRPowerNode CBlockHeader::GetMMRNode() const
{
    uint256 blockHash = GetHash();

    uint256 preHash = Hash(BEGIN(hashMerkleRoot), END(hashMerkleRoot), BEGIN(blockHash), END(blockHash));
    uint256 power = ArithToUint256(GetCompactPower(nNonce, nBits, nVersion));

    return CMMRPowerNode(Hash(BEGIN(preHash), END(preHash), BEGIN(power), END(power)), power);
}

void CBlockHeader::AddMerkleProofBridge(CMerkleBranch &branch) const
{
    // we need to add the block hash on the right
    branch.branch.push_back(GetHash());
    branch.nIndex = branch.nIndex << 1;
}

void CBlockHeader::AddBlockProofBridge(CMerkleBranch &branch) const
{
    // we need to add the merkle root on the left
    branch.branch.push_back(hashMerkleRoot);
    branch.nIndex = branch.nIndex << 1 + 1;
}

uint256 CBlockHeader::GetPrevMMRRoot() const
{
    uint256 ret;
    CPBaaSBlockHeader pbh;
    if (GetPBaaSHeader(pbh, ASSETCHAINS_CHAINID) != -1)
    {
        ret = pbh.hashPrevMMRRoot;
    }
    return ret;
}

// checks that the solution stored data for this header matches what is expected, ensuring that the
// values in the header match the hash of the pre-header. it does not check the prev MMR root
bool CBlockHeader::CheckNonCanonicalData() const
{
    CPBaaSPreHeader pbph(hashPrevBlock, hashMerkleRoot, hashFinalSaplingRoot, nNonce, nBits);
    uint256 dummyMMR;
    CPBaaSBlockHeader pbbh1 = CPBaaSBlockHeader(ASSETCHAINS_CHAINID, pbph, dummyMMR);
    CPBaaSBlockHeader pbbh2;
    int32_t idx = GetPBaaSHeader(pbbh2, ASSETCHAINS_CHAINID);
    if (idx != -1)
    {
        if (pbbh1.hashPreHeader == pbbh2.hashPreHeader)
        {
            return true;
        }
    }
    return false;
}

// checks that the solution stored data for this header matches what is expected, ensuring that the
// values in the header match the hash of the pre-header. it does not check the prev MMR root
bool CBlockHeader::CheckNonCanonicalData(uint160 &cID) const
{
    CPBaaSPreHeader pbph(hashPrevBlock, hashMerkleRoot, hashFinalSaplingRoot, nNonce, nBits);
    uint256 dummyMMR;
    CPBaaSBlockHeader pbbh1 = CPBaaSBlockHeader(cID, pbph, dummyMMR);
    CPBaaSBlockHeader pbbh2;
    int32_t idx = GetPBaaSHeader(pbbh2, cID);
    if (idx != -1)
    {
        if (pbbh1.hashPreHeader == pbbh2.hashPreHeader)
        {
            return true;
        }
    }
    return false;
}

// returns -1 on failure, upon failure, pbbh is undefined and likely corrupted
int32_t CBlockHeader::GetPBaaSHeader(CPBaaSBlockHeader &pbh, const uint160 &cID) const
{
    // find the specified PBaaS header in the solution and return its index if present
    // if not present, return -1
    if (nVersion == VERUS_V2)
    {
        // search in the solution for this header index and return it if found
        CPBaaSSolutionDescriptor d = CVerusSolutionVector::solutionTools.GetDescriptor(nSolution);
        if (CVerusSolutionVector::solutionTools.IsPBaaS(nSolution) != 0)
        {
            int32_t len = CVerusSolutionVector::solutionTools.ExtraDataLen(nSolution);
            int32_t numHeaders = d.numPBaaSHeaders;
            const CPBaaSBlockHeader *ppbbh = CVerusSolutionVector::solutionTools.GetFirstPBaaSHeader(nSolution);
            for (int32_t i = 0; i < numHeaders; i++)
            {
                if ((ppbbh + i)->chainID == cID)
                {
                    pbh = *(ppbbh + i);
                    return i;
                }
            }
        }
    }
    return -1;
}

// returns the index of the new header if added, otherwise, -1
int32_t CBlockHeader::AddPBaaSHeader(const CPBaaSBlockHeader &pbh)
{
    CVerusSolutionVector sv(nSolution);
    CPBaaSSolutionDescriptor d = sv.Descriptor();
    int32_t retVal = d.numPBaaSHeaders;

    // make sure we have space. do not adjust capacity
    // if there is anything in the extradata, we have no more room
    if (!d.extraDataSize && (uint32_t)(sv.ExtraDataLen() / sizeof(CPBaaSBlockHeader)) > 0)
    {
        d.numPBaaSHeaders++;
        sv.SetDescriptor(d);                            // update descriptor to make sure it will accept the set
        sv.SetPBaaSHeader(pbh, d.numPBaaSHeaders - 1);
        return retVal;
    }

    return -1;
}

// add or update the PBaaS header for this block from the current block header & this prevMMR. This is required to make a valid PoS or PoW block.
bool CBlockHeader::AddUpdatePBaaSHeader(const CPBaaSBlockHeader &pbh)
{
    CPBaaSBlockHeader pbbh;
    if (CConstVerusSolutionVector::Version(nSolution) == CActivationHeight::SOLUTION_VERUSV3)
    {
        if (int32_t idx = GetPBaaSHeader(pbbh, pbh.chainID) != -1)
        {
            return UpdatePBaaSHeader(pbh);
        }
        else
        {
            return (AddPBaaSHeader(pbh) != -1);
        }
    }
    return false;
}

// add or update the current PBaaS header for this block from the current block header & this prevMMR.
// This is required to make a valid PoS or PoW block.
bool CBlockHeader::AddUpdatePBaaSHeader(uint256 prevMMRRoot)
{
    if (CConstVerusSolutionVector::Version(nSolution) == CActivationHeight::SOLUTION_VERUSV3)
    {
        CPBaaSPreHeader pbph(hashPrevBlock, hashMerkleRoot, hashFinalSaplingRoot, nNonce, nBits);
        CPBaaSBlockHeader pbh(ASSETCHAINS_CHAINID, pbph, prevMMRRoot);

        CPBaaSBlockHeader pbbh;
        int32_t idx = GetPBaaSHeader(pbbh, ASSETCHAINS_CHAINID);

        if (idx != -1)
        {
            return UpdatePBaaSHeader(pbh);
        }
        else
        {
            return (AddPBaaSHeader(pbh) != -1);
        }
    }
    return false;
}

uint256 CBlockHeader::GetSHA256DHash() const
{
    return SerializeHash(*this);
}

uint256 CBlockHeader::GetVerusHash() const
{
    if (hashPrevBlock.IsNull())
        // always use SHA256D for genesis block
        return SerializeHash(*this);
    else
        return SerializeVerusHash(*this);
}

uint256 CBlockHeader::GetVerusV2Hash() const
{
    if (hashPrevBlock.IsNull())
    {
        // always use SHA256D for genesis block
        return SerializeHash(*this);
    }
    else
    {
        if (nVersion == VERUS_V2)
        {
            // in order for this to work, the PBaaS hash of the pre-header must match the header data
            // otherwise, it cannot clear the canonical data and hash in a chain-independent manner
            if (CConstVerusSolutionVector::IsPBaaS(nSolution) && CheckNonCanonicalData())
            {
                CBlockHeader bh = CBlockHeader(*this);
                bh.ClearNonCanonicalData();
                return SerializeVerusHashV2b(bh);
            }
            else
            {
                return SerializeVerusHashV2b(*this);
            }
        }
        else
        {
            return SerializeVerusHash(*this);
        }
    }
}

void CBlockHeader::SetSHA256DHash()
{
    CBlockHeader::hashFunction = &CBlockHeader::GetSHA256DHash;
}

void CBlockHeader::SetVerusHash()
{
    CBlockHeader::hashFunction = &CBlockHeader::GetVerusHash;
}

void CBlockHeader::SetVerusV2Hash()
{
    CBlockHeader::hashFunction = &CBlockHeader::GetVerusV2Hash;
}

// returns false if unable to fast calculate the VerusPOSHash from the header. 
// if it returns false, value is set to 0, but it can still be calculated from the full block
// in that case. the only difference between this and the POS hash for the contest is that it is not divided by the value out
// this is used as a source of entropy
bool CBlockHeader::GetRawVerusPOSHash(uint256 &ret, int32_t nHeight) const
{
    // if below the required height or no storage space in the solution, we can't get
    // a cached txid value to calculate the POSHash from the header
    if (!(CPOSNonce::NewNonceActive(nHeight) && IsVerusPOSBlock()))
    {
        ret = uint256();
        return false;
    }

    // if we can calculate, this assumes the protocol that the POSHash calculation is:
    //    hashWriter << ASSETCHAINS_MAGIC;
    //    hashWriter << nNonce; (nNonce is:
    //                           (high 128 bits == low 128 bits of verus hash of low 128 bits of nonce)
    //                           (low 32 bits == compact PoS difficult)
    //                           (mid 96 bits == low 96 bits of HASH(pastHash, txid, voutnum)
    //                              pastHash is hash of height - 100, either PoW hash of block or PoS hash, if new PoS
    //                          )
    //    hashWriter << height;
    //    return hashWriter.GetHash();
    if (nVersion == VERUS_V2)
    {
        CVerusHashV2Writer hashWriter = CVerusHashV2Writer(SER_GETHASH, PROTOCOL_VERSION);

        hashWriter << ASSETCHAINS_MAGIC;
        hashWriter << nNonce;
        hashWriter << nHeight;
        ret = hashWriter.GetHash();
    }
    else
    {
        CVerusHashWriter hashWriter = CVerusHashWriter(SER_GETHASH, PROTOCOL_VERSION);

        hashWriter << ASSETCHAINS_MAGIC;
        hashWriter << nNonce;
        hashWriter << nHeight;
        ret = hashWriter.GetHash();
    }
    return true;
}

bool CBlockHeader::GetVerusPOSHash(arith_uint256 &ret, int32_t nHeight, CAmount value) const
{
    uint256 raw;
    if (GetRawVerusPOSHash(raw, nHeight))
    {
        ret = UintToArith256(raw) / value;
        return true;
    }
    return false;
}

// depending on the height of the block and its type, this returns the POS hash or the POW hash
uint256 CBlockHeader::GetVerusEntropyHash(int32_t height) const
{
    uint256 retVal;
    // if we qualify as PoW, use PoW hash, regardless of PoS state
    if (GetRawVerusPOSHash(retVal, height))
    {
        // POS hash
        return retVal;
    }
    return GetHash();
}

uint256 BuildMerkleTree(bool* fMutated, const std::vector<uint256> leaves,
        std::vector<uint256> &vMerkleTree)
{
    /* WARNING! If you're reading this because you're learning about crypto
       and/or designing a new system that will use merkle trees, keep in mind
       that the following merkle tree algorithm has a serious flaw related to
       duplicate txids, resulting in a vulnerability (CVE-2012-2459).

       The reason is that if the number of hashes in the list at a given time
       is odd, the last one is duplicated before computing the next level (which
       is unusual in Merkle trees). This results in certain sequences of
       transactions leading to the same merkle root. For example, these two
       trees:

                   A                A
                 /  \            /    \
                B    C          B       C
               / \    \        / \     / \
              D   E   F       D   E   F   F
             / \ / \ / \     / \ / \ / \ / \
             1 2 3 4 5 6     1 2 3 4 5 6 5 6

       for transaction lists [1,2,3,4,5,6] and [1,2,3,4,5,6,5,6] (where 5 and
       6 are repeated) result in the same root hash A (because the hash of both
       of (F) and (F,F) is C).

       The vulnerability results from being able to send a block with such a
       transaction list, with the same merkle root, and the same block hash as
       the original without duplication, resulting in failed validation. If the
       receiving node proceeds to mark that block as permanently invalid
       however, it will fail to accept further unmodified (and thus potentially
       valid) versions of the same block. We defend against this by detecting
       the case where we would hash two identical hashes at the end of the list
       together, and treating that identically to the block having an invalid
       merkle root. Assuming no double-SHA256 collisions, this will detect all
       known ways of changing the transactions without affecting the merkle
       root.
    */

    vMerkleTree.clear();
    vMerkleTree.reserve(leaves.size() * 2 + 16); // Safe upper bound for the number of total nodes.
    for (std::vector<uint256>::const_iterator it(leaves.begin()); it != leaves.end(); ++it)
        vMerkleTree.push_back(*it);
    int j = 0;
    bool mutated = false;
    for (int nSize = leaves.size(); nSize > 1; nSize = (nSize + 1) / 2)
    {
        for (int i = 0; i < nSize; i += 2)
        {
            int i2 = std::min(i+1, nSize-1);
            if (i2 == i + 1 && i2 + 1 == nSize && vMerkleTree[j+i] == vMerkleTree[j+i2]) {
                // Two identical hashes at the end of the list at a particular level.
                mutated = true;
            }
            vMerkleTree.push_back(Hash(BEGIN(vMerkleTree[j+i]),  END(vMerkleTree[j+i]),
                                       BEGIN(vMerkleTree[j+i2]), END(vMerkleTree[j+i2])));
        }
        j += nSize;
    }
    if (fMutated) {
        *fMutated = mutated;
    }
    return (vMerkleTree.empty() ? uint256() : vMerkleTree.back());
}


uint256 CBlock::BuildMerkleTree(bool* fMutated) const
{
    std::vector<uint256> leaves;
    for (int i=0; i<vtx.size(); i++) leaves.push_back(vtx[i].GetHash());
    return ::BuildMerkleTree(fMutated, leaves, vMerkleTree);
}


std::vector<uint256> GetMerkleBranch(int nIndex, int nLeaves, const std::vector<uint256> &vMerkleTree)
{
    std::vector<uint256> vMerkleBranch;
    int j = 0;
    for (int nSize = nLeaves; nSize > 1; nSize = (nSize + 1) / 2)
    {
        int i = std::min(nIndex^1, nSize-1);
        vMerkleBranch.push_back(vMerkleTree[j+i]);
        nIndex >>= 1;
        j += nSize;
    }
    return vMerkleBranch;
}


std::vector<uint256> CBlock::GetMerkleBranch(int nIndex) const
{
    if (vMerkleTree.empty())
        BuildMerkleTree();
    return ::GetMerkleBranch(nIndex, vtx.size(), vMerkleTree);
}


uint256 CBlock::CheckMerkleBranch(uint256 hash, const std::vector<uint256>& vMerkleBranch, int nIndex)
{
    if (nIndex == -1)
        return uint256();
    for (std::vector<uint256>::const_iterator it(vMerkleBranch.begin()); it != vMerkleBranch.end(); ++it)
    {
        if (nIndex & 1)
            hash = Hash(BEGIN(*it), END(*it), BEGIN(hash), END(hash));
        else
            hash = Hash(BEGIN(hash), END(hash), BEGIN(*it), END(*it));
        nIndex >>= 1;
    }
    return hash;
}

std::string CBlock::ToString() const
{
    std::stringstream s;
    s << strprintf("CBlock(hash=%s, ver=%d, hashPrevBlock=%s, hashMerkleRoot=%s, hashFinalSaplingRoot=%s, nTime=%u, nBits=%08x, nNonce=%s, vtx=%u)\n",
        GetHash().ToString(),
        nVersion,
        hashPrevBlock.ToString(),
        hashMerkleRoot.ToString(),
        hashFinalSaplingRoot.ToString(),
        nTime, nBits, nNonce.ToString(),
        vtx.size());
    for (unsigned int i = 0; i < vtx.size(); i++)
    {
        s << "  " << vtx[i].ToString() << "\n";
    }
    s << "  vMerkleTree: ";
    for (unsigned int i = 0; i < vMerkleTree.size(); i++)
        s << " " << vMerkleTree[i].ToString();
    s << "\n";
    return s.str();
}
