// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activemasternode.h"
#include "masternode-payments.h"
#include "addrman.h"
#include "masternode-budget.h"
#include "masternode-sync.h"
#include "masternodeman.h"
#include "masternode-helpers.h"
#include "masternodeconfig.h"
#include "spork.h"
#include "sync.h"
#include "util.h"
#include "utilmoneystr.h"
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>

/** Object for who's going to get paid on which blocks */
CFrenchnodePayments masternodePayments;

CCriticalSection cs_vecPayments;
CCriticalSection cs_mapFrenchnodeBlocks;
CCriticalSection cs_mapFrenchnodePayeeVotes;

//
// CFrenchnodePaymentDB
//

CFrenchnodePaymentDB::CFrenchnodePaymentDB()
{
    pathDB = GetDataDir() / "mnpayments.dat";
    strMagicMessage = "FrenchnodePayments";
}

bool CFrenchnodePaymentDB::Write(const CFrenchnodePayments& objToSave)
{
    int64_t nStart = GetTimeMillis();

    // serialize, checksum data up to that point, then append checksum
    CDataStream ssObj(SER_DISK, CLIENT_VERSION);
    ssObj << strMagicMessage;                   // masternode cache file specific magic message
    ssObj << FLATDATA(Params().MessageStart()); // network specific magic number
    ssObj << objToSave;
    uint256 hash = Hash(ssObj.begin(), ssObj.end());
    ssObj << hash;

    // open output file, and associate with CAutoFile
    FILE* file = fopen(pathDB.string().c_str(), "wb");
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s : Failed to open file %s", __func__, pathDB.string());

    // Write and commit header, data
    try {
        fileout << ssObj;
    } catch (std::exception& e) {
        return error("%s : Serialize or I/O error - %s", __func__, e.what());
    }
    fileout.fclose();

    LogPrint("masternode","Written info to mnpayments.dat  %dms\n", GetTimeMillis() - nStart);

    return true;
}

CFrenchnodePaymentDB::ReadResult CFrenchnodePaymentDB::Read(CFrenchnodePayments& objToLoad, bool fDryRun)
{
    int64_t nStart = GetTimeMillis();
    // open input file, and associate with CAutoFile
    FILE* file = fopen(pathDB.string().c_str(), "rb");
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
    if (filein.IsNull()) {
        error("%s : Failed to open file %s", __func__, pathDB.string());
        return FileError;
    }

    // use file size to size memory buffer
    int fileSize = boost::filesystem::file_size(pathDB);
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

    CDataStream ssObj(vchData, SER_DISK, CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssObj.begin(), ssObj.end());
    if (hashIn != hashTmp) {
        error("%s : Checksum mismatch, data corrupted", __func__);
        return IncorrectHash;
    }

    unsigned char pchMsgTmp[4];
    std::string strMagicMessageTmp;
    try {
        // de-serialize file header (masternode cache file specific magic message) and ..
        ssObj >> strMagicMessageTmp;

        // ... verify the message matches predefined one
        if (strMagicMessage != strMagicMessageTmp) {
            error("%s : Invalid masternode payement cache magic message", __func__);
            return IncorrectMagicMessage;
        }


        // de-serialize file header (network specific magic number) and ..
        ssObj >> FLATDATA(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp))) {
            error("%s : Invalid network magic number", __func__);
            return IncorrectMagicNumber;
        }

        // de-serialize data into CFrenchnodePayments object
        ssObj >> objToLoad;
    } catch (std::exception& e) {
        objToLoad.Clear();
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return IncorrectFormat;
    }

    LogPrint("masternode","Loaded info from mnpayments.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrint("masternode","  %s\n", objToLoad.ToString());
    if (!fDryRun) {
        LogPrint("masternode","Frenchnode payments manager - cleaning....\n");
        objToLoad.CleanPaymentList();
        LogPrint("masternode","Frenchnode payments manager - result:\n");
        LogPrint("masternode","  %s\n", objToLoad.ToString());
    }

    return Ok;
}

