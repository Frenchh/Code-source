// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "masternodeman.h"
#include "activemasternode.h"
#include "masternode-payments.h"
#include "masternode-helpers.h"
#include "addrman.h"
#include "masternode.h"
#include "spork.h"
#include "util.h"
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>

/** Frenchnode manager */
CFrenchnodeMan mnodeman;

struct CompareLastPaid {
    bool operator()(const pair<int64_t, CTxIn>& t1,
        const pair<int64_t, CTxIn>& t2) const
    {
        return t1.first < t2.first;
    }
};

struct CompareScoreTxIn {
    bool operator()(const pair<int64_t, CTxIn>& t1,
        const pair<int64_t, CTxIn>& t2) const
    {
        return t1.first < t2.first;
    }
};

struct CompareScoreMN {
    bool operator()(const pair<int64_t, CFrenchnode>& t1,
        const pair<int64_t, CFrenchnode>& t2) const
    {
        return t1.first < t2.first;
    }
};

//
// CFrenchnodeDB
//

CFrenchnodeDB::CFrenchnodeDB()
{
    pathMN = GetDataDir() / "mncache.dat";
    strMagicMessage = "FrenchnodeCache";
}

bool CFrenchnodeDB::Write(const CFrenchnodeMan& mnodemanToSave)
{
    int64_t nStart = GetTimeMillis();

    // serialize, checksum data up to that point, then append checksum
    CDataStream ssFrenchnodes(SER_DISK, CLIENT_VERSION);
    ssFrenchnodes << strMagicMessage;                   // masternode cache file specific magic message
    ssFrenchnodes << FLATDATA(Params().MessageStart()); // network specific magic number
    ssFrenchnodes << mnodemanToSave;
    uint256 hash = Hash(ssFrenchnodes.begin(), ssFrenchnodes.end());
    ssFrenchnodes << hash;

    // open output file, and associate with CAutoFile
    FILE* file = fopen(pathMN.string().c_str(), "wb");
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s : Failed to open file %s", __func__, pathMN.string());

    // Write and commit header, data
    try {
        fileout << ssFrenchnodes;
    } catch (std::exception& e) {
        return error("%s : Serialize or I/O error - %s", __func__, e.what());
    }
    //    FileCommit(fileout);
    fileout.fclose();

    LogPrint("masternode","Written info to mncache.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrint("masternode","  %s\n", mnodemanToSave.ToString());

    return true;
}

CFrenchnodeDB::ReadResult CFrenchnodeDB::Read(CFrenchnodeMan& mnodemanToLoad, bool fDryRun)
{
    int64_t nStart = GetTimeMillis();
    // open input file, and associate with CAutoFile
    FILE* file = fopen(pathMN.string().c_str(), "rb");
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
    if (filein.IsNull()) {
        error("%s : Failed to open file %s", __func__, pathMN.string());
        return FileError;
    }

    // use file size to size memory buffer
    int fileSize = boost::filesystem::file_size(pathMN);
    int dataSize = fileSize - sizeof(uint256);
    // Don't try to resize to a negative number if file is small
    if (dataSize < 0)
        dataSize = 0;
    vector<unsigned char> vchData;
    vchData.resize(dataSize);
    uint256 hashIn;

    // read data and checksum from file
    try {
        filein.read((char*)&vchData[0], dataSize);
        filein >> hashIn;
    } catch (std::exception& e) {
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return HashReadError;
    }
    filein.fclose();

    CDataStream ssFrenchnodes(vchData, SER_DISK, CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssFrenchnodes.begin(), ssFrenchnodes.end());
    if (hashIn != hashTmp) {
        error("%s : Checksum mismatch, data corrupted", __func__);
        return IncorrectHash;
    }

    unsigned char pchMsgTmp[4];
    std::string strMagicMessageTmp;
    try {
        // de-serialize file header (masternode cache file specific magic message) and ..

        ssFrenchnodes >> strMagicMessageTmp;

        // ... verify the message matches predefined one
        if (strMagicMessage != strMagicMessageTmp) {
            error("%s : Invalid masternode cache magic message", __func__);
            return IncorrectMagicMessage;
        }

        // de-serialize file header (network specific magic number) and ..
        ssFrenchnodes >> FLATDATA(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp))) {
            error("%s : Invalid network magic number", __func__);
            return IncorrectMagicNumber;
        }
        // de-serialize data into CFrenchnodeMan object
        ssFrenchnodes >> mnodemanToLoad;
    } catch (std::exception& e) {
        mnodemanToLoad.Clear();
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return IncorrectFormat;
    }

    LogPrint("masternode","Loaded info from mncache.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrint("masternode","  %s\n", mnodemanToLoad.ToString());
    if (!fDryRun) {
        LogPrint("masternode","Frenchnode manager - cleaning....\n");
        mnodemanToLoad.CheckAndRemove(true);
        LogPrint("masternode","Frenchnode manager - result:\n");
        LogPrint("masternode","  %s\n", mnodemanToLoad.ToString());
    }

    return Ok;
}

