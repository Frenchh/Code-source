// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// clang-format off
#include "main.h"
#include "activemasternode.h"
#include "masternode-sync.h"
#include "masternode-payments.h"
#include "masternode-budget.h"
#include "masternode.h"
#include "masternodeman.h"
#include "spork.h"
#include "util.h"
#include "addrman.h"
// clang-format on

class CFrenchnodeSync;
CFrenchnodeSync masternodeSync;

CFrenchnodeSync::CFrenchnodeSync()
{
    Reset();
}

bool CFrenchnodeSync::IsSynced()
{
    return RequestedFrenchnodeAssets == FRENCHNODE_SYNC_FINISHED;
}

bool CFrenchnodeSync::IsBlockchainSynced()
{
    static bool fBlockchainSynced = false;
    static int64_t lastProcess = GetTime();

    // if the last call to this function was more than 60 minutes ago (client was in sleep mode) reset the sync process
    if (GetTime() - lastProcess > 60 * 60) {
        Reset();
        fBlockchainSynced = false;
    }
    lastProcess = GetTime();

    if (fBlockchainSynced) return true;

    if (fImporting || fReindex) return false;

    TRY_LOCK(cs_main, lockMain);
    if (!lockMain) return false;

    CBlockIndex* pindex = chainActive.Tip();
    if (pindex == NULL) return false;


    if (pindex->nTime + 60 * 60 < GetTime())
        return false;

    fBlockchainSynced = true;

    return true;
}

void CFrenchnodeSync::Reset()
{
    lastFrenchnodeList = 0;
    lastFrenchnodeWinner = 0;
    lastBudgetItem = 0;
    mapSeenSyncMNB.clear();
    mapSeenSyncMNW.clear();
    mapSeenSyncBudget.clear();
    lastFailure = 0;
    nCountFailures = 0;
    sumFrenchnodeList = 0;
    sumFrenchnodeWinner = 0;
    sumBudgetItemProp = 0;
    sumBudgetItemFin = 0;
    countFrenchnodeList = 0;
    countFrenchnodeWinner = 0;
    countBudgetItemProp = 0;
    countBudgetItemFin = 0;
    RequestedFrenchnodeAssets = FRENCHNODE_SYNC_INITIAL;
    RequestedFrenchnodeAttempt = 0;
    nAssetSyncStarted = GetTime();
}

void CFrenchnodeSync::AddedFrenchnodeList(uint256 hash)
{
    if (mnodeman.mapSeenFrenchnodeBroadcast.count(hash)) {
        if (mapSeenSyncMNB[hash] < FRENCHNODE_SYNC_THRESHOLD) {
            lastFrenchnodeList = GetTime();
            mapSeenSyncMNB[hash]++;
        }
    } else {
        lastFrenchnodeList = GetTime();
        mapSeenSyncMNB.insert(make_pair(hash, 1));
    }
}

void CFrenchnodeSync::AddedFrenchnodeWinner(uint256 hash)
{
    if (masternodePayments.mapFrenchnodePayeeVotes.count(hash)) {
        if (mapSeenSyncMNW[hash] < FRENCHNODE_SYNC_THRESHOLD) {
            lastFrenchnodeWinner = GetTime();
            mapSeenSyncMNW[hash]++;
        }
    } else {
        lastFrenchnodeWinner = GetTime();
        mapSeenSyncMNW.insert(make_pair(hash, 1));
    }
}

void CFrenchnodeSync::AddedBudgetItem(uint256 hash)
{
    if (budget.mapSeenFrenchnodeBudgetProposals.count(hash) || budget.mapSeenFrenchnodeBudgetVotes.count(hash) ||
        budget.mapSeenFinalizedBudgets.count(hash) || budget.mapSeenFinalizedBudgetVotes.count(hash)) {
        if (mapSeenSyncBudget[hash] < FRENCHNODE_SYNC_THRESHOLD) {
            lastBudgetItem = GetTime();
            mapSeenSyncBudget[hash]++;
        }
    } else {
        lastBudgetItem = GetTime();
        mapSeenSyncBudget.insert(make_pair(hash, 1));
    }
}