void DumpFrenchnodePayments()
{
    int64_t nStart = GetTimeMillis();

    CFrenchnodePaymentDB paymentdb;
    CFrenchnodePayments tempPayments;

    LogPrint("masternode","Verifying mnpayments.dat format...\n");
    CFrenchnodePaymentDB::ReadResult readResult = paymentdb.Read(tempPayments, true);
    // there was an error and it was not an error on file opening => do not proceed
    if (readResult == CFrenchnodePaymentDB::FileError)
        LogPrint("masternode","Missing budgets file - mnpayments.dat, will try to recreate\n");
    else if (readResult != CFrenchnodePaymentDB::Ok) {
        LogPrint("masternode","Error reading mnpayments.dat: ");
        if (readResult == CFrenchnodePaymentDB::IncorrectFormat)
            LogPrint("masternode","magic is ok but data has invalid format, will try to recreate\n");
        else {
            LogPrint("masternode","file format is unknown or invalid, please fix it manually\n");
            return;
        }
    }
    LogPrint("masternode","Writting info to mnpayments.dat...\n");
    paymentdb.Write(masternodePayments);

    LogPrint("masternode","Budget dump finished  %dms\n", GetTimeMillis() - nStart);
}

bool IsBlockValueValid(const CBlock& block, CAmount nExpectedValue, CAmount nMinted)
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (pindexPrev == NULL) return true;

    int nHeight = 0;
    if (pindexPrev->GetBlockHash() == block.hashPrevBlock) {
        nHeight = pindexPrev->nHeight + 1;
    } else { //out of order
        BlockMap::iterator mi = mapBlockIndex.find(block.hashPrevBlock);
        if (mi != mapBlockIndex.end() && (*mi).second)
            nHeight = (*mi).second->nHeight + 1;
    }

    if (nHeight == 0) {
        LogPrint("masternode","IsBlockValueValid() : WARNING: Couldn't find previous block\n");
    }

    if (!masternodeSync.IsSynced()) { //there is no budget data to use to check anything
        //super blocks will always be on these blocks, max 100 per budgeting
        if (nHeight % GetBudgetPaymentCycleBlocks() < 100) {
            return true;
        } else {
            if (nMinted > nExpectedValue) {
                return false;
            }
        }
    } else { // we're synced and have data so check the budget schedule

        //are these blocks even enabled
        if (!IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS)) {
            return nMinted <= nExpectedValue;
        }

        if (budget.IsBudgetPaymentBlock(nHeight)) {
            //the value of the block is evaluated in CheckBlock
            return true;
        } else {
            if (nMinted > nExpectedValue) {
                return false;
            }
        }
    }

    return true;
}

bool IsBlockPayeeValid(const CBlock& block, int nBlockHeight)
{
    if (!masternodeSync.IsSynced()) { //there is no budget data to use to check anything -- find the longest chain
        LogPrint("mnpayments", "Client not synced, skipping block payee checks\n");
        return true;
    }

    const CTransaction& txNew = (nBlockHeight > Params().LAST_POW_BLOCK() ? block.vtx[1] : block.vtx[0]);

    //check if it's a budget block
    if (IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS)) {
        if (budget.IsBudgetPaymentBlock(nBlockHeight)) {
            if (budget.IsTransactionValid(txNew, nBlockHeight))
                return true;

            LogPrint("masternode","Invalid budget payment detected %s\n", txNew.ToString().c_str());
            if (IsSporkActive(SPORK_9_FRENCHNODE_BUDGET_ENFORCEMENT))
                return false;

            LogPrint("masternode","Budget enforcement is disabled, accepting block\n");
            return true;
        }
    }

    //check for masternode payee
    if (masternodePayments.IsTransactionValid(txNew, nBlockHeight))
        return true;
    LogPrint("masternode","Invalid mn payment detected %s\n", txNew.ToString().c_str());

    if (IsSporkActive(SPORK_8_FRENCHNODE_PAYMENT_ENFORCEMENT))
        return false;
    LogPrint("masternode","Frenchnode payment enforcement is disabled, accepting block\n");

    return true;
}


