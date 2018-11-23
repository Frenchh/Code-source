#include "masternodelist.h"
#include "ui_masternodelist.h"

#include "activemasternode.h"
#include "clientmodel.h"
#include "guiutil.h"
#include "init.h"
#include "masternode-sync.h"
#include "masternodeconfig.h"
#include "masternodeman.h"
#include "sync.h"
#include "wallet.h"
#include "walletmodel.h"

#include <QMessageBox>
#include <QTimer>

CCriticalSection cs_masternodes;

FrenchnodeList::FrenchnodeList(QWidget* parent) : QWidget(parent),
                                                  ui(new Ui::FrenchnodeList),
                                                  clientModel(0),
                                                  walletModel(0)
{
    ui->setupUi(this);

    ui->startButton->setEnabled(false);

    int columnAliasWidth = 100;
    int columnAddressWidth = 200;
    int columnProtocolWidth = 60;
    int columnStatusWidth = 80;
    int columnActiveWidth = 130;
    int columnLastSeenWidth = 130;

    ui->tableWidgetMyFrenchnodes->setAlternatingRowColors(true);
    ui->tableWidgetMyFrenchnodes->setColumnWidth(0, columnAliasWidth);
    ui->tableWidgetMyFrenchnodes->setColumnWidth(1, columnAddressWidth);
    ui->tableWidgetMyFrenchnodes->setColumnWidth(2, columnProtocolWidth);
    ui->tableWidgetMyFrenchnodes->setColumnWidth(3, columnStatusWidth);
    ui->tableWidgetMyFrenchnodes->setColumnWidth(4, columnActiveWidth);
    ui->tableWidgetMyFrenchnodes->setColumnWidth(5, columnLastSeenWidth);

    ui->tableWidgetMyFrenchnodes->setContextMenuPolicy(Qt::CustomContextMenu);

    QAction* startAliasAction = new QAction(tr("Start alias"), this);
    contextMenu = new QMenu();
    contextMenu->addAction(startAliasAction);
    connect(ui->tableWidgetMyFrenchnodes, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(showContextMenu(const QPoint&)));
    connect(startAliasAction, SIGNAL(triggered()), this, SLOT(on_startButton_clicked()));

    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(updateMyNodeList()));
    timer->start(1000);

    // Fill MN list
    fFilterUpdated = true;
    nTimeFilterUpdated = GetTime();
}

FrenchnodeList::~FrenchnodeList()
{
    delete ui;
}

void FrenchnodeList::setClientModel(ClientModel* model)
{
    this->clientModel = model;
}

void FrenchnodeList::setWalletModel(WalletModel* model)
{
    this->walletModel = model;
}

void FrenchnodeList::showContextMenu(const QPoint& point)
{
    QTableWidgetItem* item = ui->tableWidgetMyFrenchnodes->itemAt(point);
    if (item) contextMenu->exec(QCursor::pos());
}

void FrenchnodeList::StartAlias(std::string strAlias)
{
    std::string strStatusHtml;
    strStatusHtml += "<center>Alias: " + strAlias;

    BOOST_FOREACH (CFrenchnodeConfig::CFrenchnodeEntry mne, masternodeConfig.getEntries()) {
        if (mne.getAlias() == strAlias) {
            std::string strError;
            CFrenchnodeBroadcast mnb;

            bool fSuccess = CFrenchnodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), strError, mnb);

            if (fSuccess) {
                strStatusHtml += "<br>Successfully started masternode.";
                mnodeman.UpdateFrenchnodeList(mnb);
                mnb.Relay();
            } else {
                strStatusHtml += "<br>Failed to start masternode.<br>Error: " + strError;
            }
            break;
        }
    }
    strStatusHtml += "</center>";

    QMessageBox msg;
    msg.setText(QString::fromStdString(strStatusHtml));
    msg.exec();

    updateMyNodeList(true);
}

void FrenchnodeList::StartAll(std::string strCommand)
{
    int nCountSuccessful = 0;
    int nCountFailed = 0;
    std::string strFailedHtml;

    BOOST_FOREACH (CFrenchnodeConfig::CFrenchnodeEntry mne, masternodeConfig.getEntries()) {
        std::string strError;
        CFrenchnodeBroadcast mnb;

        int nIndex;
        if(!mne.castOutputIndex(nIndex))
            continue;

        CTxIn txin = CTxIn(uint256S(mne.getTxHash()), uint32_t(nIndex));
        CFrenchnode* pmn = mnodeman.Find(txin);

        if (strCommand == "start-missing" && pmn) continue;

        bool fSuccess = CFrenchnodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), strError, mnb);

        if (fSuccess) {
            nCountSuccessful++;
            mnodeman.UpdateFrenchnodeList(mnb);
            mnb.Relay();
        } else {
            nCountFailed++;
            strFailedHtml += "\nFailed to start " + mne.getAlias() + ". Error: " + strError;
        }
    }
    pwalletMain->Lock();

    std::string returnObj;
    returnObj = strprintf("Successfully started %d masternodes, failed to start %d, total %d", nCountSuccessful, nCountFailed, nCountFailed + nCountSuccessful);
    if (nCountFailed > 0) {
        returnObj += strFailedHtml;
    }

    QMessageBox msg;
    msg.setText(QString::fromStdString(returnObj));
    msg.exec();

    updateMyNodeList(true);
}

