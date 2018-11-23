// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef FRENCHNODE_H
#define FRENCHNODE_H

#include "base58.h"
#include "key.h"
#include "main.h"
#include "net.h"
#include "sync.h"
#include "timedata.h"
#include "util.h"

#define FRENCHNODE_MIN_CONFIRMATIONS 7
#define FRENCHNODE_MIN_MNP_SECONDS (10 * 60)
#define FRENCHNODE_MIN_MNB_SECONDS (5 * 60)
#define FRENCHNODE_PING_SECONDS (5 * 60)
#define FRENCHNODE_EXPIRATION_SECONDS (120 * 60)
#define FRENCHNODE_REMOVAL_SECONDS (130 * 60)
#define FRENCHNODE_CHECK_SECONDS 5

#define FRENCHNODE_COLLATERAL 1000

using namespace std;

class CFrenchnode;
class CFrenchnodeBroadcast;
class CFrenchnodePing;
extern map<int64_t, uint256> mapCacheBlockHashes;

bool GetBlockHash(uint256& hash, int nBlockHeight);


//
// The Frenchnode Ping Class : Contains a different serialize method for sending pings from masternodes throughout the network
//

class CFrenchnodePing
{
public:
    CTxIn vin;
    uint256 blockHash;
    int64_t sigTime; //mnb message times
    std::vector<unsigned char> vchSig;
    //removed stop

    CFrenchnodePing();
    CFrenchnodePing(CTxIn& newVin);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(vin);
        READWRITE(blockHash);
        READWRITE(sigTime);
        READWRITE(vchSig);
    }

    bool CheckAndUpdate(int& nDos, bool fRequireEnabled = true);
    bool Sign(CKey& keyFrenchnode, CPubKey& pubKeyFrenchnode);
    void Relay();

    uint256 GetHash()
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << vin;
        ss << sigTime;
        return ss.GetHash();
    }

    void swap(CFrenchnodePing& first, CFrenchnodePing& second) // nothrow
    {
        // enable ADL (not necessary in our case, but good practice)
        using std::swap;

        // by swapping the members of two classes,
        // the two classes are effectively swapped
        swap(first.vin, second.vin);
        swap(first.blockHash, second.blockHash);
        swap(first.sigTime, second.sigTime);
        swap(first.vchSig, second.vchSig);
    }

    CFrenchnodePing& operator=(CFrenchnodePing from)
    {
        swap(*this, from);
        return *this;
    }
    friend bool operator==(const CFrenchnodePing& a, const CFrenchnodePing& b)
    {
        return a.vin == b.vin && a.blockHash == b.blockHash;
    }
    friend bool operator!=(const CFrenchnodePing& a, const CFrenchnodePing& b)
    {
        return !(a == b);
    }
};

//
// The Frenchnode Class. It contains the input of the FRENCH collateral, signature to prove
// it's the one who own that ip address and code for calculating the payment election.
//
class CFrenchnode
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;
    int64_t lastTimeChecked;

