// Copyright (c) 2014-2016 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2017-2018 The French developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activemasternode.h"
#include "addrman.h"
#include "masternode.h"
#include "masternodeconfig.h"
#include "masternodeman.h"
#include "masternode-helpers.h"
#include "protocol.h"
#include "spork.h"

// Keep track of the active Frenchnode
CActiveFrenchnode activeFrenchnode;

//
// Bootup the Frenchnode, look for a FRENCH collateral input and register on the network
//
void CActiveFrenchnode::ManageStatus()
{
    std::string errorMessage;

    if (!fMasterNode) return;

    if (fDebug) LogPrintf("CActiveFrenchnode::ManageStatus() - Begin\n");

    //need correct blocks to send ping
    if (Params().NetworkID() != CBaseChainParams::REGTEST && !masternodeSync.IsBlockchainSynced()) {
        status = ACTIVE_FRENCHNODE_SYNC_IN_PROCESS;
        LogPrintf("CActiveFrenchnode::ManageStatus() - %s\n", GetStatus());
        return;
    }

    if (status == ACTIVE_FRENCHNODE_SYNC_IN_PROCESS) status = ACTIVE_FRENCHNODE_INITIAL;

    if (status == ACTIVE_FRENCHNODE_INITIAL) {
        CFrenchnode* pmn;
        pmn = mnodeman.Find(pubKeyFrenchnode);
        if (pmn != NULL) {
            pmn->Check();
            if (pmn->IsEnabled() && pmn->protocolVersion == PROTOCOL_VERSION) EnableHotColdMasterNode(pmn->vin, pmn->addr);
        }
    }

    if (status != ACTIVE_FRENCHNODE_STARTED) {
        // Set defaults
        status = ACTIVE_FRENCHNODE_NOT_CAPABLE;
        notCapableReason = "";

        if (pwalletMain->IsLocked()) {
            notCapableReason = "Wallet is locked.";
            LogPrintf("CActiveFrenchnode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        if (pwalletMain->GetBalance() == 0) {
            notCapableReason = "Hot node, waiting for remote activation.";
            LogPrintf("CActiveFrenchnode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        if (strMasterNodeAddr.empty()) {
            if (!GetLocal(service)) {
                notCapableReason = "Can't detect external address. Please use the masternodeaddr configuration option.";
                LogPrintf("CActiveFrenchnode::ManageStatus() - not capable: %s\n", notCapableReason);
                return;
            }
        } else {
            service = CService(strMasterNodeAddr);
        }

        // if (Params().NetworkID() == CBaseChainParams::MAIN) {
        //     if (service.GetPort() != 1062) {
        //         notCapableReason = strprintf("Invalid port: %u - only 1062 is supported on mainnet.", service.GetPort());
        //         LogPrintf("CActiveFrenchnode::ManageStatus() - not capable: %s\n", notCapableReason);
        //         return;
        //     }
        // } else if (service.GetPort() == 1062) {
        //     notCapableReason = strprintf("Invalid port: %u - 1062 is only supported on mainnet.", service.GetPort());
        //     LogPrintf("CActiveFrenchnode::ManageStatus() - not capable: %s\n", notCapableReason);
        //     return;
        // }

        LogPrintf("CActiveFrenchnode::ManageStatus() - Checking inbound connection to '%s'\n", service.ToString());

        CNode* pnode = ConnectNode((CAddress)service, NULL);
        if (!pnode) {
            notCapableReason = "Could not connect to " + service.ToString();
            LogPrintf("CActiveFrenchnode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }
        pnode->Release();

        // Choose coins to use
        CPubKey pubKeyCollateralAddress;
        CKey keyCollateralAddress;

        if (GetMasterNodeVin(vin, pubKeyCollateralAddress, keyCollateralAddress)) {
            if (GetInputAge(vin) < FRENCHNODE_MIN_CONFIRMATIONS) {
                status = ACTIVE_FRENCHNODE_INPUT_TOO_NEW;
                notCapableReason = strprintf("%s - %d confirmations", GetStatus(), GetInputAge(vin));
                LogPrintf("CActiveFrenchnode::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            LOCK(pwalletMain->cs_wallet);
            pwalletMain->LockCoin(vin.prevout);

            // send to all nodes
            CPubKey pubKeyFrenchnode;
            CKey keyFrenchnode;

            if (!masternodeSigner.SetKey(strMasterNodePrivKey, errorMessage, keyFrenchnode, pubKeyFrenchnode)) {
                notCapableReason = "Error upon calling SetKey: " + errorMessage;
                LogPrintf("Register::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            if (!Register(vin, service, keyCollateralAddress, pubKeyCollateralAddress, keyFrenchnode, pubKeyFrenchnode, errorMessage)) {
                notCapableReason = "Error on Register: " + errorMessage;
                LogPrintf("Register::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            LogPrintf("CActiveFrenchnode::ManageStatus() - Is capable master node!\n");
            status = ACTIVE_FRENCHNODE_STARTED;

            return;
        } else {
            notCapableReason = "Could not find suitable coins!";
            LogPrintf("CActiveFrenchnode::ManageStatus() - %s\n", notCapableReason);
            return;
        }
    }

    //send to all peers
    if (!SendFrenchnodePing(errorMessage)) {
        LogPrintf("CActiveFrenchnode::ManageStatus() - Error on Ping: %s\n", errorMessage);
    }
}

std::string CActiveFrenchnode::GetStatus()
{
    switch (status) {
    case ACTIVE_FRENCHNODE_INITIAL:
        return "Node just started, not yet activated";
    case ACTIVE_FRENCHNODE_SYNC_IN_PROCESS:
        return "Sync in progress. Must wait until sync is complete to start Frenchnode";
    case ACTIVE_FRENCHNODE_INPUT_TOO_NEW:
        return strprintf("Frenchnode input must have at least %d confirmations", FRENCHNODE_MIN_CONFIRMATIONS);
    case ACTIVE_FRENCHNODE_NOT_CAPABLE:
        return "Not capable masternode: " + notCapableReason;
    case ACTIVE_FRENCHNODE_STARTED:
        return "Frenchnode successfully started";
    default:
        return "unknown";
    }
}

bool CActiveFrenchnode::SendFrenchnodePing(std::string& errorMessage)
{
    if (status != ACTIVE_FRENCHNODE_STARTED) {
        errorMessage = "Frenchnode is not in a running status";
        return false;
    }

    CPubKey pubKeyFrenchnode;
    CKey keyFrenchnode;

    if (!masternodeSigner.SetKey(strMasterNodePrivKey, errorMessage, keyFrenchnode, pubKeyFrenchnode)) {
        errorMessage = strprintf("Error upon calling SetKey: %s\n", errorMessage);
        return false;
    }

    LogPrintf("CActiveFrenchnode::SendFrenchnodePing() - Relay Frenchnode Ping vin = %s\n", vin.ToString());

    CFrenchnodePing mnp(vin);
    if (!mnp.Sign(keyFrenchnode, pubKeyFrenchnode)) {
        errorMessage = "Couldn't sign Frenchnode Ping";
        return false;
    }

    // Update lastPing for our masternode in Frenchnode list
    CFrenchnode* pmn = mnodeman.Find(vin);
    if (pmn != NULL) {
        if (pmn->IsPingedWithin(FRENCHNODE_PING_SECONDS, mnp.sigTime)) {
            errorMessage = "Too early to send Frenchnode Ping";
            return false;
        }

        pmn->lastPing = mnp;
        mnodeman.mapSeenFrenchnodePing.insert(make_pair(mnp.GetHash(), mnp));

        //mnodeman.mapSeenFrenchnodeBroadcast.lastPing is probably outdated, so we'll update it
        CFrenchnodeBroadcast mnb(*pmn);
        uint256 hash = mnb.GetHash();
        if (mnodeman.mapSeenFrenchnodeBroadcast.count(hash)) mnodeman.mapSeenFrenchnodeBroadcast[hash].lastPing = mnp;

        mnp.Relay();

        return true;
    } else {
        // Seems like we are trying to send a ping while the Frenchnode is not registered in the network
        errorMessage = "Frenchnode List doesn't include our Frenchnode, shutting down Frenchnode pinging service! " + vin.ToString();
        status = ACTIVE_FRENCHNODE_NOT_CAPABLE;
        notCapableReason = errorMessage;
        return false;
    }
}

bool CActiveFrenchnode::Register(std::string strService, std::string strKeyFrenchnode, std::string strTxHash, std::string strOutputIndex, std::string& errorMessage)
{
    CTxIn vin;
    CPubKey pubKeyCollateralAddress;
    CKey keyCollateralAddress;
    CPubKey pubKeyFrenchnode;
    CKey keyFrenchnode;

    //need correct blocks to send ping
    if (!masternodeSync.IsBlockchainSynced()) {
        errorMessage = GetStatus();
        LogPrintf("CActiveFrenchnode::Register() - %s\n", errorMessage);
        return false;
    }

    if (!masternodeSigner.SetKey(strKeyFrenchnode, errorMessage, keyFrenchnode, pubKeyFrenchnode)) {
        errorMessage = strprintf("Can't find keys for masternode %s - %s", strService, errorMessage);
        LogPrintf("CActiveFrenchnode::Register() - %s\n", errorMessage);
        return false;
    }

    if (!GetMasterNodeVin(vin, pubKeyCollateralAddress, keyCollateralAddress, strTxHash, strOutputIndex)) {
        errorMessage = strprintf("Could not allocate vin %s:%s for masternode %s", strTxHash, strOutputIndex, strService);
        LogPrintf("CActiveFrenchnode::Register() - %s\n", errorMessage);
        return false;
    }

    // CService service = CService(strService);
    // if (Params().NetworkID() == CBaseChainParams::MAIN) {
    //     if (service.GetPort() != 1062) {
    //         errorMessage = strprintf("Invalid port %u for masternode %s - only 1062 is supported on mainnet.", service.GetPort(), strService);
    //         LogPrintf("CActiveFrenchnode::Register() - %s\n", errorMessage);
    //         return false;
    //     }
    // } else if (service.GetPort() == 1062) {
    //     errorMessage = strprintf("Invalid port %u for masternode %s - 1062 is only supported on mainnet.", service.GetPort(), strService);
    //     LogPrintf("CActiveFrenchnode::Register() - %s\n", errorMessage);
    //     return false;
    // }

    addrman.Add(CAddress(service), CNetAddr("127.0.0.1"), 2 * 60 * 60);

    return Register(vin, CService(strService), keyCollateralAddress, pubKeyCollateralAddress, keyFrenchnode, pubKeyFrenchnode, errorMessage);
}

bool CActiveFrenchnode::Register(CTxIn vin, CService service, CKey keyCollateralAddress, CPubKey pubKeyCollateralAddress, CKey keyFrenchnode, CPubKey pubKeyFrenchnode, std::string& errorMessage)
{
    CFrenchnodeBroadcast mnb;
    CFrenchnodePing mnp(vin);
    if (!mnp.Sign(keyFrenchnode, pubKeyFrenchnode)) {
        errorMessage = strprintf("Failed to sign ping, vin: %s", vin.ToString());
        LogPrintf("CActiveFrenchnode::Register() -  %s\n", errorMessage);
        return false;
    }
    mnodeman.mapSeenFrenchnodePing.insert(make_pair(mnp.GetHash(), mnp));

    LogPrintf("CActiveFrenchnode::Register() - Adding to Frenchnode list\n    service: %s\n    vin: %s\n", service.ToString(), vin.ToString());
    mnb = CFrenchnodeBroadcast(service, vin, pubKeyCollateralAddress, pubKeyFrenchnode, PROTOCOL_VERSION);
    mnb.lastPing = mnp;
    if (!mnb.Sign(keyCollateralAddress)) {
        errorMessage = strprintf("Failed to sign broadcast, vin: %s", vin.ToString());
        LogPrintf("CActiveFrenchnode::Register() - %s\n", errorMessage);
        return false;
    }
    mnodeman.mapSeenFrenchnodeBroadcast.insert(make_pair(mnb.GetHash(), mnb));
    masternodeSync.AddedFrenchnodeList(mnb.GetHash());

    CFrenchnode* pmn = mnodeman.Find(vin);
    if (pmn == NULL) {
        CFrenchnode mn(mnb);
        mnodeman.Add(mn);
    } else {
        pmn->UpdateFromNewBroadcast(mnb);
    }

    //send to all peers
    LogPrintf("CActiveFrenchnode::Register() - RelayElectionEntry vin = %s\n", vin.ToString());
    mnb.Relay();

    return true;
}

bool CActiveFrenchnode::GetMasterNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey)
{
    return GetMasterNodeVin(vin, pubkey, secretKey, "", "");
}

bool CActiveFrenchnode::GetMasterNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey, std::string strTxHash, std::string strOutputIndex)
{
    // Find possible candidates
    TRY_LOCK(pwalletMain->cs_wallet, fWallet);
    if (!fWallet) return false;

    vector<COutput> possibleCoins = SelectCoinsFrenchnode();
    COutput* selectedOutput;

    // Find the vin
    if (!strTxHash.empty()) {
        // Let's find it
        uint256 txHash(strTxHash);
        int outputIndex;
        try {
            outputIndex = std::stoi(strOutputIndex.c_str());
        } catch (const std::exception& e) {
            LogPrintf("%s: %s on strOutputIndex\n", __func__, e.what());
            return false;
        }

        bool found = false;
        BOOST_FOREACH (COutput& out, possibleCoins) {
            if (out.tx->GetHash() == txHash && out.i == outputIndex) {
                selectedOutput = &out;
                found = true;
                break;
            }
        }
        if (!found) {
            LogPrintf("CActiveFrenchnode::GetMasterNodeVin - Could not locate valid vin\n");
            return false;
        }
    } else {
        // No output specified,  Select the first one
        if (possibleCoins.size() > 0) {
            selectedOutput = &possibleCoins[0];
        } else {
            LogPrintf("CActiveFrenchnode::GetMasterNodeVin - Could not locate specified vin from possible list\n");
            return false;
        }
    }

    // At this point we have a selected output, retrieve the associated info
    return GetVinFromOutput(*selectedOutput, vin, pubkey, secretKey);
}


// Extract Frenchnode vin information from output
bool CActiveFrenchnode::GetVinFromOutput(COutput out, CTxIn& vin, CPubKey& pubkey, CKey& secretKey)
{
    CScript pubScript;

    vin = CTxIn(out.tx->GetHash(), out.i);
    pubScript = out.tx->vout[out.i].scriptPubKey; // the inputs PubKey

    CTxDestination address1;
    ExtractDestination(pubScript, address1);
    CBitcoinAddress address2(address1);

    CKeyID keyID;
    if (!address2.GetKeyID(keyID)) {
        LogPrintf("CActiveFrenchnode::GetMasterNodeVin - Address does not refer to a key\n");
        return false;
    }

    if (!pwalletMain->GetKey(keyID, secretKey)) {
        LogPrintf("CActiveFrenchnode::GetMasterNodeVin - Private key for address is not known\n");
        return false;
    }

    pubkey = secretKey.GetPubKey();
    return true;
}

// get all possible outputs for running Frenchnode
vector<COutput> CActiveFrenchnode::SelectCoinsFrenchnode()
{
    vector<COutput> vCoins;
    vector<COutput> filteredCoins;
    vector<COutPoint> confLockedCoins;

    // Temporary unlock MN coins from masternode.conf
    if (GetBoolArg("-mnconflock", true)) {
        uint256 mnTxHash;
        BOOST_FOREACH (CFrenchnodeConfig::CFrenchnodeEntry mne, masternodeConfig.getEntries()) {
            mnTxHash.SetHex(mne.getTxHash());

            int nIndex;
            if(!mne.castOutputIndex(nIndex))
                continue;

            COutPoint outpoint = COutPoint(mnTxHash, nIndex);
            confLockedCoins.push_back(outpoint);
            pwalletMain->UnlockCoin(outpoint);
        }
    }

    // Retrieve all possible outputs
    pwalletMain->AvailableCoins(vCoins);

    // Lock MN coins from masternode.conf back if they where temporary unlocked
    if (!confLockedCoins.empty()) {
        BOOST_FOREACH (COutPoint outpoint, confLockedCoins)
            pwalletMain->LockCoin(outpoint);
    }

    // Filter
    BOOST_FOREACH (const COutput& out, vCoins) {
        if (out.tx->vout[out.i].nValue == FRENCHNODE_COLLATERAL * COIN) { //exactly
            filteredCoins.push_back(out);
        }
    }
    return filteredCoins;
}

// when starting a Frenchnode, this can enable to run as a hot wallet with no funds
bool CActiveFrenchnode::EnableHotColdMasterNode(CTxIn& newVin, CService& newService)
{
    if (!fMasterNode) return false;

    status = ACTIVE_FRENCHNODE_STARTED;

    //The values below are needed for signing mnping messages going forward
    vin = newVin;
    service = newService;

    LogPrintf("CActiveFrenchnode::EnableHotColdMasterNode() - Enabled! You may shut down the cold daemon.\n");

    return true;
}