bool CFrenchnodeSync::IsBudgetPropEmpty()
{
    return sumBudgetItemProp == 0 && countBudgetItemProp > 0;
}

bool CFrenchnodeSync::IsBudgetFinEmpty()
{
    return sumBudgetItemFin == 0 && countBudgetItemFin > 0;
}

void CFrenchnodeSync::GetNextAsset()
{
    switch (RequestedFrenchnodeAssets) {
    case (FRENCHNODE_SYNC_INITIAL):
    case (FRENCHNODE_SYNC_FAILED): // should never be used here actually, use Reset() instead
        ClearFulfilledRequest();
        RequestedFrenchnodeAssets = FRENCHNODE_SYNC_SPORKS;
        break;
    case (FRENCHNODE_SYNC_SPORKS):
        RequestedFrenchnodeAssets = FRENCHNODE_SYNC_LIST;
        break;
    case (FRENCHNODE_SYNC_LIST):
        RequestedFrenchnodeAssets = FRENCHNODE_SYNC_MNW;
        break;
    case (FRENCHNODE_SYNC_MNW):
        RequestedFrenchnodeAssets = FRENCHNODE_SYNC_BUDGET;
        break;
    case (FRENCHNODE_SYNC_BUDGET):
        LogPrintf("CFrenchnodeSync::GetNextAsset - Sync has finished\n");
        RequestedFrenchnodeAssets = FRENCHNODE_SYNC_FINISHED;
        break;
    }
    RequestedFrenchnodeAttempt = 0;
    nAssetSyncStarted = GetTime();
}

std::string CFrenchnodeSync::GetSyncStatus()
{
    switch (masternodeSync.RequestedFrenchnodeAssets) {
    case FRENCHNODE_SYNC_INITIAL:
        return _("Synchronization pending...");
    case FRENCHNODE_SYNC_SPORKS:
        return _("Synchronizing sporks...");
    case FRENCHNODE_SYNC_LIST:
        return _("Synchronizing masternodes...");
    case FRENCHNODE_SYNC_MNW:
        return _("Synchronizing masternode winners...");
    case FRENCHNODE_SYNC_BUDGET:
        return _("Synchronizing budgets...");
    case FRENCHNODE_SYNC_FAILED:
        return _("Synchronization failed");
    case FRENCHNODE_SYNC_FINISHED:
        return _("Synchronization finished");
    }
    return "";
}

void CFrenchnodeSync::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (strCommand == "ssc") { //Sync status count
        int nItemID;
        int nCount;
        vRecv >> nItemID >> nCount;

        if (RequestedFrenchnodeAssets >= FRENCHNODE_SYNC_FINISHED) return;

        //this means we will receive no further communication
        switch (nItemID) {
        case (FRENCHNODE_SYNC_LIST):
            if (nItemID != RequestedFrenchnodeAssets) return;
            sumFrenchnodeList += nCount;
            countFrenchnodeList++;
            break;
        case (FRENCHNODE_SYNC_MNW):
            if (nItemID != RequestedFrenchnodeAssets) return;
            sumFrenchnodeWinner += nCount;
            countFrenchnodeWinner++;
            break;
        case (FRENCHNODE_SYNC_BUDGET_PROP):
            if (RequestedFrenchnodeAssets != FRENCHNODE_SYNC_BUDGET) return;
            sumBudgetItemProp += nCount;
            countBudgetItemProp++;
            break;
        case (FRENCHNODE_SYNC_BUDGET_FIN):
            if (RequestedFrenchnodeAssets != FRENCHNODE_SYNC_BUDGET) return;
            sumBudgetItemFin += nCount;
            countBudgetItemFin++;
            break;
        }

        LogPrint("masternode", "CFrenchnodeSync:ProcessMessage - ssc - got inventory count %d %d\n", nItemID, nCount);
    }
}

void CFrenchnodeSync::ClearFulfilledRequest()
{
    TRY_LOCK(cs_vNodes, lockRecv);
    if (!lockRecv) return;

    BOOST_FOREACH (CNode* pnode, vNodes) {
        pnode->ClearFulfilledRequest("getspork");
        pnode->ClearFulfilledRequest("mnsync");
        pnode->ClearFulfilledRequest("mnwsync");
        pnode->ClearFulfilledRequest("busync");
    }
}