void DumpFrenchnodes()
{
    int64_t nStart = GetTimeMillis();

    CFrenchnodeDB mndb;
    CFrenchnodeMan tempMnodeman;

    LogPrint("masternode","Verifying mncache.dat format...\n");
    CFrenchnodeDB::ReadResult readResult = mndb.Read(tempMnodeman, true);
    // there was an error and it was not an error on file opening => do not proceed
    if (readResult == CFrenchnodeDB::FileError)
        LogPrint("masternode","Missing masternode cache file - mncache.dat, will try to recreate\n");
    else if (readResult != CFrenchnodeDB::Ok) {
        LogPrint("masternode","Error reading mncache.dat: ");
        if (readResult == CFrenchnodeDB::IncorrectFormat)
            LogPrint("masternode","magic is ok but data has invalid format, will try to recreate\n");
        else {
            LogPrint("masternode","file format is unknown or invalid, please fix it manually\n");
            return;
        }
    }
    LogPrint("masternode","Writting info to mncache.dat...\n");
    mndb.Write(mnodeman);

    LogPrint("masternode","Frenchnode dump finished  %dms\n", GetTimeMillis() - nStart);
}

CFrenchnodeMan::CFrenchnodeMan()
{
}

bool CFrenchnodeMan::Add(CFrenchnode& mn)
{
    LOCK(cs);

    if (!mn.IsEnabled())
        return false;

    CFrenchnode* pmn = Find(mn.vin);
    if (pmn == NULL) {
        LogPrint("masternode", "CFrenchnodeMan: Adding new Frenchnode %s - %i now\n", mn.vin.prevout.hash.ToString(), size() + 1);
        vFrenchnodes.push_back(mn);
        return true;
    }

    return false;
}

void CFrenchnodeMan::AskForMN(CNode* pnode, CTxIn& vin)
{
    std::map<COutPoint, int64_t>::iterator i = mWeAskedForFrenchnodeListEntry.find(vin.prevout);
    if (i != mWeAskedForFrenchnodeListEntry.end()) {
        int64_t t = (*i).second;
        if (GetTime() < t) return; // we've asked recently
    }

    // ask for the mnb info once from the node that sent mnp

    LogPrint("masternode", "CFrenchnodeMan::AskForMN - Asking node for missing entry, vin: %s\n", vin.prevout.hash.ToString());
    pnode->PushMessage("dseg", vin);
    int64_t askAgain = GetTime() + FRENCHNODE_MIN_MNP_SECONDS;
    mWeAskedForFrenchnodeListEntry[vin.prevout] = askAgain;
}

void CFrenchnodeMan::Check()
{
    LOCK(cs);

    BOOST_FOREACH (CFrenchnode& mn, vFrenchnodes) {
        mn.Check();
    }
}