void FillBlockPayee(CMutableTransaction& txNew, CAmount nFees, bool fProofOfStake)
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (!pindexPrev) return;

    if (IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS) && budget.IsBudgetPaymentBlock(pindexPrev->nHeight + 1)) {
        budget.FillBlockPayee(txNew, nFees, fProofOfStake);
    } else {
        masternodePayments.FillBlockPayee(txNew, nFees, fProofOfStake);
    }
}

std::string GetRequiredPaymentsString(int nBlockHeight)
{
    if (IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS) && budget.IsBudgetPaymentBlock(nBlockHeight)) {
        return budget.GetRequiredPaymentsString(nBlockHeight);
    } else {
        return masternodePayments.GetRequiredPaymentsString(nBlockHeight);
    }
}

void CFrenchnodePayments::FillBlockPayee(CMutableTransaction& txNew, int64_t nFees, bool fProofOfStake)
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (!pindexPrev) return;

    bool hasPayment = true;
    CScript payee;

    //spork
    if (!masternodePayments.GetBlockPayee(pindexPrev->nHeight + 1, payee)) {
        //no masternode detected
        CFrenchnode* winningNode = mnodeman.GetCurrentMasterNode(1);
        if (winningNode) {
            payee = GetScriptForDestination(winningNode->pubKeyCollateralAddress.GetID());
        } else {
            LogPrint("masternode","CreateNewBlock: Failed to detect masternode to pay\n");
            hasPayment = false;
        }
    }

    CAmount blockValue = GetBlockValue(pindexPrev->nHeight);
    CAmount masternodePayment = GetFrenchnodePayment(pindexPrev->nHeight, blockValue);

    if (hasPayment) {
        if (fProofOfStake) {
            /**For Proof Of Stake vout[0] must be null
             * Stake reward can be split into many different outputs, so we must
             * use vout.size() to align with several different cases.
             * An additional output is appended as the masternode payment
             */
            unsigned int i = txNew.vout.size();
            txNew.vout.resize(i + 1);
            txNew.vout[i].scriptPubKey = payee;
            txNew.vout[i].nValue = masternodePayment;

            //subtract mn payment from the stake reward
            txNew.vout[i - 1].nValue -= masternodePayment;
        } else {
            txNew.vout.resize(2);
            txNew.vout[1].scriptPubKey = payee;
            txNew.vout[1].nValue = masternodePayment;
            txNew.vout[0].nValue = blockValue - masternodePayment;
        }

        CTxDestination address1;
        ExtractDestination(payee, address1);
        CBitcoinAddress address2(address1);

        LogPrint("masternode","Frenchnode payment of %s to %s\n", FormatMoney(masternodePayment).c_str(), address2.ToString().c_str());
    }
}

int CFrenchnodePayments::GetMinFrenchnodePaymentsProto()
{
    if (IsSporkActive(SPORK_10_FRENCHNODE_PAY_UPDATED_NODES))
        return ActiveProtocol();                          // Allow only updated peers
    else
        return MIN_PEER_PROTO_VERSION_BEFORE_ENFORCEMENT; // Also allow old peers as long as they are allowed to run
}

