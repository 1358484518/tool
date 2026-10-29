#include "FtpWidget.h"
#include "FtpClientWorker.h"
#include "FtpWorker.h"
#include "TftpClientWorker.h"
#include "TftpServerWorker.h"

#include <QCheckBox>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
#ifdef Q_OS_UNIX
const int kDefaultPort = 2121;  // 类 Unix 上 21 端口通常要 root
#else
const int kDefaultPort = 21;
#endif
const int kDefaultTftpPort = 6969;  // 69 需要管理员。和 FTP 的 21 错开
}

FtpWidget::FtpWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
    initWorker();
    initClient();
    initTftpServer();
    initTftpClient();
    loadSettings();
    retranslateUi();
}

FtpWidget::~FtpWidget()
{
    saveSettings();
    shutdownWorker(m_clientThread, m_client);
    m_client = nullptr;
    shutdownWorker(m_tftpClientThread, m_tftpClient);
    m_tftpClient = nullptr;
    shutdownWorker(m_tftpServerThread, m_tftpServer);
    m_tftpServer = nullptr;
    shutdownWorker(m_thread, m_worker);
    m_worker = nullptr;
}

void FtpWidget::setupUi()
{
    auto *page = new QGridLayout(this);
    page->setContentsMargins(8, 8, 8, 8);
    page->setSpacing(8);

    m_serverGroup = new QGroupBox(this);
    auto *serverLayout = new QVBoxLayout(m_serverGroup);
    auto *serverGrid = new QGridLayout;
    serverGrid->setColumnStretch(1, 1);
    int row = 0;
    m_portLabel = new QLabel(m_serverGroup);
    m_portEdit = new QLineEdit(m_serverGroup);
    m_portEdit->setValidator(new QIntValidator(1, 65535, m_portEdit));
    serverGrid->addWidget(m_portLabel, row, 0);
    serverGrid->addWidget(m_portEdit, row, 1, 1, 2);
    ++row;
    m_userLabel = new QLabel(m_serverGroup);
    m_userEdit = new QLineEdit(m_serverGroup);
    serverGrid->addWidget(m_userLabel, row, 0);
    serverGrid->addWidget(m_userEdit, row, 1, 1, 2);
    ++row;
    m_passwordLabel = new QLabel(m_serverGroup);
    m_passwordEdit = new QLineEdit(m_serverGroup);
    serverGrid->addWidget(m_passwordLabel, row, 0);
    serverGrid->addWidget(m_passwordEdit, row, 1, 1, 2);
    ++row;
    m_rootLabel = new QLabel(m_serverGroup);
    m_rootEdit = new QLineEdit(m_serverGroup);
    m_rootEdit->setReadOnly(true);
    m_browseBtn = new QToolButton(m_serverGroup);
    m_browseBtn->setText(QStringLiteral("..."));
    serverGrid->addWidget(m_rootLabel, row, 0);
    serverGrid->addWidget(m_rootEdit, row, 1);
    serverGrid->addWidget(m_browseBtn, row, 2);
    ++row;
    m_anonymousCheck = new QCheckBox(m_serverGroup);
    serverGrid->addWidget(m_anonymousCheck, row, 0, 1, 3);
    ++row;
    m_readOnlyCheck = new QCheckBox(m_serverGroup);
    serverGrid->addWidget(m_readOnlyCheck, row, 0, 1, 3);
    ++row;
    m_oneIpCheck = new QCheckBox(m_serverGroup);
    serverGrid->addWidget(m_oneIpCheck, row, 0, 1, 3);
    ++row;
    m_restartBtn = new QPushButton(m_serverGroup);
    serverGrid->addWidget(m_restartBtn, row, 0, 1, 3);
    serverLayout->addLayout(serverGrid);
    m_statusLabel = new QLabel(m_serverGroup);
    m_statusLabel->setWordWrap(true);
    serverLayout->addWidget(m_statusLabel);
    m_log = new QPlainTextEdit(m_serverGroup);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(1000);
    serverLayout->addWidget(m_log, 1);

    m_clientGroup = new QGroupBox(this);
    auto *clientLayout = new QVBoxLayout(m_clientGroup);
    auto *clientForm = new QGridLayout;
    clientForm->setColumnStretch(1, 1);
    m_clientHostLabel = new QLabel(m_clientGroup);
    m_clientHostEdit = new QLineEdit(m_clientGroup);
    m_clientPortLabel = new QLabel(m_clientGroup);
    m_clientPortEdit = new QLineEdit(m_clientGroup);
    m_clientPortEdit->setValidator(new QIntValidator(1, 65535, m_clientPortEdit));
    m_clientUserLabel = new QLabel(m_clientGroup);
    m_clientUserEdit = new QLineEdit(m_clientGroup);
    m_clientPasswordLabel = new QLabel(m_clientGroup);
    m_clientPasswordEdit = new QLineEdit(m_clientGroup);
    clientForm->addWidget(m_clientHostLabel, 0, 0);
    clientForm->addWidget(m_clientHostEdit, 0, 1);
    clientForm->addWidget(m_clientPortLabel, 0, 2);
    clientForm->addWidget(m_clientPortEdit, 0, 3);
    clientForm->addWidget(m_clientUserLabel, 1, 0);
    clientForm->addWidget(m_clientUserEdit, 1, 1);
    clientForm->addWidget(m_clientPasswordLabel, 1, 2);
    clientForm->addWidget(m_clientPasswordEdit, 1, 3);
    m_connectBtn = new QPushButton(m_clientGroup);
    clientForm->addWidget(m_connectBtn, 0, 4, 2, 1);
    clientLayout->addLayout(clientForm);

    m_pathLabel = new QLabel(m_clientGroup);
    m_pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    clientLayout->addWidget(m_pathLabel);

    m_fileList = new QTreeWidget(m_clientGroup);
    m_fileList->setRootIsDecorated(false);
    m_fileList->setAlternatingRowColors(true);
    m_fileList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_fileList->header()->setStretchLastSection(true);
    clientLayout->addWidget(m_fileList, 1);

    auto *clientButtons = new QHBoxLayout;
    m_upBtn = new QPushButton(m_clientGroup);
    m_refreshBtn = new QPushButton(m_clientGroup);
    m_downloadBtn = new QPushButton(m_clientGroup);
    m_uploadBtn = new QPushButton(m_clientGroup);
    m_cancelBtn = new QPushButton(m_clientGroup);
    clientButtons->addWidget(m_upBtn);
    clientButtons->addWidget(m_refreshBtn);
    clientButtons->addWidget(m_downloadBtn);
    clientButtons->addWidget(m_uploadBtn);
    clientButtons->addWidget(m_cancelBtn);
    clientButtons->addStretch();
    clientLayout->addLayout(clientButtons);

    auto *saveRow = new QHBoxLayout;
    m_saveLabel = new QLabel(m_clientGroup);
    m_saveDirEdit = new QLineEdit(m_clientGroup);
    m_saveBrowseBtn = new QPushButton(m_clientGroup);
    saveRow->addWidget(m_saveLabel);
    saveRow->addWidget(m_saveDirEdit, 1);
    saveRow->addWidget(m_saveBrowseBtn);
    clientLayout->addLayout(saveRow);

    m_progress = new QProgressBar(m_clientGroup);
    m_progress->setRange(0, 1);
    m_progress->setValue(0);
    m_progress->setTextVisible(true);
    clientLayout->addWidget(m_progress);
    m_clientStatus = new QLabel(m_clientGroup);
    m_clientStatus->setWordWrap(true);
    clientLayout->addWidget(m_clientStatus);

    m_tftpServerGroup = new QGroupBox(this);
    auto *tftpServerLayout = new QVBoxLayout(m_tftpServerGroup);
    auto *tftpServerForm = new QGridLayout;
    tftpServerForm->setColumnStretch(1, 1);
    m_tftpPortLabel = new QLabel(m_tftpServerGroup);
    m_tftpPortEdit = new QLineEdit(m_tftpServerGroup);
    m_tftpPortEdit->setValidator(new QIntValidator(1, 65535, m_tftpPortEdit));
    m_tftpRootLabel = new QLabel(m_tftpServerGroup);
    m_tftpRootEdit = new QLineEdit(m_tftpServerGroup);
    m_tftpRootEdit->setReadOnly(true);
    m_tftpBrowseBtn = new QToolButton(m_tftpServerGroup);
    m_tftpBrowseBtn->setText(QStringLiteral("..."));
    m_tftpToggleBtn = new QPushButton(m_tftpServerGroup);
    tftpServerForm->addWidget(m_tftpPortLabel, 0, 0);
    tftpServerForm->addWidget(m_tftpPortEdit, 0, 1);
    tftpServerForm->addWidget(m_tftpToggleBtn, 0, 2);
    tftpServerForm->addWidget(m_tftpRootLabel, 1, 0);
    tftpServerForm->addWidget(m_tftpRootEdit, 1, 1);
    tftpServerForm->addWidget(m_tftpBrowseBtn, 1, 2);
    tftpServerLayout->addLayout(tftpServerForm);
    m_tftpStatus = new QLabel(m_tftpServerGroup);
    m_tftpStatus->setWordWrap(true);
    tftpServerLayout->addWidget(m_tftpStatus);
    m_tftpServerLog = new QPlainTextEdit(m_tftpServerGroup);
    m_tftpServerLog->setReadOnly(true);
    m_tftpServerLog->setMaximumBlockCount(1000);
    tftpServerLayout->addWidget(m_tftpServerLog, 1);

    m_tftpClientGroup = new QGroupBox(this);
    auto *tftpClientLayout = new QVBoxLayout(m_tftpClientGroup);
    auto *tftpClientForm = new QGridLayout;
    tftpClientForm->setColumnStretch(1, 1);
    m_tftpHostLabel = new QLabel(m_tftpClientGroup);
    m_tftpHostEdit = new QLineEdit(m_tftpClientGroup);
    m_tftpClientPortLabel = new QLabel(m_tftpClientGroup);
    m_tftpClientPortEdit = new QLineEdit(m_tftpClientGroup);
    m_tftpClientPortEdit->setValidator(new QIntValidator(1, 65535, m_tftpClientPortEdit));
    m_tftpRemoteLabel = new QLabel(m_tftpClientGroup);
    m_tftpRemoteEdit = new QLineEdit(m_tftpClientGroup);
    m_tftpLocalLabel = new QLabel(m_tftpClientGroup);
    m_tftpLocalEdit = new QLineEdit(m_tftpClientGroup);
    m_tftpLocalBrowseBtn = new QPushButton(m_tftpClientGroup);
    tftpClientForm->addWidget(m_tftpHostLabel, 0, 0);
    tftpClientForm->addWidget(m_tftpHostEdit, 0, 1);
    tftpClientForm->addWidget(m_tftpClientPortLabel, 0, 2);
    tftpClientForm->addWidget(m_tftpClientPortEdit, 0, 3);
    tftpClientForm->addWidget(m_tftpRemoteLabel, 1, 0);
    tftpClientForm->addWidget(m_tftpRemoteEdit, 1, 1, 1, 3);
    tftpClientForm->addWidget(m_tftpLocalLabel, 2, 0);
    tftpClientForm->addWidget(m_tftpLocalEdit, 2, 1, 1, 2);
    tftpClientForm->addWidget(m_tftpLocalBrowseBtn, 2, 3);
    tftpClientLayout->addLayout(tftpClientForm);
    auto *tftpButtons = new QHBoxLayout;
    m_tftpDownloadBtn = new QPushButton(m_tftpClientGroup);
    m_tftpUploadBtn = new QPushButton(m_tftpClientGroup);
    m_tftpCancelBtn = new QPushButton(m_tftpClientGroup);
    tftpButtons->addWidget(m_tftpDownloadBtn);
    tftpButtons->addWidget(m_tftpUploadBtn);
    tftpButtons->addWidget(m_tftpCancelBtn);
    tftpButtons->addStretch();
    tftpClientLayout->addLayout(tftpButtons);
    m_tftpProgress = new QProgressBar(m_tftpClientGroup);
    m_tftpProgress->setRange(0, 1);
    m_tftpProgress->setValue(0);
    m_tftpProgress->setTextVisible(true);
    tftpClientLayout->addWidget(m_tftpProgress);
    m_tftpClientStatus = new QLabel(m_tftpClientGroup);
    m_tftpClientStatus->setWordWrap(true);
    tftpClientLayout->addWidget(m_tftpClientStatus);
    m_tftpClientLog = new QPlainTextEdit(m_tftpClientGroup);
    m_tftpClientLog->setReadOnly(true);
    m_tftpClientLog->setMaximumBlockCount(1000);
    tftpClientLayout->addWidget(m_tftpClientLog, 1);

    page->addWidget(m_serverGroup, 0, 0);
    page->addWidget(m_clientGroup, 0, 1);
    page->addWidget(m_tftpServerGroup, 1, 0);
    page->addWidget(m_tftpClientGroup, 1, 1);
    page->setRowStretch(0, 1);
    page->setRowStretch(1, 1);
    page->setColumnStretch(0, 1);
    page->setColumnStretch(1, 1);

    connect(m_browseBtn, &QToolButton::clicked, this, &FtpWidget::onBrowseRoot);
    connect(m_restartBtn, &QPushButton::clicked, this, &FtpWidget::onRestart);
    connect(m_anonymousCheck, &QCheckBox::toggled, this, &FtpWidget::onAnonymousToggled);
    connect(m_portEdit, &QLineEdit::editingFinished, this, &FtpWidget::saveSettings);
    connect(m_userEdit, &QLineEdit::editingFinished, this, &FtpWidget::saveSettings);
    connect(m_passwordEdit, &QLineEdit::editingFinished, this, &FtpWidget::saveSettings);
    connect(m_readOnlyCheck, &QCheckBox::toggled, this, [this](bool) { saveSettings(); });
    connect(m_oneIpCheck, &QCheckBox::toggled, this, [this](bool) { saveSettings(); });

    connect(m_connectBtn, &QPushButton::clicked, this, &FtpWidget::onClientConnect);
    connect(m_upBtn, &QPushButton::clicked, this, &FtpWidget::onClientUp);
    connect(m_refreshBtn, &QPushButton::clicked, this, &FtpWidget::onClientRefresh);
    connect(m_downloadBtn, &QPushButton::clicked, this, &FtpWidget::onClientDownload);
    connect(m_uploadBtn, &QPushButton::clicked, this, &FtpWidget::onClientUpload);
    connect(m_cancelBtn, &QPushButton::clicked, this, &FtpWidget::onClientCancel);
    connect(m_saveBrowseBtn, &QPushButton::clicked, this, &FtpWidget::onClientBrowseSave);
    connect(m_fileList, &QTreeWidget::itemActivated, this, &FtpWidget::onClientItemActivated);
    connect(m_fileList, &QTreeWidget::itemSelectionChanged, this, &FtpWidget::onClientSelectionChanged);
    connect(m_clientHostEdit, &QLineEdit::editingFinished, this, &FtpWidget::saveSettings);
    connect(m_clientPortEdit, &QLineEdit::editingFinished, this, &FtpWidget::saveSettings);
    connect(m_clientUserEdit, &QLineEdit::editingFinished, this, &FtpWidget::saveSettings);
    connect(m_clientPasswordEdit, &QLineEdit::editingFinished, this, &FtpWidget::saveSettings);
    connect(m_saveDirEdit, &QLineEdit::editingFinished, this, &FtpWidget::saveSettings);

    connect(m_tftpToggleBtn, &QPushButton::clicked, this, &FtpWidget::onTftpToggle);
    connect(m_tftpBrowseBtn, &QToolButton::clicked, this, &FtpWidget::onTftpBrowseRoot);
    connect(m_tftpDownloadBtn, &QPushButton::clicked, this, &FtpWidget::onTftpDownload);
    connect(m_tftpUploadBtn, &QPushButton::clicked, this, &FtpWidget::onTftpUpload);
    connect(m_tftpLocalBrowseBtn, &QPushButton::clicked, this, &FtpWidget::onTftpBrowseLocal);
    connect(m_tftpCancelBtn, &QPushButton::clicked, this, &FtpWidget::onTftpCancel);
    connect(m_tftpPortEdit, &QLineEdit::editingFinished, this, &FtpWidget::saveSettings);
    connect(m_tftpHostEdit, &QLineEdit::editingFinished, this, &FtpWidget::saveSettings);
    connect(m_tftpClientPortEdit, &QLineEdit::editingFinished, this, &FtpWidget::saveSettings);
    connect(m_tftpRemoteEdit, &QLineEdit::editingFinished, this, &FtpWidget::saveSettings);
    connect(m_tftpLocalEdit, &QLineEdit::editingFinished, this, &FtpWidget::saveSettings);
}

