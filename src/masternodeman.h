// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef FRENCHNODEMAN_H
#define FRENCHNODEMAN_H

#include "base58.h"
#include "key.h"
#include "main.h"
#include "masternode.h"
#include "net.h"
#include "sync.h"
#include "util.h"

#define FRENCHNODES_DUMP_SECONDS (15 * 60)
#define FRENCHNODES_DSEG_SECONDS (3 * 60 * 60)

using namespace std;

class CFrenchnodeMan;

extern CFrenchnodeMan mnodeman;
void DumpFrenchnodes();

/** Access to the MN database (mncache.dat)
 */
class CFrenchnodeDB
{
private:
    boost::filesystem::path pathMN;
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

    CFrenchnodeDB();
    bool Write(const CFrenchnodeMan& mnodemanToSave);
    ReadResult Read(CFrenchnodeMan& mnodemanToLoad, bool fDryRun = false);
};

class CFrenchnodeMan
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    // critical section to protect the inner data structures specifically on messaging
    mutable CCriticalSection cs_process_message;

    // map to hold all MNs
    std::vector<CFrenchnode> vFrenchnodes;
    // who's asked for the Frenchnode list and the last time
    std::map<CNetAddr, int64_t> mAskedUsForFrenchnodeList;
    // who we asked for the Frenchnode list and the last time
    std::map<CNetAddr, int64_t> mWeAskedForFrenchnodeList;
    // which Frenchnodes we've asked for
    std::map<COutPoint, int64_t> mWeAskedForFrenchnodeListEntry;

public:
    // Keep track of all broadcasts I've seen
    map<uint256, CFrenchnodeBroadcast> mapSeenFrenchnodeBroadcast;
    // Keep track of all pings I've seen
    map<uint256, CFrenchnodePing> mapSeenFrenchnodePing;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        LOCK(cs);
        READWRITE(vFrenchnodes);
        READWRITE(mAskedUsForFrenchnodeList);
        READWRITE(mWeAskedForFrenchnodeList);
        READWRITE(mWeAskedForFrenchnodeListEntry);

        READWRITE(mapSeenFrenchnodeBroadcast);
        READWRITE(mapSeenFrenchnodePing);
    }

    CFrenchnodeMan();
    CFrenchnodeMan(CFrenchnodeMan& other);

    /// Add an entry
    bool Add(CFrenchnode& mn);

    /// Ask (source) node for mnb
    void AskForMN(CNode* pnode, CTxIn& vin);

    /// Check all Frenchnodes
    void Check();

    /// Check all Frenchnodes and remove inactive
    void CheckAndRemove(bool forceExpiredRemoval = false);

    /// Clear Frenchnode vector
    void Clear();

    int CountEnabled(int protocolVersion = -1);

    void CountNetworks(int protocolVersion, int& ipv4, int& ipv6, int& onion);

    void DsegUpdate(CNode* pnode);

    /// Find an entry
    CFrenchnode* Find(const CScript& payee);
    CFrenchnode* Find(const CTxIn& vin);
    CFrenchnode* Find(const CPubKey& pubKeyFrenchnode);

    /// Find an entry in the masternode list that is next to be paid
    CFrenchnode* GetNextFrenchnodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount);

    /// Find a random entry
    CFrenchnode* FindRandomNotInVec(std::vector<CTxIn>& vecToExclude, int protocolVersion = -1);

    /// Get the current winner for this block
    CFrenchnode* GetCurrentMasterNode(int mod = 1, int64_t nBlockHeight = 0, int minProtocol = 0);

    std::vector<CFrenchnode> GetFullFrenchnodeVector()
    {
        Check();
        return vFrenchnodes;
    }

    std::vector<pair<int, CFrenchnode> > GetFrenchnodeRanks(int64_t nBlockHeight, int minProtocol = 0);
    int GetFrenchnodeRank(const CTxIn& vin, int64_t nBlockHeight, int minProtocol = 0, bool fOnlyActive = true);
    CFrenchnode* GetFrenchnodeByRank(int nRank, int64_t nBlockHeight, int minProtocol = 0, bool fOnlyActive = true);

    void ProcessFrenchnodeConnections();

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);

    /// Return the number of (unique) Frenchnodes
    int size() { return vFrenchnodes.size(); }

    /// Return the number of Frenchnodes older than (default) 8000 seconds
    int stable_size ();

    std::string ToString() const;

    void Remove(CTxIn vin);

    /// Update masternode list and maps using provided CFrenchnodeBroadcast
    void UpdateFrenchnodeList(CFrenchnodeBroadcast mnb);
};

#endif