void CFrenchnodePayments::ProcessMessageFrenchnodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (!masternodeSync.IsBlockchainSynced()) return;

    if (fLiteMode) return; //disable all Frenchnode related functionality


    if (strCommand == "mnget") { //Frenchnode Payments Request Sync
        if (fLiteMode) return;   //disable all Frenchnode related functionality

        int nCountNeeded;
        vRecv >> nCountNeeded;

        if (Params().NetworkID() == CBaseChainParams::MAIN) {
            if (pfrom->HasFulfilledRequest("mnget")) {
                LogPrint("masternode","mnget - peer already asked me for the list\n");
                Misbehaving(pfrom->GetId(), 20);
                return;
            }
        }

        pfrom->FulfilledRequest("mnget");
        masternodePayments.Sync(pfrom, nCountNeeded);
        LogPrint("mnpayments", "mnget - Sent Frenchnode winners to peer %i\n", pfrom->GetId());
    } else if (strCommand == "mnw") { //Frenchnode Payments Declare Winner
        //this is required in litemodef
        CFrenchnodePaymentWinner winner;
        vRecv >> winner;

        if (pfrom->nVersion < ActiveProtocol()) return;

        int nHeight;
        {
            TRY_LOCK(cs_main, locked);
            if (!locked || chainActive.Tip() == NULL) return;
            nHeight = chainActive.Tip()->nHeight;
        }

        if (masternodePayments.mapFrenchnodePayeeVotes.count(winner.GetHash())) {
            LogPrint("mnpayments", "mnw - Already seen - %s bestHeight %d\n", winner.GetHash().ToString().c_str(), nHeight);
            masternodeSync.AddedFrenchnodeWinner(winner.GetHash());
            return;
        }

        int nFirstBlock = nHeight - (mnodeman.CountEnabled() * 1.25);
        if (winner.nBlockHeight < nFirstBlock || winner.nBlockHeight > nHeight + 20) {
            LogPrint("mnpayments", "mnw - winner out of range - FirstBlock %d Height %d bestHeight %d\n", nFirstBlock, winner.nBlockHeight, nHeight);
            return;
        }

        std::string strError = "";
        if (!winner.IsValid(pfrom, strError)) {
            // if(strError != "") LogPrint("masternode","mnw - invalid message - %s\n", strError);
            return;
        }

        if (!masternodePayments.CanVote(winner.vinFrenchnode.prevout, winner.nBlockHeight)) {
            //  LogPrint("masternode","mnw - masternode already voted - %s\n", winner.vinFrenchnode.prevout.ToStringShort());
            return;
        }

        if (!winner.SignatureValid()) {
            // LogPrint("masternode","mnw - invalid signature\n");
            if (masternodeSync.IsSynced()) Misbehaving(pfrom->GetId(), 20);
            // it could just be a non-synced masternode
            mnodeman.AskForMN(pfrom, winner.vinFrenchnode);
            return;
        }

        CTxDestination address1;
        ExtractDestination(winner.payee, address1);
        CBitcoinAddress address2(address1);

        //   LogPrint("mnpayments", "mnw - winning vote - Addr %s Height %d bestHeight %d - %s\n", address2.ToString().c_str(), winner.nBlockHeight, nHeight, winner.vinFrenchnode.prevout.ToStringShort());

        if (masternodePayments.AddWinningFrenchnode(winner)) {
            winner.Relay();
            masternodeSync.AddedFrenchnodeWinner(winner.GetHash());
        }
    }
}

bool CFrenchnodePaymentWinner::Sign(CKey& keyFrenchnode, CPubKey& pubKeyFrenchnode)
{
    std::string errorMessage;
    std::string strMasterNodeSignMessage;

    std::string strMessage = vinFrenchnode.prevout.ToStringShort() +
                             boost::lexical_cast<std::string>(nBlockHeight) +
                             payee.ToString();

    if (!masternodeSigner.SignMessage(strMessage, errorMessage, vchSig, keyFrenchnode)) {
        LogPrint("masternode","CFrenchnodePing::Sign() - Error: %s\n", errorMessage.c_str());
        return false;
    }

    if (!masternodeSigner.VerifyMessage(pubKeyFrenchnode, vchSig, strMessage, errorMessage)) {
        LogPrint("masternode","CFrenchnodePing::Sign() - Error: %s\n", errorMessage.c_str());
        return false;
    }

    return true;
}