void CFrenchnodeMan::CheckAndRemove(bool forceExpiredRemoval)
{
    Check();

    LOCK(cs);

    //remove inactive and outdated
    vector<CFrenchnode>::iterator it = vFrenchnodes.begin();
    while (it != vFrenchnodes.end()) {
        if ((*it).activeState == CFrenchnode::FRENCHNODE_REMOVE ||
            (*it).activeState == CFrenchnode::FRENCHNODE_VIN_SPENT ||
            (forceExpiredRemoval && (*it).activeState == CFrenchnode::FRENCHNODE_EXPIRED) ||
            (*it).protocolVersion < masternodePayments.GetMinFrenchnodePaymentsProto()) {
            LogPrint("masternode", "CFrenchnodeMan: Removing inactive Frenchnode %s - %i now\n", (*it).vin.prevout.hash.ToString(), size() - 1);

            //erase all of the broadcasts we've seen from this vin
            // -- if we missed a few pings and the node was removed, this will allow is to get it back without them
            //    sending a brand new mnb
            map<uint256, CFrenchnodeBroadcast>::iterator it3 = mapSeenFrenchnodeBroadcast.begin();
            while (it3 != mapSeenFrenchnodeBroadcast.end()) {
                if ((*it3).second.vin == (*it).vin) {
                    masternodeSync.mapSeenSyncMNB.erase((*it3).first);
                    mapSeenFrenchnodeBroadcast.erase(it3++);
                } else {
                    ++it3;
                }
            }

            // allow us to ask for this masternode again if we see another ping
            map<COutPoint, int64_t>::iterator it2 = mWeAskedForFrenchnodeListEntry.begin();
            while (it2 != mWeAskedForFrenchnodeListEntry.end()) {
                if ((*it2).first == (*it).vin.prevout) {
                    mWeAskedForFrenchnodeListEntry.erase(it2++);
                } else {
                    ++it2;
                }
            }

            it = vFrenchnodes.erase(it);
        } else {
            ++it;
        }
    }

    // check who's asked for the Frenchnode list
    map<CNetAddr, int64_t>::iterator it1 = mAskedUsForFrenchnodeList.begin();
    while (it1 != mAskedUsForFrenchnodeList.end()) {
        if ((*it1).second < GetTime()) {
            mAskedUsForFrenchnodeList.erase(it1++);
        } else {
            ++it1;
        }
    }

    // check who we asked for the Frenchnode list
    it1 = mWeAskedForFrenchnodeList.begin();
    while (it1 != mWeAskedForFrenchnodeList.end()) {
        if ((*it1).second < GetTime()) {
            mWeAskedForFrenchnodeList.erase(it1++);
        } else {
            ++it1;
        }
    }

    // check which Frenchnodes we've asked for
    map<COutPoint, int64_t>::iterator it2 = mWeAskedForFrenchnodeListEntry.begin();
    while (it2 != mWeAskedForFrenchnodeListEntry.end()) {
        if ((*it2).second < GetTime()) {
            mWeAskedForFrenchnodeListEntry.erase(it2++);
        } else {
            ++it2;
        }
    }

    // remove expired mapSeenFrenchnodeBroadcast
    map<uint256, CFrenchnodeBroadcast>::iterator it3 = mapSeenFrenchnodeBroadcast.begin();
    while (it3 != mapSeenFrenchnodeBroadcast.end()) {
        if ((*it3).second.lastPing.sigTime < GetTime() - (FRENCHNODE_REMOVAL_SECONDS * 2)) {
            mapSeenFrenchnodeBroadcast.erase(it3++);
            masternodeSync.mapSeenSyncMNB.erase((*it3).second.GetHash());
        } else {
            ++it3;
        }
    }

    // remove expired mapSeenFrenchnodePing
    map<uint256, CFrenchnodePing>::iterator it4 = mapSeenFrenchnodePing.begin();
    while (it4 != mapSeenFrenchnodePing.end()) {
        if ((*it4).second.sigTime < GetTime() - (FRENCHNODE_REMOVAL_SECONDS * 2)) {
            mapSeenFrenchnodePing.erase(it4++);
        } else {
            ++it4;
        }
    }
}

void CFrenchnodeMan::Clear()
{
    LOCK(cs);
    vFrenchnodes.clear();
    mAskedUsForFrenchnodeList.clear();
    mWeAskedForFrenchnodeList.clear();
    mWeAskedForFrenchnodeListEntry.clear();
    mapSeenFrenchnodeBroadcast.clear();
    mapSeenFrenchnodePing.clear();
}