public:
    enum state {
        FRENCHNODE_PRE_ENABLED,
        FRENCHNODE_ENABLED,
        FRENCHNODE_EXPIRED,
        FRENCHNODE_OUTPOINT_SPENT,
        FRENCHNODE_REMOVE,
        FRENCHNODE_WATCHDOG_EXPIRED,
        FRENCHNODE_POSE_BAN,
        FRENCHNODE_VIN_SPENT,
        FRENCHNODE_POS_ERROR
    };

    CTxIn vin;
    CService addr;
    CPubKey pubKeyCollateralAddress;
    CPubKey pubKeyFrenchnode;
    CPubKey pubKeyCollateralAddress1;
    CPubKey pubKeyFrenchnode1;
    std::vector<unsigned char> sig;
    int activeState;
    int64_t sigTime; //mnb message time
    int cacheInputAge;
    int cacheInputAgeBlock;
    bool unitTest;
    bool allowFreeTx;
    int protocolVersion;
    int nActiveState;
    int64_t nLastDsq; //the dsq count from the last dsq broadcast of this node
    int nScanningErrorCount;
    int nLastScanningErrorBlockHeight;
    CFrenchnodePing lastPing;

    int64_t nLastDsee;  // temporary, do not save. Remove after migration to v12
    int64_t nLastDseep; // temporary, do not save. Remove after migration to v12

    CFrenchnode();
    CFrenchnode(const CFrenchnode& other);
    CFrenchnode(const CFrenchnodeBroadcast& mnb);


    void swap(CFrenchnode& first, CFrenchnode& second) // nothrow
    {
        // enable ADL (not necessary in our case, but good practice)
        using std::swap;

        // by swapping the members of two classes,
        // the two classes are effectively swapped
        swap(first.vin, second.vin);
        swap(first.addr, second.addr);
        swap(first.pubKeyCollateralAddress, second.pubKeyCollateralAddress);
        swap(first.pubKeyFrenchnode, second.pubKeyFrenchnode);
        swap(first.sig, second.sig);
        swap(first.activeState, second.activeState);
        swap(first.sigTime, second.sigTime);
        swap(first.lastPing, second.lastPing);
        swap(first.cacheInputAge, second.cacheInputAge);
        swap(first.cacheInputAgeBlock, second.cacheInputAgeBlock);
        swap(first.unitTest, second.unitTest);
        swap(first.allowFreeTx, second.allowFreeTx);
        swap(first.protocolVersion, second.protocolVersion);
        swap(first.nLastDsq, second.nLastDsq);
        swap(first.nScanningErrorCount, second.nScanningErrorCount);
        swap(first.nLastScanningErrorBlockHeight, second.nLastScanningErrorBlockHeight);
    }

    CFrenchnode& operator=(CFrenchnode from)
    {
        swap(*this, from);
        return *this;
    }
    friend bool operator==(const CFrenchnode& a, const CFrenchnode& b)
    {
        return a.vin == b.vin;
    }
    friend bool operator!=(const CFrenchnode& a, const CFrenchnode& b)
    {
        return !(a.vin == b.vin);
    }

    uint256 CalculateScore(int mod = 1, int64_t nBlockHeight = 0);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        LOCK(cs);

        READWRITE(vin);
        READWRITE(addr);
        READWRITE(pubKeyCollateralAddress);
        READWRITE(pubKeyFrenchnode);
        READWRITE(sig);
        READWRITE(sigTime);
        READWRITE(protocolVersion);
        READWRITE(activeState);
        READWRITE(lastPing);
        READWRITE(cacheInputAge);
        READWRITE(cacheInputAgeBlock);
        READWRITE(unitTest);
        READWRITE(allowFreeTx);
        READWRITE(nLastDsq);
        READWRITE(nScanningErrorCount);
        READWRITE(nLastScanningErrorBlockHeight);
    }

    int64_t SecondsSincePayment();

    bool UpdateFromNewBroadcast(CFrenchnodeBroadcast& mnb);

    inline uint64_t SliceHash(uint256& hash, int slice)
    {
        uint64_t n = 0;
        memcpy(&n, &hash + slice * 64, 64);
        return n;
    }

    void Check(bool forceCheck = false);

    bool IsBroadcastedWithin(int seconds)
    {
        return (GetAdjustedTime() - sigTime) < seconds;
    }

    bool IsPingedWithin(int seconds, int64_t now = -1)
    {
        now == -1 ? now = GetAdjustedTime() : now;

        return (lastPing == CFrenchnodePing()) ? false : now - lastPing.sigTime < seconds;
    }

    void Disable()
    {
        sigTime = 0;
        lastPing = CFrenchnodePing();
    }

    bool IsEnabled()
    {
        return activeState == FRENCHNODE_ENABLED;
    }

    int GetFrenchnodeInputAge()
    {
        if (chainActive.Tip() == NULL) return 0;

        if (cacheInputAge == 0) {
            cacheInputAge = GetInputAge(vin);
            cacheInputAgeBlock = chainActive.Tip()->nHeight;
        }

        return cacheInputAge + (chainActive.Tip()->nHeight - cacheInputAgeBlock);
    }

    std::string GetStatus();

    std::string Status()
    {
        std::string strStatus = "ACTIVE";

        if (activeState == CFrenchnode::FRENCHNODE_ENABLED) strStatus = "ENABLED";
        if (activeState == CFrenchnode::FRENCHNODE_EXPIRED) strStatus = "EXPIRED";
        if (activeState == CFrenchnode::FRENCHNODE_VIN_SPENT) strStatus = "VIN_SPENT";
        if (activeState == CFrenchnode::FRENCHNODE_REMOVE) strStatus = "REMOVE";
        if (activeState == CFrenchnode::FRENCHNODE_POS_ERROR) strStatus = "POS_ERROR";

        return strStatus;
    }

    int64_t GetLastPaid();
    bool IsValidNetAddr();
};


//
// The Frenchnode Broadcast Class : Contains a different serialize method for sending masternodes through the network
//

class CFrenchnodeBroadcast : public CFrenchnode
{
public:
    CFrenchnodeBroadcast();
    CFrenchnodeBroadcast(CService newAddr, CTxIn newVin, CPubKey newPubkey, CPubKey newPubkey2, int protocolVersionIn);
    CFrenchnodeBroadcast(const CFrenchnode& mn);

    bool CheckAndUpdate(int& nDoS);
    bool CheckInputsAndAdd(int& nDos);
    bool Sign(CKey& keyCollateralAddress);
    void Relay();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(vin);
        READWRITE(addr);
        READWRITE(pubKeyCollateralAddress);
        READWRITE(pubKeyFrenchnode);
        READWRITE(sig);
        READWRITE(sigTime);
        READWRITE(protocolVersion);
        READWRITE(lastPing);
        READWRITE(nLastDsq);
    }

    uint256 GetHash()
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << sigTime;
        ss << pubKeyCollateralAddress;
        return ss.GetHash();
    }

    /// Create Frenchnode broadcast, needs to be relayed manually after that
    static bool Create(CTxIn vin, CService service, CKey keyCollateralAddressNew, CPubKey pubKeyCollateralAddressNew, CKey keyFrenchnodeNew, CPubKey pubKeyFrenchnodeNew, std::string& strErrorRet, CFrenchnodeBroadcast& mnbRet);
    static bool Create(std::string strService, std::string strKey, std::string strTxHash, std::string strOutputIndex, std::string& strErrorRet, CFrenchnodeBroadcast& mnbRet, bool fOffline = false);
};

#endif
