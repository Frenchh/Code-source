// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef FRENCHNODE_PAYMENTS_H
#define FRENCHNODE_PAYMENTS_H

#include "key.h"
#include "main.h"
#include "masternode.h"
#include "clientversion.h"

#include <boost/lexical_cast.hpp>

using namespace std;

extern CCriticalSection cs_vecPayments;
extern CCriticalSection cs_mapFrenchnodeBlocks;
extern CCriticalSection cs_mapFrenchnodePayeeVotes;

class CFrenchnodePayments;
class CFrenchnodePaymentWinner;
class CFrenchnodeBlockPayees;

extern CFrenchnodePayments masternodePayments;

#define MNPAYMENTS_SIGNATURES_REQUIRED 7
#define MNPAYMENTS_SIGNATURES_TOTAL 10

void ProcessMessageFrenchnodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
bool IsBlockPayeeValid(const CBlock& block, int nBlockHeight);
std::string GetRequiredPaymentsString(int nBlockHeight);
bool IsBlockValueValid(const CBlock& block, CAmount nExpectedValue, CAmount nMinted);
void FillBlockPayee(CMutableTransaction& txNew, CAmount nFees, bool fProofOfStake);

void DumpFrenchnodePayments();

/** Save Frenchnode Payment Data (mnpayments.dat)
 */
class CFrenchnodePaymentDB
{
private:
    boost::filesystem::path pathDB;
    std::string strMagicMessage;

public:
    enum ReadResult {
        Ok,
        FileError,
        HashReadError,
        IncorrectHash,
        IncorrectMagicMessage,
        IncorrectMagicNumber,
        IncorrectFormat
    };

    CFrenchnodePaymentDB();
    bool Write(const CFrenchnodePayments& objToSave);
    ReadResult Read(CFrenchnodePayments& objToLoad, bool fDryRun = false);
};

class CFrenchnodePayee
{
public:
    CScript scriptPubKey;
    int nVotes;

    CFrenchnodePayee()
    {
        scriptPubKey = CScript();
        nVotes = 0;
    }

    CFrenchnodePayee(CScript payee, int nVotesIn)
    {
        scriptPubKey = payee;
        nVotes = nVotesIn;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(scriptPubKey);
        READWRITE(nVotes);
    }
};

// Keep track of votes for payees from masternodes
class CFrenchnodeBlockPayees
{
public:
    int nBlockHeight;
    std::vector<CFrenchnodePayee> vecPayments;

    CFrenchnodeBlockPayees()
    {
        nBlockHeight = 0;
        vecPayments.clear();
    }
    CFrenchnodeBlockPayees(int nBlockHeightIn)
    {
        nBlockHeight = nBlockHeightIn;
        vecPayments.clear();
    }

    void AddPayee(CScript payeeIn, int nIncrement)
    {
        LOCK(cs_vecPayments);

        BOOST_FOREACH (CFrenchnodePayee& payee, vecPayments) {
            if (payee.scriptPubKey == payeeIn) {
                payee.nVotes += nIncrement;
                return;
            }
        }

        CFrenchnodePayee c(payeeIn, nIncrement);
        vecPayments.push_back(c);
    }

    bool GetPayee(CScript& payee)
    {
        LOCK(cs_vecPayments);

        int nVotes = -1;
        BOOST_FOREACH (CFrenchnodePayee& p, vecPayments) {
            if (p.nVotes > nVotes) {
                payee = p.scriptPubKey;
                nVotes = p.nVotes;
            }
        }

        return (nVotes > -1);
    }

    bool HasPayeeWithVotes(CScript payee, int nVotesReq)
    {
        LOCK(cs_vecPayments);

        BOOST_FOREACH (CFrenchnodePayee& p, vecPayments) {
            if (p.nVotes >= nVotesReq && p.scriptPubKey == payee) return true;
        }

        return false;
    }

    bool IsTransactionValid(const CTransaction& txNew);
    std::string GetRequiredPaymentsString();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(nBlockHeight);
        READWRITE(vecPayments);
    }
};

