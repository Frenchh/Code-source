// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef FRENCHNODE_SYNC_H
#define FRENCHNODE_SYNC_H

#define FRENCHNODE_SYNC_INITIAL 0
#define FRENCHNODE_SYNC_SPORKS 1
#define FRENCHNODE_SYNC_LIST 2
#define FRENCHNODE_SYNC_MNW 3
#define FRENCHNODE_SYNC_BUDGET 4
#define FRENCHNODE_SYNC_BUDGET_PROP 10
#define FRENCHNODE_SYNC_BUDGET_FIN 11
#define FRENCHNODE_SYNC_FAILED 998
#define FRENCHNODE_SYNC_FINISHED 999

#define FRENCHNODE_SYNC_TIMEOUT 5
#define FRENCHNODE_SYNC_THRESHOLD 2

class CFrenchnodeSync;
extern CFrenchnodeSync masternodeSync;

//
// CFrenchnodeSync : Sync masternode assets in stages
//

class CFrenchnodeSync
{
public:
    std::map<uint256, int> mapSeenSyncMNB;
    std::map<uint256, int> mapSeenSyncMNW;
    std::map<uint256, int> mapSeenSyncBudget;

    int64_t lastFrenchnodeList;
    int64_t lastFrenchnodeWinner;
    int64_t lastBudgetItem;
    int64_t lastFailure;
    int nCountFailures;

    // sum of all counts
    int sumFrenchnodeList;
    int sumFrenchnodeWinner;
    int sumBudgetItemProp;
    int sumBudgetItemFin;
    // peers that reported counts
    int countFrenchnodeList;
    int countFrenchnodeWinner;
    int countBudgetItemProp;
    int countBudgetItemFin;

    // Count peers we've requested the list from
    int RequestedFrenchnodeAssets;
    int RequestedFrenchnodeAttempt;

    // Time when current masternode asset sync started
    int64_t nAssetSyncStarted;

    CFrenchnodeSync();

    void AddedFrenchnodeList(uint256 hash);
    void AddedFrenchnodeWinner(uint256 hash);
    void AddedBudgetItem(uint256 hash);
    void GetNextAsset();
    std::string GetSyncStatus();
    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    bool IsBudgetFinEmpty();
    bool IsBudgetPropEmpty();

    void Reset();
    void Process();
    bool IsSynced();
    bool IsBlockchainSynced();
    bool IsFrenchnodeListSynced() { return RequestedFrenchnodeAssets > FRENCHNODE_SYNC_LIST; }
    void ClearFulfilledRequest();
};

#endif