void FtpWidget::retranslateUi()
{
    m_serverGroup->setTitle(tr("FTP Server"));
    m_portLabel->setText(tr("Port:"));
    m_userLabel->setText(tr("User:"));
    m_passwordLabel->setText(tr("Password:"));
    m_rootLabel->setText(tr("Root path:"));
    m_anonymousCheck->setText(tr("Allow anonymous login"));
    m_readOnlyCheck->setText(tr("Read-only"));
    m_oneIpCheck->setText(tr("Only one IP allowed"));
    if (m_statusLabel->text().isEmpty())
        m_statusLabel->setText(tr("Not listening"));

    m_clientGroup->setTitle(tr("FTP Client"));
    m_clientHostLabel->setText(tr("Address:"));
    m_clientPortLabel->setText(tr("Port:"));
    m_clientUserLabel->setText(tr("User:"));
    m_clientPasswordLabel->setText(tr("Password:"));
    m_saveLabel->setText(tr("Save to:"));
    m_saveBrowseBtn->setText(tr("Browse"));
    m_upBtn->setText(tr("Up"));
    m_fileList->setHeaderLabels(QStringList() << tr("Name") << tr("Size") << tr("Owner")
                                              << tr("Group") << tr("Time"));
    if (m_clientStatus->text().isEmpty())
        m_clientStatus->setText(tr("Disconnected."));

    m_tftpServerGroup->setTitle(tr("TFTP Server"));
    m_tftpPortLabel->setText(tr("Port:"));
    m_tftpRootLabel->setText(tr("Root path:"));
    m_tftpClientGroup->setTitle(tr("TFTP Client"));
    m_tftpHostLabel->setText(tr("Address:"));
    m_tftpClientPortLabel->setText(tr("Port:"));
    m_tftpRemoteLabel->setText(tr("Remote file:"));
    m_tftpLocalLabel->setText(tr("Local file:"));
    m_tftpLocalBrowseBtn->setText(tr("Browse"));
    if (m_tftpStatus->text().isEmpty())
        m_tftpStatus->setText(tr("Not listening"));

    updateClientButtons();
    updateFtpButtons();
    updateTftpButtons();
}