bool CFrenchnodePayments::GetBlockPayee(int nBlockHeight, CScript& payee)
{
    if (mapFrenchnodeBlocks.count(nBlockHeight)) {
        return mapFrenchnodeBlocks[nBlockHeight].GetPayee(payee);
    }

    return false;
}

// Is this masternode scheduled to get paid soon?
// -- Only look ahead up to 8 blocks to allow for propagation of the latest 2 winners
bool CFrenchnodePayments::IsScheduled(CFrenchnode& mn, int nNotBlockHeight)
{
    LOCK(cs_mapFrenchnodeBlocks);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || chainActive.Tip() == NULL) return false;
        nHeight = chainActive.Tip()->nHeight;
    }

    CScript mnpayee;
    mnpayee = GetScriptForDestination(mn.pubKeyCollateralAddress.GetID());

    CScript payee;
    for (int64_t h = nHeight; h <= nHeight + 8; h++) {
        if (h == nNotBlockHeight) continue;
        if (mapFrenchnodeBlocks.count(h)) {
            if (mapFrenchnodeBlocks[h].GetPayee(payee)) {
                if (mnpayee == payee) {
                    return true;
                }
            }
        }
    }

    return false;
}

bool CFrenchnodePayments::AddWinningFrenchnode(CFrenchnodePaymentWinner& winnerIn)
{
    uint256 blockHash = 0;
    if (!GetBlockHash(blockHash, winnerIn.nBlockHeight - 100)) {
        return false;
    }

    {
        LOCK2(cs_mapFrenchnodePayeeVotes, cs_mapFrenchnodeBlocks);

        if (mapFrenchnodePayeeVotes.count(winnerIn.GetHash())) {
            return false;
        }

        mapFrenchnodePayeeVotes[winnerIn.GetHash()] = winnerIn;

        if (!mapFrenchnodeBlocks.count(winnerIn.nBlockHeight)) {
            CFrenchnodeBlockPayees blockPayees(winnerIn.nBlockHeight);
            mapFrenchnodeBlocks[winnerIn.nBlockHeight] = blockPayees;
        }
    }

    mapFrenchnodeBlocks[winnerIn.nBlockHeight].AddPayee(winnerIn.payee, 1);

    return true;
}

bool CFrenchnodeBlockPayees::IsTransactionValid(const CTransaction& txNew)
{
    LOCK(cs_vecPayments);

    int nMaxSignatures = 0;
    int nFrenchnode_Drift_Count = 0;

    std::string strPayeesPossible = "";

    CAmount nReward = GetBlockValue(nBlockHeight);

    if (IsSporkActive(SPORK_8_FRENCHNODE_PAYMENT_ENFORCEMENT)) {
        // Get a stable number of masternodes by ignoring newly activated (< 8000 sec old) masternodes
        nFrenchnode_Drift_Count = mnodeman.stable_size() + Params().FrenchnodeCountDrift();
    }
    else {
        //account for the fact that all peers do not see the same masternode count. A allowance of being off our masternode count is given
        //we only need to look at an increased masternode count because as count increases, the reward decreases. This code only checks
        //for mnPayment >= required, so it only makes sense to check the max node count allowed.
        nFrenchnode_Drift_Count = mnodeman.size() + Params().FrenchnodeCountDrift();
    }

    CAmount requiredFrenchnodePayment = GetFrenchnodePayment(nBlockHeight, nReward, nFrenchnode_Drift_Count);

    //require at least 6 signatures
    BOOST_FOREACH (CFrenchnodePayee& payee, vecPayments)
        if (payee.nVotes >= nMaxSignatures && payee.nVotes >= MNPAYMENTS_SIGNATURES_REQUIRED)
            nMaxSignatures = payee.nVotes;

    // if we don't have at least 6 signatures on a payee, approve whichever is the longest chain
    if (nMaxSignatures < MNPAYMENTS_SIGNATURES_REQUIRED) return true;

    BOOST_FOREACH (CFrenchnodePayee& payee, vecPayments) {
        bool found = false;
        BOOST_FOREACH (CTxOut out, txNew.vout) {
            if (payee.scriptPubKey == out.scriptPubKey) {
                if(out.nValue >= requiredFrenchnodePayment)
                    found = true;
                else
                    LogPrint("masternode","Frenchnode payment is out of drift range. Paid=%s Min=%s\n", FormatMoney(out.nValue).c_str(), FormatMoney(requiredFrenchnodePayment).c_str());
            }
        }

        if (payee.nVotes >= MNPAYMENTS_SIGNATURES_REQUIRED) {
            if (found) return true;

            CTxDestination address1;
            ExtractDestination(payee.scriptPubKey, address1);
            CBitcoinAddress address2(address1);

            if (strPayeesPossible == "") {
                strPayeesPossible += address2.ToString();
            } else {
                strPayeesPossible += "," + address2.ToString();
            }
        }
    }

    LogPrint("masternode","CFrenchnodePayments::IsTransactionValid - Missing required payment of %s to %s\n", FormatMoney(requiredFrenchnodePayment).c_str(), strPayeesPossible.c_str());
    return false;
}