int CFrenchnodeMan::stable_size ()
{
    int nStable_size = 0;
    int nMinProtocol = ActiveProtocol();
    int64_t nFrenchnode_Min_Age = GetSporkValue(SPORK_16_MN_WINNER_MINIMUM_AGE);
    int64_t nFrenchnode_Age = 0;

    BOOST_FOREACH (CFrenchnode& mn, vFrenchnodes) {
        if (mn.protocolVersion < nMinProtocol) {
            continue; // Skip obsolete versions
        }
        if (IsSporkActive (SPORK_8_FRENCHNODE_PAYMENT_ENFORCEMENT)) {
            nFrenchnode_Age = GetAdjustedTime() - mn.sigTime;
            if ((nFrenchnode_Age) < nFrenchnode_Min_Age) {
                continue; // Skip masternodes younger than (default) 8000 sec (MUST be > FRENCHNODE_REMOVAL_SECONDS)
            }
        }
        mn.Check ();
        if (!mn.IsEnabled ())
            continue; // Skip not-enabled masternodes

        nStable_size++;
    }

    return nStable_size;
}

int CFrenchnodeMan::CountEnabled(int protocolVersion)
{
    int i = 0;
    protocolVersion = protocolVersion == -1 ? masternodePayments.GetMinFrenchnodePaymentsProto() : protocolVersion;

    BOOST_FOREACH (CFrenchnode& mn, vFrenchnodes) {
        mn.Check();
        if (mn.protocolVersion < protocolVersion || !mn.IsEnabled()) continue;
        i++;
    }

    return i;
}

void CFrenchnodeMan::CountNetworks(int protocolVersion, int& ipv4, int& ipv6, int& onion)
{
    protocolVersion = protocolVersion == -1 ? masternodePayments.GetMinFrenchnodePaymentsProto() : protocolVersion;

    BOOST_FOREACH (CFrenchnode& mn, vFrenchnodes) {
        mn.Check();
        std::string strHost;
        int port;
        SplitHostPort(mn.addr.ToString(), port, strHost);
        CNetAddr node = CNetAddr(strHost, false);
        int nNetwork = node.GetNetwork();
        switch (nNetwork) {
            case 1 :
                ipv4++;
                break;
            case 2 :
                ipv6++;
                break;
            case 3 :
                onion++;
                break;
        }
    }
}

void CFrenchnodeMan::DsegUpdate(CNode* pnode)
{
    LOCK(cs);

    if (Params().NetworkID() == CBaseChainParams::MAIN) {
        if (!(pnode->addr.IsRFC1918() || pnode->addr.IsLocal())) {
            std::map<CNetAddr, int64_t>::iterator it = mWeAskedForFrenchnodeList.find(pnode->addr);
            if (it != mWeAskedForFrenchnodeList.end()) {
                if (GetTime() < (*it).second) {
                    LogPrint("masternode", "dseg - we already asked peer %i for the list; skipping...\n", pnode->GetId());
                    return;
                }
            }
        }
    }

    pnode->PushMessage("dseg", CTxIn());
    int64_t askAgain = GetTime() + FRENCHNODES_DSEG_SECONDS;
    mWeAskedForFrenchnodeList[pnode->addr] = askAgain;
}

CFrenchnode* CFrenchnodeMan::Find(const CScript& payee)
{
    LOCK(cs);
    CScript payee2;

    BOOST_FOREACH (CFrenchnode& mn, vFrenchnodes) {
        payee2 = GetScriptForDestination(mn.pubKeyCollateralAddress.GetID());
        if (payee2 == payee)
            return &mn;
    }
    return NULL;
}

CFrenchnode* CFrenchnodeMan::Find(const CTxIn& vin)
{
    LOCK(cs);

    BOOST_FOREACH (CFrenchnode& mn, vFrenchnodes) {
        if (mn.vin.prevout == vin.prevout)
            return &mn;
    }
    return NULL;
}


CFrenchnode* CFrenchnodeMan::Find(const CPubKey& pubKeyFrenchnode)
{
    LOCK(cs);

    BOOST_FOREACH (CFrenchnode& mn, vFrenchnodes) {
        if (mn.pubKeyFrenchnode == pubKeyFrenchnode)
            return &mn;
    }
    return NULL;
}