void FtpWidget::loadSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("Ftp"));
    const QString port = settings.value(QStringLiteral("port"), kDefaultPort).toString();
    const QString root = settings.value(QStringLiteral("root")).toString();
    const QString user = settings.value(QStringLiteral("user"), QStringLiteral("admin")).toString();
    const QString password = settings.value(QStringLiteral("password"), QStringLiteral("qt")).toString();
    const bool anonymous = settings.value(QStringLiteral("anonymous"), false).toBool();
    const bool readOnly = settings.value(QStringLiteral("readOnly"), false).toBool();
    const bool oneIp = settings.value(QStringLiteral("oneIp"), true).toBool();
    const QString clientHost = settings.value(QStringLiteral("clientHost"), QStringLiteral("127.0.0.1")).toString();
    const QString clientPort = settings.value(QStringLiteral("clientPort"), port).toString();
    const QString clientUser = settings.value(QStringLiteral("clientUser"), user).toString();
    const QString clientPassword = settings.value(QStringLiteral("clientPassword"), password).toString();
    const QString saveDir = settings.value(QStringLiteral("saveDir"), QDir::homePath()).toString();
    settings.endGroup();

    settings.beginGroup(QStringLiteral("Tftp"));
    const QString tftpPort = settings.value(QStringLiteral("port"), kDefaultTftpPort).toString();
    const QString tftpRoot = settings.value(QStringLiteral("root"), QDir::homePath()).toString();
    const QString tftpHost = settings.value(QStringLiteral("host"), QStringLiteral("127.0.0.1")).toString();
    const QString tftpClientPort = settings.value(QStringLiteral("clientPort"), tftpPort).toString();
    const QString tftpRemote = settings.value(QStringLiteral("remote")).toString();
    const QString tftpLocal = settings.value(QStringLiteral("local")).toString();
    settings.endGroup();

    // 勾选会立刻保存。先读完再填，避免默认值把还没读到的项写回去。
    m_anonymousCheck->blockSignals(true);
    m_readOnlyCheck->blockSignals(true);
    m_oneIpCheck->blockSignals(true);
    m_portEdit->setText(port);
    m_rootEdit->setText(root.isEmpty() ? QDir::rootPath() : root);
    m_userEdit->setText(user);
    m_passwordEdit->setText(password);
    m_anonymousCheck->setChecked(anonymous);
    m_readOnlyCheck->setChecked(readOnly);
    m_oneIpCheck->setChecked(oneIp);
    m_userEdit->setEnabled(!anonymous);
    m_passwordEdit->setEnabled(!anonymous);
    m_clientHostEdit->setText(clientHost);
    m_clientPortEdit->setText(clientPort);
    m_clientUserEdit->setText(clientUser);
    m_clientPasswordEdit->setText(clientPassword);
    m_saveDirEdit->setText(saveDir);
    m_tftpPortEdit->setText(tftpPort);
    m_tftpRootEdit->setText(tftpRoot.isEmpty() ? QDir::homePath() : tftpRoot);
    m_tftpHostEdit->setText(tftpHost);
    m_tftpClientPortEdit->setText(tftpClientPort);
    m_tftpRemoteEdit->setText(tftpRemote);
    m_tftpLocalEdit->setText(tftpLocal);
    m_anonymousCheck->blockSignals(false);
    m_readOnlyCheck->blockSignals(false);
    m_oneIpCheck->blockSignals(false);
}