std::string CFrenchnodeBlockPayees::GetRequiredPaymentsString()
{
    LOCK(cs_vecPayments);

    std::string ret = "Unknown";

    BOOST_FOREACH (CFrenchnodePayee& payee, vecPayments) {
        CTxDestination address1;
        ExtractDestination(payee.scriptPubKey, address1);
        CBitcoinAddress address2(address1);

        if (ret != "Unknown") {
            ret += ", " + address2.ToString() + ":" + boost::lexical_cast<std::string>(payee.nVotes);
        } else {
            ret = address2.ToString() + ":" + boost::lexical_cast<std::string>(payee.nVotes);
        }
    }

    return ret;
}

std::string CFrenchnodePayments::GetRequiredPaymentsString(int nBlockHeight)
{
    LOCK(cs_mapFrenchnodeBlocks);

    if (mapFrenchnodeBlocks.count(nBlockHeight)) {
        return mapFrenchnodeBlocks[nBlockHeight].GetRequiredPaymentsString();
    }

    return "Unknown";
}

bool CFrenchnodePayments::IsTransactionValid(const CTransaction& txNew, int nBlockHeight)
{
    LOCK(cs_mapFrenchnodeBlocks);

    if (mapFrenchnodeBlocks.count(nBlockHeight)) {
        return mapFrenchnodeBlocks[nBlockHeight].IsTransactionValid(txNew);
    }

    return true;
}

void CFrenchnodePayments::CleanPaymentList()
{
    LOCK2(cs_mapFrenchnodePayeeVotes, cs_mapFrenchnodeBlocks);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || chainActive.Tip() == NULL) return;
        nHeight = chainActive.Tip()->nHeight;
    }

    //keep up to five cycles for historical sake
    int nLimit = std::max(int(mnodeman.size() * 1.25), 1000);

    std::map<uint256, CFrenchnodePaymentWinner>::iterator it = mapFrenchnodePayeeVotes.begin();
    while (it != mapFrenchnodePayeeVotes.end()) {
        CFrenchnodePaymentWinner winner = (*it).second;

        if (nHeight - winner.nBlockHeight > nLimit) {
            LogPrint("mnpayments", "CFrenchnodePayments::CleanPaymentList - Removing old Frenchnode payment - block %d\n", winner.nBlockHeight);
            masternodeSync.mapSeenSyncMNW.erase((*it).first);
            mapFrenchnodePayeeVotes.erase(it++);
            mapFrenchnodeBlocks.erase(winner.nBlockHeight);
        } else {
            ++it;
        }
    }
}