//
// Deterministically select the oldest/best masternode to pay on the network
//
CFrenchnode* CFrenchnodeMan::GetNextFrenchnodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount)
{
    LOCK(cs);

    CFrenchnode* pBestFrenchnode = NULL;
    std::vector<pair<int64_t, CTxIn> > vecFrenchnodeLastPaid;

    /*
        Make a vector with all of the last paid times
    */

    int nMnCount = CountEnabled();
    BOOST_FOREACH (CFrenchnode& mn, vFrenchnodes) {
        mn.Check();
        if (!mn.IsEnabled()) continue;

        // //check protocol version
        if (mn.protocolVersion < masternodePayments.GetMinFrenchnodePaymentsProto()) continue;

        //it's in the list (up to 8 entries ahead of current block to allow propagation) -- so let's skip it
        if (masternodePayments.IsScheduled(mn, nBlockHeight)) continue;

        //it's too new, wait for a cycle
        if (fFilterSigTime && mn.sigTime + (nMnCount * 2.6 * 60) > GetAdjustedTime()) continue;

        //make sure it has as many confirmations as there are masternodes
        if (mn.GetFrenchnodeInputAge() < nMnCount) continue;

        vecFrenchnodeLastPaid.push_back(make_pair(mn.SecondsSincePayment(), mn.vin));
    }

    nCount = (int)vecFrenchnodeLastPaid.size();

    //when the network is in the process of upgrading, don't penalize nodes that recently restarted
    if (fFilterSigTime && nCount < nMnCount / 3) return GetNextFrenchnodeInQueueForPayment(nBlockHeight, false, nCount);

    // Sort them high to low
    sort(vecFrenchnodeLastPaid.rbegin(), vecFrenchnodeLastPaid.rend(), CompareLastPaid());

    // Look at 1/10 of the oldest nodes (by last payment), calculate their scores and pay the best one
    //  -- This doesn't look at who is being paid in the +8-10 blocks, allowing for double payments very rarely
    //  -- 1/100 payments should be a double payment on mainnet - (1/(3000/10))*2
    //  -- (chance per block * chances before IsScheduled will fire)
    int nTenthNetwork = CountEnabled() / 10;
    int nCountTenth = 0;
    uint256 nHigh = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CTxIn) & s, vecFrenchnodeLastPaid) {
        CFrenchnode* pmn = Find(s.second);
        if (!pmn) break;

        uint256 n = pmn->CalculateScore(1, nBlockHeight - 100);
        if (n > nHigh) {
            nHigh = n;
            pBestFrenchnode = pmn;
        }
        nCountTenth++;
        if (nCountTenth >= nTenthNetwork) break;
    }
    return pBestFrenchnode;
}

CFrenchnode* CFrenchnodeMan::FindRandomNotInVec(std::vector<CTxIn>& vecToExclude, int protocolVersion)
{
    LOCK(cs);

    protocolVersion = protocolVersion == -1 ? masternodePayments.GetMinFrenchnodePaymentsProto() : protocolVersion;

    int nCountEnabled = CountEnabled(protocolVersion);
    LogPrint("masternode", "CFrenchnodeMan::FindRandomNotInVec - nCountEnabled - vecToExclude.size() %d\n", nCountEnabled - vecToExclude.size());
    if (nCountEnabled - vecToExclude.size() < 1) return NULL;

    int rand = GetRandInt(nCountEnabled - vecToExclude.size());
    LogPrint("masternode", "CFrenchnodeMan::FindRandomNotInVec - rand %d\n", rand);
    bool found;

    BOOST_FOREACH (CFrenchnode& mn, vFrenchnodes) {
        if (mn.protocolVersion < protocolVersion || !mn.IsEnabled()) continue;
        found = false;
        BOOST_FOREACH (CTxIn& usedVin, vecToExclude) {
            if (mn.vin.prevout == usedVin.prevout) {
                found = true;
                break;
            }
        }
        if (found) continue;
        if (--rand < 1) {
            return &mn;
        }
    }

    return NULL;
}