void FtpWidget::saveSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("Ftp"));
    settings.setValue(QStringLiteral("port"), m_portEdit->text().trimmed());
    settings.setValue(QStringLiteral("root"), m_rootEdit->text().trimmed());
    settings.setValue(QStringLiteral("user"), m_userEdit->text());
    settings.setValue(QStringLiteral("password"), m_passwordEdit->text());
    settings.setValue(QStringLiteral("anonymous"), m_anonymousCheck->isChecked());
    settings.setValue(QStringLiteral("readOnly"), m_readOnlyCheck->isChecked());
    settings.setValue(QStringLiteral("oneIp"), m_oneIpCheck->isChecked());
    settings.setValue(QStringLiteral("clientHost"), m_clientHostEdit->text().trimmed());
    settings.setValue(QStringLiteral("clientPort"), m_clientPortEdit->text().trimmed());
    settings.setValue(QStringLiteral("clientUser"), m_clientUserEdit->text());
    settings.setValue(QStringLiteral("clientPassword"), m_clientPasswordEdit->text());
    settings.setValue(QStringLiteral("saveDir"), m_saveDirEdit->text().trimmed());
    settings.endGroup();

    settings.beginGroup(QStringLiteral("Tftp"));
    settings.setValue(QStringLiteral("port"), m_tftpPortEdit->text().trimmed());
    settings.setValue(QStringLiteral("root"), m_tftpRootEdit->text().trimmed());
    settings.setValue(QStringLiteral("host"), m_tftpHostEdit->text().trimmed());
    settings.setValue(QStringLiteral("clientPort"), m_tftpClientPortEdit->text().trimmed());
    settings.setValue(QStringLiteral("remote"), m_tftpRemoteEdit->text().trimmed());
    settings.setValue(QStringLiteral("local"), m_tftpLocalEdit->text().trimmed());
    settings.endGroup();
}