bool CFrenchnodePaymentWinner::IsValid(CNode* pnode, std::string& strError)
{
    CFrenchnode* pmn = mnodeman.Find(vinFrenchnode);

    if (!pmn) {
        strError = strprintf("Unknown Frenchnode %s", vinFrenchnode.prevout.hash.ToString());
        LogPrint("masternode","CFrenchnodePaymentWinner::IsValid - %s\n", strError);
        mnodeman.AskForMN(pnode, vinFrenchnode);
        return false;
    }

    if (pmn->protocolVersion < ActiveProtocol()) {
        strError = strprintf("Frenchnode protocol too old %d - req %d", pmn->protocolVersion, ActiveProtocol());
        LogPrint("masternode","CFrenchnodePaymentWinner::IsValid - %s\n", strError);
        return false;
    }

    int n = mnodeman.GetFrenchnodeRank(vinFrenchnode, nBlockHeight - 100, ActiveProtocol());

    if (n > MNPAYMENTS_SIGNATURES_TOTAL) {
        //It's common to have masternodes mistakenly think they are in the top 10
        // We don't want to print all of these messages, or punish them unless they're way off
        if (n > MNPAYMENTS_SIGNATURES_TOTAL * 2) {
            strError = strprintf("Frenchnode not in the top %d (%d)", MNPAYMENTS_SIGNATURES_TOTAL * 2, n);
            LogPrint("masternode","CFrenchnodePaymentWinner::IsValid - %s\n", strError);
            //if (masternodeSync.IsSynced()) Misbehaving(pnode->GetId(), 20);
        }
        return false;
    }

    return true;
}

bool CFrenchnodePayments::ProcessBlock(int nBlockHeight)
{
    if (!fMasterNode) return false;

    //reference node - hybrid mode

    int n = mnodeman.GetFrenchnodeRank(activeFrenchnode.vin, nBlockHeight - 100, ActiveProtocol());

    if (n == -1) {
        LogPrint("mnpayments", "CFrenchnodePayments::ProcessBlock - Unknown Frenchnode\n");
        return false;
    }

    if (n > MNPAYMENTS_SIGNATURES_TOTAL) {
        LogPrint("mnpayments", "CFrenchnodePayments::ProcessBlock - Frenchnode not in the top %d (%d)\n", MNPAYMENTS_SIGNATURES_TOTAL, n);
        return false;
    }

    if (nBlockHeight <= nLastBlockHeight) return false;

    CFrenchnodePaymentWinner newWinner(activeFrenchnode.vin);

    if (budget.IsBudgetPaymentBlock(nBlockHeight)) {
        //is budget payment block -- handled by the budgeting software
    } else {
        LogPrint("masternode","CFrenchnodePayments::ProcessBlock() Start nHeight %d - vin %s. \n", nBlockHeight, activeFrenchnode.vin.prevout.hash.ToString());

        // pay to the oldest MN that still had no payment but its input is old enough and it was active long enough
        int nCount = 0;
        CFrenchnode* pmn = mnodeman.GetNextFrenchnodeInQueueForPayment(nBlockHeight, true, nCount);

        if (pmn != NULL) {
            LogPrint("masternode","CFrenchnodePayments::ProcessBlock() Found by FindOldestNotInVec \n");

            newWinner.nBlockHeight = nBlockHeight;

            CScript payee = GetScriptForDestination(pmn->pubKeyCollateralAddress.GetID());
            newWinner.AddPayee(payee);

            CTxDestination address1;
            ExtractDestination(payee, address1);
            CBitcoinAddress address2(address1);

            LogPrint("masternode","CFrenchnodePayments::ProcessBlock() Winner payee %s nHeight %d. \n", address2.ToString().c_str(), newWinner.nBlockHeight);
        } else {
            LogPrint("masternode","CFrenchnodePayments::ProcessBlock() Failed to find masternode to pay\n");
        }
    }

    std::string errorMessage;
    CPubKey pubKeyFrenchnode;
    CKey keyFrenchnode;

    if (!masternodeSigner.SetKey(strMasterNodePrivKey, errorMessage, keyFrenchnode, pubKeyFrenchnode)) {
        LogPrint("masternode","CFrenchnodePayments::ProcessBlock() - Error upon calling SetKey: %s\n", errorMessage.c_str());
        return false;
    }

    LogPrint("masternode","CFrenchnodePayments::ProcessBlock() - Signing Winner\n");
    if (newWinner.Sign(keyFrenchnode, pubKeyFrenchnode)) {
        LogPrint("masternode","CFrenchnodePayments::ProcessBlock() - AddWinningFrenchnode\n");

        if (AddWinningFrenchnode(newWinner)) {
            newWinner.Relay();
            nLastBlockHeight = nBlockHeight;
            return true;
        }
    }

    return false;
}