CFrenchnode* CFrenchnodeMan::GetCurrentMasterNode(int mod, int64_t nBlockHeight, int minProtocol)
{
    int64_t score = 0;
    CFrenchnode* winner = NULL;

    // scan for winner
    BOOST_FOREACH (CFrenchnode& mn, vFrenchnodes) {
        mn.Check();
        if (mn.protocolVersion < minProtocol || !mn.IsEnabled()) continue;

        // calculate the score for each Frenchnode
        uint256 n = mn.CalculateScore(mod, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        // determine the winner
        if (n2 > score) {
            score = n2;
            winner = &mn;
        }
    }

    return winner;
}

int CFrenchnodeMan::GetFrenchnodeRank(const CTxIn& vin, int64_t nBlockHeight, int minProtocol, bool fOnlyActive)
{
    std::vector<pair<int64_t, CTxIn> > vecFrenchnodeScores;
    int64_t nFrenchnode_Min_Age = GetSporkValue(SPORK_16_MN_WINNER_MINIMUM_AGE);
    int64_t nFrenchnode_Age = 0;

    //make sure we know about this block
    uint256 hash = 0;
    if (!GetBlockHash(hash, nBlockHeight)) return -1;

    // scan for winner
    BOOST_FOREACH (CFrenchnode& mn, vFrenchnodes) {
        if (mn.protocolVersion < minProtocol) {
            LogPrint("masternode","Skipping Frenchnode with obsolete version %d\n", mn.protocolVersion);
            continue;                                                       // Skip obsolete versions
        }

        if (IsSporkActive(SPORK_8_FRENCHNODE_PAYMENT_ENFORCEMENT)) {
            nFrenchnode_Age = GetAdjustedTime() - mn.sigTime;
            if ((nFrenchnode_Age) < nFrenchnode_Min_Age) {
                if (fDebug) LogPrint("masternode","Skipping just activated Frenchnode. Age: %ld\n", nFrenchnode_Age);
                continue;                                                   // Skip masternodes younger than (default) 1 hour
            }
        }
        if (fOnlyActive) {
            mn.Check();
            if (!mn.IsEnabled()) continue;
        }
        uint256 n = mn.CalculateScore(1, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        vecFrenchnodeScores.push_back(make_pair(n2, mn.vin));
    }

    sort(vecFrenchnodeScores.rbegin(), vecFrenchnodeScores.rend(), CompareScoreTxIn());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CTxIn) & s, vecFrenchnodeScores) {
        rank++;
        if (s.second.prevout == vin.prevout) {
            return rank;
        }
    }

    return -1;
}

std::vector<pair<int, CFrenchnode> > CFrenchnodeMan::GetFrenchnodeRanks(int64_t nBlockHeight, int minProtocol)
{
    std::vector<pair<int64_t, CFrenchnode> > vecFrenchnodeScores;
    std::vector<pair<int, CFrenchnode> > vecFrenchnodeRanks;

    //make sure we know about this block
    uint256 hash = 0;
    if (!GetBlockHash(hash, nBlockHeight)) return vecFrenchnodeRanks;

    // scan for winner
    BOOST_FOREACH (CFrenchnode& mn, vFrenchnodes) {
        mn.Check();

        if (mn.protocolVersion < minProtocol) continue;

        if (!mn.IsEnabled()) {
            vecFrenchnodeScores.push_back(make_pair(9999, mn));
            continue;
        }

        uint256 n = mn.CalculateScore(1, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        vecFrenchnodeScores.push_back(make_pair(n2, mn));
    }

    sort(vecFrenchnodeScores.rbegin(), vecFrenchnodeScores.rend(), CompareScoreMN());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CFrenchnode) & s, vecFrenchnodeScores) {
        rank++;
        vecFrenchnodeRanks.push_back(make_pair(rank, s.second));
    }

    return vecFrenchnodeRanks;
}