void FtpWidget::initWorker()
{
    m_worker = new FtpWorker;
    m_worker->moveToThread(&m_thread);
    connect(this, &FtpWidget::startRequested, m_worker, &FtpWorker::startServer);
    connect(this, &FtpWidget::stopRequested, m_worker, &FtpWorker::stopServer);
    connect(m_worker, &FtpWorker::serverState, this, &FtpWidget::onServerState);
    connect(m_worker, &FtpWorker::logLine, this, &FtpWidget::onLogLine);
    connect(m_worker, &FtpWorker::peerConnected, this, &FtpWidget::onPeerConnected);
    m_thread.start();
}

void FtpWidget::initClient()
{
    m_client = new FtpClientWorker;
    m_client->moveToThread(&m_clientThread);
    connect(this, &FtpWidget::clientConnectRequested, m_client, &FtpClientWorker::connectToHost);
    connect(this, &FtpWidget::clientDisconnectRequested, m_client, &FtpClientWorker::disconnectFromHost);
    connect(this, &FtpWidget::clientRefreshRequested, m_client, &FtpClientWorker::refreshList);
    connect(this, &FtpWidget::clientEnterRequested, m_client, &FtpClientWorker::enterDirectory);
    connect(this, &FtpWidget::clientUpRequested, m_client, &FtpClientWorker::cdToParent);
    connect(this, &FtpWidget::clientDownloadRequested, m_client, &FtpClientWorker::download);
    connect(this, &FtpWidget::clientUploadRequested, m_client, &FtpClientWorker::upload);
    connect(this, &FtpWidget::clientCancelRequested, m_client, &FtpClientWorker::cancelTransfer);
    connect(m_client, &FtpClientWorker::statusMessage, this, &FtpWidget::onClientStatus);
    connect(m_client, &FtpClientWorker::connectionClosed, this, &FtpWidget::onClientClosed);
    connect(m_client, &FtpClientWorker::loggedIn, this, &FtpWidget::onClientLoggedIn);
    connect(m_client, &FtpClientWorker::listingStarted, this, &FtpWidget::onListingStarted);
    connect(m_client, &FtpClientWorker::listItem, this, &FtpWidget::onListItem);
    connect(m_client, &FtpClientWorker::listFinished, this, &FtpWidget::onListFinished);
    connect(m_client, &FtpClientWorker::directoryFailed, this, &FtpWidget::onClientDirectoryFailed);
    connect(m_client, &FtpClientWorker::transferActive, this, &FtpWidget::onTransferActive);
    connect(m_client, &FtpClientWorker::transferProgress, this, &FtpWidget::onTransferProgress);
    m_clientThread.start();
}

void FtpWidget::initTftpServer()
{
    m_tftpServer = new TftpServerWorker;
    m_tftpServer->moveToThread(&m_tftpServerThread);
    connect(this, &FtpWidget::tftpStartRequested, m_tftpServer, &TftpServerWorker::startServer);
    connect(this, &FtpWidget::tftpStopRequested, m_tftpServer, &TftpServerWorker::stopServer);
    connect(m_tftpServer, &TftpServerWorker::logLine, this, &FtpWidget::onTftpServerLog);
    connect(m_tftpServer, &TftpServerWorker::runningChanged, this, &FtpWidget::onTftpRunning);
    m_tftpServerThread.start();
}

void FtpWidget::initTftpClient()
{
    m_tftpClient = new TftpClientWorker;
    m_tftpClient->moveToThread(&m_tftpClientThread);
    connect(this, &FtpWidget::tftpTransferRequested, m_tftpClient, &TftpClientWorker::startTransfer);
    connect(this, &FtpWidget::tftpCancelRequested, m_tftpClient, &TftpClientWorker::cancelTransfer);
    connect(m_tftpClient, &TftpClientWorker::logLine, this, &FtpWidget::onTftpClientLog);
    connect(m_tftpClient, &TftpClientWorker::progress, this, &FtpWidget::onTftpProgress);
    connect(m_tftpClient, &TftpClientWorker::transferFinished, this, &FtpWidget::onTftpFinished);
    connect(m_tftpClient, &TftpClientWorker::transferActive, this, &FtpWidget::onTftpActive);
    m_tftpClientThread.start();
}

void FtpWidget::shutdownWorker(QThread &thread, QObject *worker)
{
    if (!worker)
        return;
    worker->disconnect();
    disconnect(this, nullptr, worker, nullptr);
    // 套接字留在工作线程里删。先断开信号，避免关闭过程再刷界面。
    if (thread.isRunning() && worker->thread() == &thread) {
        QMetaObject::invokeMethod(worker, [worker]() { delete worker; }, Qt::BlockingQueuedConnection);
        thread.quit();
        thread.wait(5000);
    } else {
        delete worker;
    }
}

void FtpWidget::onRestart()
{
    if (m_ftpListening) {
        emit stopRequested();
        return;
    }
    bool ok = false;
    const int port = m_portEdit->text().trimmed().toInt(&ok);
    if (!ok || port < 1 || port > 65535) {
        m_statusLabel->setText(tr("Not listening"));
        return;
    }
    const QString root = m_rootEdit->text().trimmed();
    if (root.isEmpty() || !QDir(root).exists()) {
        m_statusLabel->setText(tr("Root directory does not exist"));
        return;
    }
    saveSettings();
    QString user;
    QString password;
    if (!m_anonymousCheck->isChecked()) {
        user = m_userEdit->text();
        password = m_passwordEdit->text();
    }
    emit startRequested(static_cast<quint16>(port), root, user, password,
                        m_readOnlyCheck->isChecked(), m_oneIpCheck->isChecked());
}

