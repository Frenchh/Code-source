// Copyright (c) 2014-2016 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ACTIVEFRENCHNODE_H
#define ACTIVEFRENCHNODE_H

#include "init.h"
#include "key.h"
#include "masternode.h"
#include "net.h"
#include "sync.h"
#include "wallet.h"

#define ACTIVE_FRENCHNODE_INITIAL 0 // initial state
#define ACTIVE_FRENCHNODE_SYNC_IN_PROCESS 1
#define ACTIVE_FRENCHNODE_INPUT_TOO_NEW 2
#define ACTIVE_FRENCHNODE_NOT_CAPABLE 3
#define ACTIVE_FRENCHNODE_STARTED 4

// Responsible for activating the Frenchnode and pinging the network
class CActiveFrenchnode
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    /// Ping Frenchnode
    bool SendFrenchnodePing(std::string& errorMessage);

    /// Register any Frenchnode
    bool Register(CTxIn vin, CService service, CKey key, CPubKey pubKey, CKey keyFrenchnode, CPubKey pubKeyFrenchnode, std::string& errorMessage);

    /// Get FRENCH collateral input that can be used for the Frenchnode
    bool GetMasterNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey, std::string strTxHash, std::string strOutputIndex);
    bool GetVinFromOutput(COutput out, CTxIn& vin, CPubKey& pubkey, CKey& secretKey);

public:
    // Initialized by init.cpp
    // Keys for the main Frenchnode
    CPubKey pubKeyFrenchnode;

    // Initialized while registering Frenchnode
    CTxIn vin;
    CService service;

    int status;
    std::string notCapableReason;

    CActiveFrenchnode()
    {
        status = ACTIVE_FRENCHNODE_INITIAL;
    }

    /// Manage status of main Frenchnode
    void ManageStatus();
    std::string GetStatus();

    /// Register remote Frenchnode
    bool Register(std::string strService, std::string strKey, std::string strTxHash, std::string strOutputIndex, std::string& errorMessage);

    /// Get FRENCH collateral input that can be used for the Frenchnode
    bool GetMasterNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey);
    vector<COutput> SelectCoinsFrenchnode();

    /// Enable cold wallet mode (run a Frenchnode with no funds)
    bool EnableHotColdMasterNode(CTxIn& vin, CService& addr);
};

extern CActiveFrenchnode activeFrenchnode;

#endif