void CFrenchnodeSync::Process()
{
    static int tick = 0;

    if (tick++ % FRENCHNODE_SYNC_TIMEOUT != 0) return;

    if (IsSynced()) {
        /* 
            Resync if we lose all masternodes from sleep/wake or failure to sync originally
        */
        if (mnodeman.CountEnabled() == 0) {
            Reset();
        } else
            return;
    }

    //try syncing again
    if (RequestedFrenchnodeAssets == FRENCHNODE_SYNC_FAILED && lastFailure + (1 * 60) < GetTime()) {
        Reset();
    } else if (RequestedFrenchnodeAssets == FRENCHNODE_SYNC_FAILED) {
        return;
    }

    LogPrint("masternode", "CFrenchnodeSync::Process() - tick %d RequestedFrenchnodeAssets %d\n", tick, RequestedFrenchnodeAssets);

    if (RequestedFrenchnodeAssets == FRENCHNODE_SYNC_INITIAL) GetNextAsset();

    // sporks synced but blockchain is not, wait until we're almost at a recent block to continue
    if (Params().NetworkID() != CBaseChainParams::REGTEST &&
        !IsBlockchainSynced() && RequestedFrenchnodeAssets > FRENCHNODE_SYNC_SPORKS) return;

    TRY_LOCK(cs_vNodes, lockRecv);
    if (!lockRecv) return;

    BOOST_FOREACH (CNode* pnode, vNodes) {
        if (Params().NetworkID() == CBaseChainParams::REGTEST) {
            if (RequestedFrenchnodeAttempt <= 2) {
                pnode->PushMessage("getsporks"); //get current network sporks
            } else if (RequestedFrenchnodeAttempt < 4) {
                mnodeman.DsegUpdate(pnode);
            } else if (RequestedFrenchnodeAttempt < 6) {
                int nMnCount = mnodeman.CountEnabled();
                pnode->PushMessage("mnget", nMnCount); //sync payees
                uint256 n = 0;
                pnode->PushMessage("mnvs", n); //sync masternode votes
            } else {
                RequestedFrenchnodeAssets = FRENCHNODE_SYNC_FINISHED;
            }
            RequestedFrenchnodeAttempt++;
            return;
        }

        //set to synced
        if (RequestedFrenchnodeAssets == FRENCHNODE_SYNC_SPORKS) {
            if (pnode->HasFulfilledRequest("getspork")) continue;
            pnode->FulfilledRequest("getspork");

            pnode->PushMessage("getsporks"); //get current network sporks
            if (RequestedFrenchnodeAttempt >= 2) GetNextAsset();
            RequestedFrenchnodeAttempt++;

            return;
        }

        if (pnode->nVersion >= masternodePayments.GetMinFrenchnodePaymentsProto()) {
            if (RequestedFrenchnodeAssets == FRENCHNODE_SYNC_LIST) {
                LogPrint("masternode", "CFrenchnodeSync::Process() - lastFrenchnodeList %lld (GetTime() - FRENCHNODE_SYNC_TIMEOUT) %lld\n", lastFrenchnodeList, GetTime() - FRENCHNODE_SYNC_TIMEOUT);
                if (lastFrenchnodeList > 0 && lastFrenchnodeList < GetTime() - FRENCHNODE_SYNC_TIMEOUT * 2 && RequestedFrenchnodeAttempt >= FRENCHNODE_SYNC_THRESHOLD) { //hasn't received a new item in the last five seconds, so we'll move to the
                    GetNextAsset();
                    return;
                }

                if (pnode->HasFulfilledRequest("mnsync")) continue;
                pnode->FulfilledRequest("mnsync");

                // timeout
                if (lastFrenchnodeList == 0 &&
                    (RequestedFrenchnodeAttempt >= FRENCHNODE_SYNC_THRESHOLD * 3 || GetTime() - nAssetSyncStarted > FRENCHNODE_SYNC_TIMEOUT * 5)) {
                    if (IsSporkActive(SPORK_8_FRENCHNODE_PAYMENT_ENFORCEMENT)) {
                        LogPrintf("CFrenchnodeSync::Process - ERROR - Sync has failed, will retry later\n");
                        RequestedFrenchnodeAssets = FRENCHNODE_SYNC_FAILED;
                        RequestedFrenchnodeAttempt = 0;
                        lastFailure = GetTime();
                        nCountFailures++;
                    } else {
                        GetNextAsset();
                    }
                    return;
                }

                if (RequestedFrenchnodeAttempt >= FRENCHNODE_SYNC_THRESHOLD * 3) return;

                mnodeman.DsegUpdate(pnode);
                RequestedFrenchnodeAttempt++;
                return;
            }

            if (RequestedFrenchnodeAssets == FRENCHNODE_SYNC_MNW) {
                if (lastFrenchnodeWinner > 0 && lastFrenchnodeWinner < GetTime() - FRENCHNODE_SYNC_TIMEOUT * 2 && RequestedFrenchnodeAttempt >= FRENCHNODE_SYNC_THRESHOLD) { //hasn't received a new item in the last five seconds, so we'll move to the
                    GetNextAsset();
                    return;
                }

                if (pnode->HasFulfilledRequest("mnwsync")) continue;
                pnode->FulfilledRequest("mnwsync");

                // timeout
                if (lastFrenchnodeWinner == 0 &&
                    (RequestedFrenchnodeAttempt >= FRENCHNODE_SYNC_THRESHOLD * 3 || GetTime() - nAssetSyncStarted > FRENCHNODE_SYNC_TIMEOUT * 5)) {
                    if (IsSporkActive(SPORK_8_FRENCHNODE_PAYMENT_ENFORCEMENT)) {
                        LogPrintf("CFrenchnodeSync::Process - ERROR - Sync has failed, will retry later\n");
                        RequestedFrenchnodeAssets = FRENCHNODE_SYNC_FAILED;
                        RequestedFrenchnodeAttempt = 0;
                        lastFailure = GetTime();
                        nCountFailures++;
                    } else {
                        GetNextAsset();
                    }
                    return;
                }

                if (RequestedFrenchnodeAttempt >= FRENCHNODE_SYNC_THRESHOLD * 3) return;

                CBlockIndex* pindexPrev = chainActive.Tip();
                if (pindexPrev == NULL) return;

                int nMnCount = mnodeman.CountEnabled();
                pnode->PushMessage("mnget", nMnCount); //sync payees
                RequestedFrenchnodeAttempt++;

                return;
            }
        }

        if (pnode->nVersion >= ActiveProtocol()) {
            if (RequestedFrenchnodeAssets == FRENCHNODE_SYNC_BUDGET) {
                
                // We'll start rejecting votes if we accidentally get set as synced too soon
                if (lastBudgetItem > 0 && lastBudgetItem < GetTime() - FRENCHNODE_SYNC_TIMEOUT * 2 && RequestedFrenchnodeAttempt >= FRENCHNODE_SYNC_THRESHOLD) { 
                    
                    // Hasn't received a new item in the last five seconds, so we'll move to the
                    GetNextAsset();

                    // Try to activate our masternode if possible
                    activeFrenchnode.ManageStatus();

                    return;
                }

                // timeout
                if (lastBudgetItem == 0 &&
                    (RequestedFrenchnodeAttempt >= FRENCHNODE_SYNC_THRESHOLD * 3 || GetTime() - nAssetSyncStarted > FRENCHNODE_SYNC_TIMEOUT * 5)) {
                    // maybe there is no budgets at all, so just finish syncing
                    GetNextAsset();
                    activeFrenchnode.ManageStatus();
                    return;
                }

                if (pnode->HasFulfilledRequest("busync")) continue;
                pnode->FulfilledRequest("busync");

                if (RequestedFrenchnodeAttempt >= FRENCHNODE_SYNC_THRESHOLD * 3) return;

                uint256 n = 0;
                pnode->PushMessage("mnvs", n); //sync masternode votes
                RequestedFrenchnodeAttempt++;

                return;
            }
        }
    }
}