void FtpWidget::onBrowseRoot()
{
    const QString current = m_rootEdit->text().trimmed();
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select FTP Root"),
        current.isEmpty() ? QDir::rootPath() : current);
    if (dir.isEmpty())
        return;
    m_rootEdit->setText(dir);
    saveSettings();
}

void FtpWidget::onAnonymousToggled(bool checked)
{
    m_userEdit->setEnabled(!checked);
    m_passwordEdit->setEnabled(!checked);
    saveSettings();
}

void FtpWidget::onServerState(bool listening, const QString &message)
{
    m_ftpListening = listening;
    m_statusLabel->setText(message);
    updateFtpButtons();
}

void FtpWidget::onPeerConnected(const QString &ip)
{
    m_statusLabel->setText(tr("Connected to %1").arg(ip));
}

void FtpWidget::onLogLine(const QString &line)
{
    m_log->appendPlainText(line);
}

void FtpWidget::setClientConnected(bool connected)
{
    m_clientConnected = connected;
    updateClientButtons();
}

void FtpWidget::updateClientButtons()
{
    m_connectBtn->setText(m_clientConnected ? tr("Disconnect") : tr("Connect"));
    m_refreshBtn->setText(tr("Refresh"));
    m_downloadBtn->setText(tr("Download"));
    m_uploadBtn->setText(tr("Upload"));
    m_cancelBtn->setText(tr("Cancel"));
    const bool idle = m_clientConnected && !m_transferring;
    m_upBtn->setEnabled(idle && m_clientPath != QLatin1String("/"));
    m_refreshBtn->setEnabled(idle);
    m_uploadBtn->setEnabled(idle);
    m_downloadBtn->setEnabled(idle && !selectedClientName().isEmpty() && !selectedClientIsDir());
    m_cancelBtn->setEnabled(m_transferring);
    m_connectBtn->setEnabled(!m_transferring);
    const bool editing = !m_clientConnected && !m_transferring;
    m_clientHostEdit->setEnabled(editing);
    m_clientPortEdit->setEnabled(editing);
    m_clientUserEdit->setEnabled(editing);
    m_clientPasswordEdit->setEnabled(editing);
}

void FtpWidget::updateFtpButtons()
{
    m_restartBtn->setText(m_ftpListening ? tr("Stop") : tr("Start"));
    const bool editing = !m_ftpListening;
    m_portEdit->setEnabled(editing);
    m_browseBtn->setEnabled(editing);
    m_userEdit->setEnabled(editing && !m_anonymousCheck->isChecked());
    m_passwordEdit->setEnabled(editing && !m_anonymousCheck->isChecked());
    m_anonymousCheck->setEnabled(editing);
    m_readOnlyCheck->setEnabled(editing);
    m_oneIpCheck->setEnabled(editing);
}

void FtpWidget::updateTftpButtons()
{
    m_tftpToggleBtn->setText(m_tftpRunning ? tr("Stop") : tr("Start"));
    m_tftpDownloadBtn->setText(tr("Download"));
    m_tftpUploadBtn->setText(tr("Upload"));
    m_tftpCancelBtn->setText(tr("Cancel"));
    m_tftpDownloadBtn->setEnabled(!m_tftpBusy);
    m_tftpUploadBtn->setEnabled(!m_tftpBusy);
    m_tftpCancelBtn->setEnabled(m_tftpBusy);
    m_tftpPortEdit->setEnabled(!m_tftpRunning);
    m_tftpBrowseBtn->setEnabled(!m_tftpRunning);
    m_tftpToggleBtn->setEnabled(!m_tftpBusy);
}

QString FtpWidget::selectedClientName() const
{
    const QTreeWidgetItem *item = m_fileList->currentItem();
    return item ? item->text(0) : QString();
}

bool FtpWidget::selectedClientIsDir() const
{
    const QTreeWidgetItem *item = m_fileList->currentItem();
    return item && item->data(0, Qt::UserRole).toBool();
}

void FtpWidget::beginClientPath(const QString &path)
{
    m_pendingPath = path.isEmpty() ? QStringLiteral("/") : path;
    m_hasPendingPath = true;
    m_navOk = true;
}

QString FtpWidget::parentClientPath(const QString &path)
{
    if (path.size() <= 1)
        return QStringLiteral("/");
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    if (slash <= 0)
        return QStringLiteral("/");
    return path.left(slash);
}

void FtpWidget::onClientConnect()
{
    if (m_clientConnected) {
        emit clientDisconnectRequested();
        return;
    }
    bool ok = false;
    const int port = m_clientPortEdit->text().trimmed().toInt(&ok);
    const QString host = m_clientHostEdit->text().trimmed();
    if (host.isEmpty() || !ok || port < 1 || port > 65535) {
        onClientStatus(tr("Enter a server and a port."));
        return;
    }
    saveSettings();
    emit clientConnectRequested(host, static_cast<quint16>(port),
                                m_clientUserEdit->text(), m_clientPasswordEdit->text());
}

void FtpWidget::onClientUp()
{
    const QString parent = parentClientPath(m_clientPath);
    beginClientPath(parent);
    emit clientUpRequested(parent);
}

void FtpWidget::onClientRefresh()
{
    beginClientPath(m_clientPath.isEmpty() ? QStringLiteral("/") : m_clientPath);
    emit clientRefreshRequested();
}

bool FtpWidget::warnIfSaveDirServed(const QString &dir)
{
    const QString saveDir = QFileInfo(dir).canonicalFilePath();
    const QString root = QFileInfo(m_rootEdit->text().trimmed()).canonicalFilePath();
    QString rel = m_clientPath;
    if (rel.startsWith(QLatin1Char('/')))
        rel.remove(0, 1);
    const QString listed = QFileInfo(QDir(m_rootEdit->text().trimmed()).filePath(rel)).canonicalFilePath();
    // 下载会先用写入方式打开目标，同一目录时这一步会把正在读的原文件清空。
    if (saveDir.isEmpty() || (saveDir != root && saveDir != listed))
        return false;
    QMessageBox::warning(this, tr("FTP Client"),
                         tr("Cannot save into a folder the FTP server is sharing. Downloading there overwrites the original files."));
    return true;
}