CFrenchnode* CFrenchnodeMan::GetFrenchnodeByRank(int nRank, int64_t nBlockHeight, int minProtocol, bool fOnlyActive)
{
    std::vector<pair<int64_t, CTxIn> > vecFrenchnodeScores;

    // scan for winner
    BOOST_FOREACH (CFrenchnode& mn, vFrenchnodes) {
        if (mn.protocolVersion < minProtocol) continue;
        if (fOnlyActive) {
            mn.Check();
            if (!mn.IsEnabled()) continue;
        }

        uint256 n = mn.CalculateScore(1, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        vecFrenchnodeScores.push_back(make_pair(n2, mn.vin));
    }

    sort(vecFrenchnodeScores.rbegin(), vecFrenchnodeScores.rend(), CompareScoreTxIn());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CTxIn) & s, vecFrenchnodeScores) {
        rank++;
        if (rank == nRank) {
            return Find(s.second);
        }
    }

    return NULL;
}

void CFrenchnodeMan::ProcessFrenchnodeConnections()
{
    //we don't care about this for regtest
    if (Params().NetworkID() == CBaseChainParams::REGTEST) return;

    LOCK(cs_vNodes);
    BOOST_FOREACH (CNode* pnode, vNodes) {
        // if (pnode->fObfuScationMaster) {
        //     LogPrint("masternode","Closing Frenchnode connection peer=%i \n", pnode->GetId());
        //     pnode->fObfuScationMaster = false;
        //     pnode->Release();
        // }
    }
}

void CFrenchnodeMan::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (fLiteMode) return; //disable all Frenchnode related functionality
    if (!masternodeSync.IsBlockchainSynced()) return;

    LOCK(cs_process_message);

    if (strCommand == "mnb") { //Frenchnode Broadcast
        CFrenchnodeBroadcast mnb;
        vRecv >> mnb;

        if (mapSeenFrenchnodeBroadcast.count(mnb.GetHash())) { //seen
            masternodeSync.AddedFrenchnodeList(mnb.GetHash());
            return;
        }
        mapSeenFrenchnodeBroadcast.insert(make_pair(mnb.GetHash(), mnb));

        int nDoS = 0;
        if (!mnb.CheckAndUpdate(nDoS)) {
            if (nDoS > 0)
                Misbehaving(pfrom->GetId(), nDoS);

            //failed
            return;
        }

        // make sure the vout that was signed is related to the transaction that spawned the Frenchnode
        //  - this is expensive, so it's only done once per Frenchnode
        if (!masternodeSigner.IsVinAssociatedWithPubkey(mnb.vin, mnb.pubKeyCollateralAddress)) {
            LogPrint("masternode","mnb - Got mismatched pubkey and vin\n");
            Misbehaving(pfrom->GetId(), 33);
            return;
        }

        // make sure it's still unspent
        if (mnb.CheckInputsAndAdd(nDoS)) {
            // use this as a peer
            addrman.Add(CAddress(mnb.addr), pfrom->addr, 2 * 60 * 60);
            masternodeSync.AddedFrenchnodeList(mnb.GetHash());
        } else {
            LogPrint("masternode","mnb - Rejected Frenchnode entry %s\n", mnb.vin.prevout.hash.ToString());

            if (nDoS > 0)
                Misbehaving(pfrom->GetId(), nDoS);
        }
    }

    else if (strCommand == "mnp") { //Frenchnode Ping
        CFrenchnodePing mnp;
        vRecv >> mnp;

        LogPrint("masternode", "mnp - Frenchnode ping, vin: %s\n", mnp.vin.prevout.hash.ToString());

        if (mapSeenFrenchnodePing.count(mnp.GetHash())) return; //seen
        mapSeenFrenchnodePing.insert(make_pair(mnp.GetHash(), mnp));

        int nDoS = 0;
        if (mnp.CheckAndUpdate(nDoS)) return;

        if (nDoS > 0) {
            // if anything significant failed, mark that node
            Misbehaving(pfrom->GetId(), nDoS);
        } else {
            // if nothing significant failed, search existing Frenchnode list
            CFrenchnode* pmn = Find(mnp.vin);
            // if it's known, don't ask for the mnb, just return
            if (pmn != NULL) return;
        }

        // something significant is broken or mn is unknown,
        // we might have to ask for a masternode entry once
        AskForMN(pfrom, mnp.vin);

    } else if (strCommand == "dseg") { //Get Frenchnode list or specific entry

        CTxIn vin;
        vRecv >> vin;

        if (vin == CTxIn()) { //only should ask for this once
            //local network
            bool isLocal = (pfrom->addr.IsRFC1918() || pfrom->addr.IsLocal());

            if (!isLocal && Params().NetworkID() == CBaseChainParams::MAIN) {
                std::map<CNetAddr, int64_t>::iterator i = mAskedUsForFrenchnodeList.find(pfrom->addr);
                if (i != mAskedUsForFrenchnodeList.end()) {
                    int64_t t = (*i).second;
                    if (GetTime() < t) {
                        Misbehaving(pfrom->GetId(), 34);
                        LogPrint("masternode","dseg - peer already asked me for the list\n");
                        return;
                    }
                }
                int64_t askAgain = GetTime() + FRENCHNODES_DSEG_SECONDS;
                mAskedUsForFrenchnodeList[pfrom->addr] = askAgain;
            }
        } //else, asking for a specific node which is ok


        int nInvCount = 0;

        BOOST_FOREACH (CFrenchnode& mn, vFrenchnodes) {
            if (mn.addr.IsRFC1918()) continue; //local network

            if (mn.IsEnabled()) {
                LogPrint("masternode", "dseg - Sending Frenchnode entry - %s \n", mn.vin.prevout.hash.ToString());
                if (vin == CTxIn() || vin == mn.vin) {
                    CFrenchnodeBroadcast mnb = CFrenchnodeBroadcast(mn);
                    uint256 hash = mnb.GetHash();
                    pfrom->PushInventory(CInv(MSG_FRENCHNODE_ANNOUNCE, hash));
                    nInvCount++;

                    if (!mapSeenFrenchnodeBroadcast.count(hash)) mapSeenFrenchnodeBroadcast.insert(make_pair(hash, mnb));

                    if (vin == mn.vin) {
                        LogPrint("masternode", "dseg - Sent 1 Frenchnode entry to peer %i\n", pfrom->GetId());
                        return;
                    }
                }
            }
        }

        if (vin == CTxIn()) {
            pfrom->PushMessage("ssc", FRENCHNODE_SYNC_LIST, nInvCount);
            LogPrint("masternode", "dseg - Sent %d Frenchnode entries to peer %i\n", nInvCount, pfrom->GetId());
        }
    }
}