// for storing the winning payments
class CFrenchnodePaymentWinner
{
public:
    CTxIn vinFrenchnode;

    int nBlockHeight;
    CScript payee;
    std::vector<unsigned char> vchSig;

    CFrenchnodePaymentWinner()
    {
        nBlockHeight = 0;
        vinFrenchnode = CTxIn();
        payee = CScript();
    }

    CFrenchnodePaymentWinner(CTxIn vinIn)
    {
        nBlockHeight = 0;
        vinFrenchnode = vinIn;
        payee = CScript();
    }

    uint256 GetHash()
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << payee;
        ss << nBlockHeight;
        ss << vinFrenchnode.prevout;

        return ss.GetHash();
    }

    bool Sign(CKey& keyFrenchnode, CPubKey& pubKeyFrenchnode);
    bool IsValid(CNode* pnode, std::string& strError);
    bool SignatureValid();
    void Relay();

    void AddPayee(CScript payeeIn)
    {
        payee = payeeIn;
    }


    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(vinFrenchnode);
        READWRITE(nBlockHeight);
        READWRITE(payee);
        READWRITE(vchSig);
    }

    std::string ToString()
    {
        std::string ret = "";
        ret += vinFrenchnode.ToString();
        ret += ", " + boost::lexical_cast<std::string>(nBlockHeight);
        ret += ", " + payee.ToString();
        ret += ", " + boost::lexical_cast<std::string>((int)vchSig.size());
        return ret;
    }
};

//
// Frenchnode Payments Class
// Keeps track of who should get paid for which blocks
//

class CFrenchnodePayments
{
private:
    int nSyncedFromPeer;
    int nLastBlockHeight;

public:
    std::map<uint256, CFrenchnodePaymentWinner> mapFrenchnodePayeeVotes;
    std::map<int, CFrenchnodeBlockPayees> mapFrenchnodeBlocks;
    std::map<uint256, int> mapFrenchnodesLastVote; //prevout.hash + prevout.n, nBlockHeight

    CFrenchnodePayments()
    {
        nSyncedFromPeer = 0;
        nLastBlockHeight = 0;
    }

    void Clear()
    {
        LOCK2(cs_mapFrenchnodeBlocks, cs_mapFrenchnodePayeeVotes);
        mapFrenchnodeBlocks.clear();
        mapFrenchnodePayeeVotes.clear();
    }

    bool AddWinningFrenchnode(CFrenchnodePaymentWinner& winner);
    bool ProcessBlock(int nBlockHeight);

    void Sync(CNode* node, int nCountNeeded);
    void CleanPaymentList();
    int LastPayment(CFrenchnode& mn);

    bool GetBlockPayee(int nBlockHeight, CScript& payee);
    bool IsTransactionValid(const CTransaction& txNew, int nBlockHeight);
    bool IsScheduled(CFrenchnode& mn, int nNotBlockHeight);

    bool CanVote(COutPoint outFrenchnode, int nBlockHeight)
    {
        LOCK(cs_mapFrenchnodePayeeVotes);

        if (mapFrenchnodesLastVote.count(outFrenchnode.hash + outFrenchnode.n)) {
            if (mapFrenchnodesLastVote[outFrenchnode.hash + outFrenchnode.n] == nBlockHeight) {
                return false;
            }
        }

        //record this masternode voted
        mapFrenchnodesLastVote[outFrenchnode.hash + outFrenchnode.n] = nBlockHeight;
        return true;
    }

    int GetMinFrenchnodePaymentsProto();
    void ProcessMessageFrenchnodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    std::string GetRequiredPaymentsString(int nBlockHeight);
    void FillBlockPayee(CMutableTransaction& txNew, int64_t nFees, bool fProofOfStake);
    std::string ToString() const;
    int GetOldestBlock();
    int GetNewestBlock();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(mapFrenchnodePayeeVotes);
        READWRITE(mapFrenchnodeBlocks);
    }
};


#endif
