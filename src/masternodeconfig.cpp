// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// clang-format off
#include "net.h"
#include "masternodeconfig.h"
#include "util.h"
#include "ui_interface.h"
#include <base58.h>
// clang-format on

CFrenchnodeConfig masternodeConfig;

void CFrenchnodeConfig::add(std::string alias, std::string ip, std::string privKey, std::string txHash, std::string outputIndex)
{
    CFrenchnodeEntry cme(alias, ip, privKey, txHash, outputIndex);
    entries.push_back(cme);
}

bool CFrenchnodeConfig::read(std::string& strErr)
{
    int linenumber = 1;
    boost::filesystem::path pathFrenchnodeConfigFile = GetFrenchnodeConfigFile();
    boost::filesystem::ifstream streamConfig(pathFrenchnodeConfigFile);

    if (!streamConfig.good()) {
        FILE* configFile = fopen(pathFrenchnodeConfigFile.string().c_str(), "a");
        if (configFile != NULL) {
            std::string strHeader = "# Frenchnode config file\n"
                                    "# Format: alias IP:port masternodeprivkey collateral_output_txid collateral_output_index\n"
                                    "# Example: mn1 127.0.0.2:1062 93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg 2bcd3c84c84f87eaa86e4e56834c92927a07f9e18718810b92e0d0324456a67c 0\n";
            fwrite(strHeader.c_str(), std::strlen(strHeader.c_str()), 1, configFile);
            fclose(configFile);
        }
        return true; // Nothing to read, so just return
    }

    for (std::string line; std::getline(streamConfig, line); linenumber++) {
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string comment, alias, ip, privKey, txHash, outputIndex;

        if (iss >> comment) {
            if (comment.at(0) == '#') continue;
            iss.str(line);
            iss.clear();
        }

        if (!(iss >> alias >> ip >> privKey >> txHash >> outputIndex)) {
            iss.str(line);
            iss.clear();
            if (!(iss >> alias >> ip >> privKey >> txHash >> outputIndex)) {
                strErr = _("Could not parse masternode.conf") + "\n" +
                         strprintf(_("Line: %d"), linenumber) + "\n\"" + line + "\"";
                streamConfig.close();
                return false;
            }
        }

        // if (Params().NetworkID() == CBaseChainParams::MAIN) {
        //     if (CService(ip).GetPort() != 1062) {
        //         strErr = _("Invalid port detected in masternode.conf") + "\n" +
        //                  strprintf(_("Line: %d"), linenumber) + "\n\"" + line + "\"" + "\n" +
        //                  _("(must be 1062 for mainnet)");
        //         streamConfig.close();
        //         return false;
        //     }
        // } else if (CService(ip).GetPort() == 1062) {
        //     strErr = _("Invalid port detected in masternode.conf") + "\n" +
        //              strprintf(_("Line: %d"), linenumber) + "\n\"" + line + "\"" + "\n" +
        //              _("(1062 could be used only on mainnet)");
        //     streamConfig.close();
        //     return false;
        // }

        add(alias, ip, privKey, txHash, outputIndex);
    }

    streamConfig.close();
    return true;
}

bool CFrenchnodeConfig::CFrenchnodeEntry::castOutputIndex(int &n)
{
    try {
        n = std::stoi(outputIndex);
    } catch (const std::exception e) {
        LogPrintf("%s: %s on getOutputIndex\n", __func__, e.what());
        return false;
    }

    return true;
}