void FtpWidget::onClientDownload()
{
    const QString name = selectedClientName();
    if (name.isEmpty() || selectedClientIsDir() || m_transferring)
        return;
    const QString dir = m_saveDirEdit->text().trimmed();
    if (dir.isEmpty()) {
        onClientStatus(tr("Choose a folder to save downloads."));
        return;
    }
    if (warnIfSaveDirServed(dir))
        return;
    saveSettings();
    emit clientDownloadRequested(name, dir);
}

void FtpWidget::onClientUpload()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Upload file"));
    if (path.isEmpty())
        return;
    emit clientUploadRequested(path);
}

void FtpWidget::onClientCancel()
{
    emit clientCancelRequested();
}

void FtpWidget::onClientBrowseSave()
{
    const QString current = m_saveDirEdit->text().trimmed();
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Choose a folder to save downloads."),
        current.isEmpty() ? QDir::homePath() : current);
    if (dir.isEmpty() || warnIfSaveDirServed(dir))
        return;
    m_saveDirEdit->setText(dir);
    saveSettings();
}

void FtpWidget::onClientItemActivated(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);
    if (!item || m_transferring || !m_clientConnected)
        return;
    const QString name = item->text(0);
    if (item->data(0, Qt::UserRole).toBool()) {
        QString next = m_clientPath.isEmpty() ? QStringLiteral("/") : m_clientPath;
        if (!next.endsWith(QLatin1Char('/')))
            next += QLatin1Char('/');
        next += name;
        beginClientPath(next);
        emit clientEnterRequested(name);
        return;
    }
    onClientDownload();
}

void FtpWidget::onClientSelectionChanged()
{
    updateClientButtons();
}

void FtpWidget::onClientStatus(const QString &text)
{
    m_clientStatus->setText(text);
}

void FtpWidget::onClientClosed()
{
    m_transferring = false;
    m_clientPath.clear();
    m_pendingPath.clear();
    m_hasPendingPath = false;
    m_fileList->clear();
    m_pathLabel->clear();
    m_progress->setRange(0, 1);
    m_progress->setValue(0);
    setClientConnected(false);
}

void FtpWidget::onClientLoggedIn()
{
    m_clientPath = QStringLiteral("/");
    beginClientPath(QStringLiteral("/"));
    setClientConnected(true);
}

void FtpWidget::onListingStarted()
{
    m_fileList->clear();
}

void FtpWidget::onListItem(const QString &name, qint64 size, const QString &owner,
                           const QString &group, const QString &time, bool isDir)
{
    if (!m_clientConnected)
        return;
    auto *item = new QTreeWidgetItem(m_fileList);
    item->setText(0, name);
    item->setData(0, Qt::UserRole, isDir);
    item->setText(1, isDir ? QString() : QString::number(size));
    item->setText(2, owner);
    item->setText(3, group);
    item->setText(4, time);
}

void FtpWidget::onListFinished()
{
    if (!m_clientConnected)
        return;
    if (m_hasPendingPath && m_navOk)
        m_clientPath = m_pendingPath;
    const bool failed = m_hasPendingPath && !m_navOk;
    m_hasPendingPath = false;
    m_navOk = true;
    m_pathLabel->setText(m_clientPath);
    if (!failed) {
        const int count = m_fileList->topLevelItemCount();
        if (count == 0)
            m_clientStatus->setText(tr("Empty directory %1").arg(m_clientPath));
        else
            m_clientStatus->setText(tr("%1 item(s) in %2").arg(count).arg(m_clientPath));
    }
    updateClientButtons();
}

void FtpWidget::onTransferActive(bool active)
{
    m_transferring = active;
    if (active) {
        m_progress->setRange(0, 0);
        m_progress->setValue(0);
    }
    updateClientButtons();
}

void FtpWidget::onTransferProgress(qint64 done, qint64 total)
{
    if (total > 0) {
        m_progress->setRange(0, 1000);
        m_progress->setValue(int(qBound(qint64(0), done * 1000 / total, qint64(1000))));
        return;
    }
    m_progress->setRange(0, 0);
}

void FtpWidget::onClientDirectoryFailed()
{
    // 列表还会回来，但是内容仍是原目录。不要把路径改成进失败的那一级。
    m_navOk = false;
}

void FtpWidget::onTftpToggle()
{
    if (m_tftpRunning) {
        emit tftpStopRequested();
        return;
    }
    bool ok = false;
    const int port = m_tftpPortEdit->text().trimmed().toInt(&ok);
    if (!ok || port < 1 || port > 65535) {
        m_tftpStatus->setText(tr("Not listening"));
        return;
    }
    const QString root = m_tftpRootEdit->text().trimmed();
    if (root.isEmpty() || !QDir(root).exists()) {
        m_tftpStatus->setText(tr("Root directory does not exist"));
        return;
    }
    saveSettings();
    emit tftpStartRequested(static_cast<quint16>(port), root);
}

void FtpWidget::onTftpBrowseRoot()
{
    const QString current = m_tftpRootEdit->text().trimmed();
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select TFTP Root"),
        current.isEmpty() ? QDir::homePath() : current);
    if (dir.isEmpty())
        return;
    m_tftpRootEdit->setText(dir);
    saveSettings();
}