void FrenchnodeList::updateMyFrenchnodeInfo(QString strAlias, QString strAddr, CFrenchnode* pmn)
{
    LOCK(cs_mnlistupdate);
    bool fOldRowFound = false;
    int nNewRow = 0;

    for (int i = 0; i < ui->tableWidgetMyFrenchnodes->rowCount(); i++) {
        if (ui->tableWidgetMyFrenchnodes->item(i, 0)->text() == strAlias) {
            fOldRowFound = true;
            nNewRow = i;
            break;
        }
    }

    if (nNewRow == 0 && !fOldRowFound) {
        nNewRow = ui->tableWidgetMyFrenchnodes->rowCount();
        ui->tableWidgetMyFrenchnodes->insertRow(nNewRow);
    }

    QTableWidgetItem* aliasItem = new QTableWidgetItem(strAlias);
    QTableWidgetItem* addrItem = new QTableWidgetItem(pmn ? QString::fromStdString(pmn->addr.ToString()) : strAddr);
    QTableWidgetItem* protocolItem = new QTableWidgetItem(QString::number(pmn ? pmn->protocolVersion : -1));
    QTableWidgetItem* statusItem = new QTableWidgetItem(QString::fromStdString(pmn ? pmn->GetStatus() : "MISSING"));
    GUIUtil::DHMSTableWidgetItem* activeSecondsItem = new GUIUtil::DHMSTableWidgetItem(pmn ? (pmn->lastPing.sigTime - pmn->sigTime) : 0);
    QTableWidgetItem* lastSeenItem = new QTableWidgetItem(QString::fromStdString(DateTimeStrFormat("%Y-%m-%d %H:%M", pmn ? pmn->lastPing.sigTime : 0)));
    QTableWidgetItem* pubkeyItem = new QTableWidgetItem(QString::fromStdString(pmn ? CBitcoinAddress(pmn->pubKeyCollateralAddress.GetID()).ToString() : ""));

    ui->tableWidgetMyFrenchnodes->setItem(nNewRow, 0, aliasItem);
    ui->tableWidgetMyFrenchnodes->setItem(nNewRow, 1, addrItem);
    ui->tableWidgetMyFrenchnodes->setItem(nNewRow, 2, protocolItem);
    ui->tableWidgetMyFrenchnodes->setItem(nNewRow, 3, statusItem);
    ui->tableWidgetMyFrenchnodes->setItem(nNewRow, 4, activeSecondsItem);
    ui->tableWidgetMyFrenchnodes->setItem(nNewRow, 5, lastSeenItem);
    ui->tableWidgetMyFrenchnodes->setItem(nNewRow, 6, pubkeyItem);
}

void FrenchnodeList::updateMyNodeList(bool fForce)
{
    static int64_t nTimeMyListUpdated = 0;

    // automatically update my masternode list only once in MY_FRENCHNODELIST_UPDATE_SECONDS seconds,
    // this update still can be triggered manually at any time via button click
    int64_t nSecondsTillUpdate = nTimeMyListUpdated + MY_FRENCHNODELIST_UPDATE_SECONDS - GetTime();
    ui->secondsLabel->setText(QString::number(nSecondsTillUpdate));

    if (nSecondsTillUpdate > 0 && !fForce) return;
    nTimeMyListUpdated = GetTime();

    ui->tableWidgetMyFrenchnodes->setSortingEnabled(false);
    BOOST_FOREACH (CFrenchnodeConfig::CFrenchnodeEntry mne, masternodeConfig.getEntries()) {
        int nIndex;
        if(!mne.castOutputIndex(nIndex))
            continue;

        CTxIn txin = CTxIn(uint256S(mne.getTxHash()), uint32_t(nIndex));
        CFrenchnode* pmn = mnodeman.Find(txin);
        updateMyFrenchnodeInfo(QString::fromStdString(mne.getAlias()), QString::fromStdString(mne.getIp()), pmn);
    }
    ui->tableWidgetMyFrenchnodes->setSortingEnabled(true);

    // reset "timer"
    ui->secondsLabel->setText("0");
}

void FrenchnodeList::on_startButton_clicked()
{
    // Find selected node alias
    QItemSelectionModel* selectionModel = ui->tableWidgetMyFrenchnodes->selectionModel();
    QModelIndexList selected = selectionModel->selectedRows();

    if (selected.count() == 0) return;

    QModelIndex index = selected.at(0);
    int nSelectedRow = index.row();
    std::string strAlias = ui->tableWidgetMyFrenchnodes->item(nSelectedRow, 0)->text().toStdString();

    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm masternode start"),
        tr("Are you sure you want to start masternode %1?").arg(QString::fromStdString(strAlias)),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if (retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if (encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForStakingOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());

        if (!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAlias(strAlias);
        return;
    }

    StartAlias(strAlias);
}

void FrenchnodeList::on_startAllButton_clicked()
{
    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm all masternodes start"),
        tr("Are you sure you want to start ALL masternodes?"),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if (retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if (encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForStakingOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());

        if (!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAll();
        return;
    }

    StartAll();
}

void FrenchnodeList::on_startMissingButton_clicked()
{
    if (!masternodeSync.IsFrenchnodeListSynced()) {
        QMessageBox::critical(this, tr("Command is not available right now"),
            tr("You can't use this command until masternode list is synced"));
        return;
    }

    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this,
        tr("Confirm missing masternodes start"),
        tr("Are you sure you want to start MISSING masternodes?"),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if (retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if (encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForStakingOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());

        if (!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAll("start-missing");
        return;
    }

    StartAll("start-missing");
}

void FrenchnodeList::on_tableWidgetMyFrenchnodes_itemSelectionChanged()
{
    if (ui->tableWidgetMyFrenchnodes->selectedItems().count() > 0) {
        ui->startButton->setEnabled(true);
    }
}

void FrenchnodeList::on_UpdateButton_clicked()
{
    updateMyNodeList(true);
}
