#ifndef FRENCHNODELIST_H
#define FRENCHNODELIST_H

#include "masternode.h"
#include "platformstyle.h"
#include "sync.h"
#include "util.h"

#include <QMenu>
#include <QTimer>
#include <QWidget>

#define MY_FRENCHNODELIST_UPDATE_SECONDS 60
#define FRENCHNODELIST_UPDATE_SECONDS 15
#define FRENCHNODELIST_FILTER_COOLDOWN_SECONDS 3

namespace Ui
{
class FrenchnodeList;
}

class ClientModel;
class WalletModel;

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

/** Frenchnode Manager page widget */
class FrenchnodeList : public QWidget
{
    Q_OBJECT

public:
    explicit FrenchnodeList(QWidget* parent = 0);
    ~FrenchnodeList();

    void setClientModel(ClientModel* clientModel);
    void setWalletModel(WalletModel* walletModel);
    void StartAlias(std::string strAlias);
    void StartAll(std::string strCommand = "start-all");

private:
    QMenu* contextMenu;
    int64_t nTimeFilterUpdated;
    bool fFilterUpdated;

public Q_SLOTS:
    void updateMyFrenchnodeInfo(QString strAlias, QString strAddr, CFrenchnode* pmn);
    void updateMyNodeList(bool fForce = false);

Q_SIGNALS:

private:
    QTimer* timer;
    Ui::FrenchnodeList* ui;
    ClientModel* clientModel;
    WalletModel* walletModel;
    CCriticalSection cs_mnlistupdate;
    QString strCurrentFilter;

private Q_SLOTS:
    void showContextMenu(const QPoint&);
    void on_startButton_clicked();
    void on_startAllButton_clicked();
    void on_startMissingButton_clicked();
    void on_tableWidgetMyFrenchnodes_itemSelectionChanged();
    void on_UpdateButton_clicked();
};
#endif // FRENCHNODELIST_H