void FtpWidget::onTftpDownload()
{
    if (m_tftpBusy)
        return;
    QString remote = m_tftpRemoteEdit->text().trimmed();
    QString local = m_tftpLocalEdit->text().trimmed();
    if (remote.isEmpty() && !local.isEmpty() && !QFileInfo(local).isDir())
        remote = QFileInfo(local).fileName();
    if (QFileInfo(local).isDir()) {
        if (remote.isEmpty()) {
            m_tftpClientStatus->setText(tr("Enter a remote file and a local file."));
            return;
        }
        local = QDir(local).filePath(QFileInfo(remote).fileName());
    } else if (local.isEmpty()) {
        const QString suggested = QFileInfo(remote).fileName();
        local = QFileDialog::getSaveFileName(this, tr("Save file"),
                                             QDir::home().filePath(suggested));
        if (local.isEmpty())
            return;
    }
    if (remote.isEmpty())
        remote = QFileInfo(local).fileName();
    local = QFileInfo(local).absoluteFilePath();
    if (remote.isEmpty() || local.isEmpty()) {
        m_tftpClientStatus->setText(tr("Enter a remote file and a local file."));
        return;
    }
    const QString rootText = m_tftpRootEdit->text().trimmed();
    QString rel = remote;
    rel.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (rel.startsWith(QLatin1Char('/')))
        rel.remove(0, 1);
    const QString served = QFileInfo(QDir(rootText).filePath(rel)).absoluteFilePath();
    const Qt::CaseSensitivity pathCase =
#ifdef Q_OS_WIN
        Qt::CaseInsensitive;
#else
        Qt::CaseSensitive;
#endif
    if (QDir::cleanPath(local).compare(QDir::cleanPath(served), pathCase) == 0) {
        QMessageBox::warning(this, tr("TFTP Client"),
                             tr("Cannot save into a folder the TFTP server is sharing. Downloading there overwrites the original files."));
        return;
    }
    bool ok = false;
    const int port = m_tftpClientPortEdit->text().trimmed().toInt(&ok);
    const QString host = m_tftpHostEdit->text().trimmed();
    if (host.isEmpty() || !ok || port < 1 || port > 65535) {
        m_tftpClientStatus->setText(tr("Enter a server and a port."));
        return;
    }
    m_tftpRemoteEdit->setText(remote);
    m_tftpLocalEdit->setText(QDir::toNativeSeparators(local));
    saveSettings();
    m_tftpProgress->setRange(0, 0);
    m_tftpProgress->setValue(0);
    emit tftpTransferRequested(host, static_cast<quint16>(port), remote, local, true);
}

void FtpWidget::onTftpUpload()
{
    if (m_tftpBusy)
        return;
    QString remote = m_tftpRemoteEdit->text().trimmed();
    QString local = m_tftpLocalEdit->text().trimmed();
    if (local.isEmpty() || !QFileInfo(local).isFile()) {
        local = QFileDialog::getOpenFileName(this, tr("Upload file"), m_tftpLocalEdit->text());
        if (local.isEmpty())
            return;
        m_tftpLocalEdit->setText(local);
    }
    if (remote.isEmpty())
        remote = QFileInfo(local).fileName();
    if (remote.isEmpty() || local.isEmpty()) {
        m_tftpClientStatus->setText(tr("Enter a remote file and a local file."));
        return;
    }
    bool ok = false;
    const int port = m_tftpClientPortEdit->text().trimmed().toInt(&ok);
    const QString host = m_tftpHostEdit->text().trimmed();
    if (host.isEmpty() || !ok || port < 1 || port > 65535) {
        m_tftpClientStatus->setText(tr("Enter a server and a port."));
        return;
    }
    m_tftpRemoteEdit->setText(remote);
    saveSettings();
    m_tftpProgress->setRange(0, 0);
    m_tftpProgress->setValue(0);
    emit tftpTransferRequested(host, static_cast<quint16>(port), remote, local, false);
}

void FtpWidget::onTftpBrowseLocal()
{
    const QString current = m_tftpLocalEdit->text().trimmed();
    const QFileInfo currentInfo(current);
    const QString start = currentInfo.isDir()
                              ? current
                              : (current.isEmpty() ? QDir::homePath() : currentInfo.absolutePath());
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Choose a folder to save downloads."), start);
    if (dir.isEmpty())
        return;
    const QString chosen = QFileInfo(dir).canonicalFilePath();
    const QString root = QFileInfo(m_tftpRootEdit->text().trimmed()).canonicalFilePath();
    if (!chosen.isEmpty() && chosen == root) {
        QMessageBox::warning(this, tr("TFTP Client"),
                             tr("Cannot save into a folder the TFTP server is sharing. Downloading there overwrites the original files."));
        return;
    }
    m_tftpLocalEdit->setText(QDir::toNativeSeparators(dir));
    saveSettings();
}

void FtpWidget::onTftpCancel()
{
    emit tftpCancelRequested();
}

void FtpWidget::onTftpRunning(bool running, const QString &message)
{
    m_tftpRunning = running;
    m_tftpStatus->setText(message);
    updateTftpButtons();
}

void FtpWidget::onTftpServerLog(const QString &line)
{
    m_tftpServerLog->appendPlainText(line);
}

void FtpWidget::onTftpClientLog(const QString &line)
{
    m_tftpClientLog->appendPlainText(line);
}

void FtpWidget::onTftpProgress(qint64 done, qint64 total)
{
    if (total > 0) {
        m_tftpProgress->setRange(0, 1000);
        m_tftpProgress->setValue(int(qBound(qint64(0), done * 1000 / total, qint64(1000))));
        m_tftpClientStatus->setText(tr("%1 / %2").arg(done).arg(total));
        return;
    }
    m_tftpProgress->setRange(0, 0);
    m_tftpClientStatus->setText(tr("%1 bytes").arg(done));
}

void FtpWidget::onTftpFinished(bool success, const QString &message)
{
    m_tftpClientLog->appendPlainText(message);
    const QString saved = m_tftpLocalEdit->text().trimmed();
    m_tftpClientStatus->setText(saved.isEmpty() ? message : message + QLatin1Char('\n') + saved);
    if (m_tftpProgress->minimum() == 0 && m_tftpProgress->maximum() == 0) {
        m_tftpProgress->setRange(0, 1);
        m_tftpProgress->setValue(success ? 1 : 0);
    } else if (success) {
        m_tftpProgress->setValue(m_tftpProgress->maximum());
    }
}

void FtpWidget::onTftpActive(bool active)
{
    m_tftpBusy = active;
    updateTftpButtons();
}

void FtpWidget::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QWidget::changeEvent(event);
}