void CFrenchnodePaymentWinner::Relay()
{
    CInv inv(MSG_FRENCHNODE_WINNER, GetHash());
    RelayInv(inv);
}

bool CFrenchnodePaymentWinner::SignatureValid()
{
    CFrenchnode* pmn = mnodeman.Find(vinFrenchnode);

    if (pmn != NULL) {
        std::string strMessage = vinFrenchnode.prevout.ToStringShort() +
                                 boost::lexical_cast<std::string>(nBlockHeight) +
                                 payee.ToString();

        std::string errorMessage = "";
        if (!masternodeSigner.VerifyMessage(pmn->pubKeyFrenchnode, vchSig, strMessage, errorMessage)) {
            return error("CFrenchnodePaymentWinner::SignatureValid() - Got bad Frenchnode address signature %s\n", vinFrenchnode.prevout.hash.ToString());
        }

        return true;
    }

    return false;
}

void CFrenchnodePayments::Sync(CNode* node, int nCountNeeded)
{
    LOCK(cs_mapFrenchnodePayeeVotes);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || chainActive.Tip() == NULL) return;
        nHeight = chainActive.Tip()->nHeight;
    }

    int nCount = (mnodeman.CountEnabled() * 1.25);
    if (nCountNeeded > nCount) nCountNeeded = nCount;

    int nInvCount = 0;
    std::map<uint256, CFrenchnodePaymentWinner>::iterator it = mapFrenchnodePayeeVotes.begin();
    while (it != mapFrenchnodePayeeVotes.end()) {
        CFrenchnodePaymentWinner winner = (*it).second;
        if (winner.nBlockHeight >= nHeight - nCountNeeded && winner.nBlockHeight <= nHeight + 20) {
            node->PushInventory(CInv(MSG_FRENCHNODE_WINNER, winner.GetHash()));
            nInvCount++;
        }
        ++it;
    }
    node->PushMessage("ssc", FRENCHNODE_SYNC_MNW, nInvCount);
}

std::string CFrenchnodePayments::ToString() const
{
    std::ostringstream info;

    info << "Votes: " << (int)mapFrenchnodePayeeVotes.size() << ", Blocks: " << (int)mapFrenchnodeBlocks.size();

    return info.str();
}


int CFrenchnodePayments::GetOldestBlock()
{
    LOCK(cs_mapFrenchnodeBlocks);

    int nOldestBlock = std::numeric_limits<int>::max();

    std::map<int, CFrenchnodeBlockPayees>::iterator it = mapFrenchnodeBlocks.begin();
    while (it != mapFrenchnodeBlocks.end()) {
        if ((*it).first < nOldestBlock) {
            nOldestBlock = (*it).first;
        }
        it++;
    }

    return nOldestBlock;
}


int CFrenchnodePayments::GetNewestBlock()
{
    LOCK(cs_mapFrenchnodeBlocks);

    int nNewestBlock = 0;

    std::map<int, CFrenchnodeBlockPayees>::iterator it = mapFrenchnodeBlocks.begin();
    while (it != mapFrenchnodeBlocks.end()) {
        if ((*it).first > nNewestBlock) {
            nNewestBlock = (*it).first;
        }
        it++;
    }

    return nNewestBlock;
}