void CFrenchnodeMan::Remove(CTxIn vin)
{
    LOCK(cs);

    vector<CFrenchnode>::iterator it = vFrenchnodes.begin();
    while (it != vFrenchnodes.end()) {
        if ((*it).vin == vin) {
            LogPrint("masternode", "CFrenchnodeMan: Removing Frenchnode %s - %i now\n", (*it).vin.prevout.hash.ToString(), size() - 1);
            vFrenchnodes.erase(it);
            break;
        }
        ++it;
    }
}

void CFrenchnodeMan::UpdateFrenchnodeList(CFrenchnodeBroadcast mnb)
{
    LOCK(cs);
    mapSeenFrenchnodePing.insert(std::make_pair(mnb.lastPing.GetHash(), mnb.lastPing));
    mapSeenFrenchnodeBroadcast.insert(std::make_pair(mnb.GetHash(), mnb));

    LogPrint("masternode","CFrenchnodeMan::UpdateFrenchnodeList -- masternode=%s\n", mnb.vin.prevout.ToStringShort());

    CFrenchnode* pmn = Find(mnb.vin);
    if (pmn == NULL) {
        CFrenchnode mn(mnb);
        if (Add(mn)) {
            masternodeSync.AddedFrenchnodeList(mnb.GetHash());
        }
    } else if (pmn->UpdateFromNewBroadcast(mnb)) {
        masternodeSync.AddedFrenchnodeList(mnb.GetHash());
    }
}

std::string CFrenchnodeMan::ToString() const
{
    std::ostringstream info;

    info << "Frenchnodes: " << (int)vFrenchnodes.size() << ", peers who asked us for Frenchnode list: " << (int)mAskedUsForFrenchnodeList.size() << ", peers we asked for Frenchnode list: " << (int)mWeAskedForFrenchnodeList.size() << ", entries in Frenchnode list we asked for: " << (int)mWeAskedForFrenchnodeListEntry.size();

    return info.str();
}
