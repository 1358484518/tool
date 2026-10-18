#include "NetAssistWidget.h"
#include "FastTextView.h"
#include "NetworkWorker.h"
#include "SettingsWidget.h"
#include "common/ProtocolUtils.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QNetworkInterface>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

namespace {

const char kBtnGreen[] =
    "QPushButton { background-color: #4CAF50; color: white; font-weight: bold; padding: 3px; border-radius: 2px; }"
    "QPushButton:hover { background-color: #43A047; }"
    "QPushButton:disabled { background-color: #A5D6A7; color: #f5f5f5; }";
const char kBtnRed[] =
    "QPushButton { background-color: #f44336; color: white; font-weight: bold; padding: 3px; border-radius: 2px; }"
    "QPushButton:hover { background-color: #E53935; }"
    "QPushButton:disabled { background-color: #EF9A9A; color: #f5f5f5; }";
const char kBtnBlue[] =
    "QPushButton { background-color: #2196F3; color: white; font-weight: bold; padding: 4px; border-radius: 2px; }"
    "QPushButton:hover { background-color: #1E88E5; }"
    "QPushButton:disabled { background-color: #90CAF9; color: white; }";
const char kBtnOrange[] =
    "QPushButton { background-color: #FF9800; color: white; font-weight: bold; padding: 3px; border-radius: 2px; }"
    "QPushButton:hover { background-color: #FB8C00; }"
    "QPushButton:disabled { background-color: #FFCC80; color: white; }";
const char kEditBorder[] =
    "QPlainTextEdit { background-color: #ffffff; color: #000000; border: 1px solid #c0c0c0; }";

QFrame *makeHLine(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    return line;
}

} // namespace

NetAssistWidget::NetAssistWidget(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(tr("Network Debug Assistant"));
    initUi();
    initConnect();
    initNetWork();
    loadSettings();
}

IoSource *NetAssistWidget::ioSource() const
{
    return m_netWorker;
}

NetAssistWidget::~NetAssistWidget()
{
    saveSettings();
    if (m_netWorker) {
        m_netWorker->disconnect();
        disconnect(this, nullptr, m_netWorker, nullptr);

        NetworkWorker *worker = m_netWorker;
        m_netWorker = nullptr;
        // 后端一直待在工作线程：就地关掉套接字并 delete，不要 move 回 UI。
        if (workerThread.isRunning() && worker->thread() == &workerThread) {
            QMetaObject::invokeMethod(worker, [worker]() {
                worker->slotCloseNetwork();
                delete worker;
            }, Qt::BlockingQueuedConnection);
            workerThread.quit();
            workerThread.wait(5000);
        } else {
            delete worker;
        }
    }
}

void NetAssistWidget::initUi()
{
    auto *mainLayout = new QHBoxLayout(this);
    mainLayout->setSpacing(6);
    mainLayout->setContentsMargins(6, 6, 6, 6);

    // ========== 左侧：接收 + 发送 ==========
    auto *leftPanel = new QWidget(this);
    auto *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setSpacing(4);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    m_editRecv = new FastTextView(leftPanel);
    m_editRecv->setStyleSheet(QString::fromLatin1(kEditBorder));
    leftLayout->addWidget(m_editRecv, 2);

    auto *sendPanel = new QFrame(leftPanel);
    sendPanel->setFrameShape(QFrame::StyledPanel);
    sendPanel->setFrameShadow(QFrame::Plain);
    sendPanel->setMinimumHeight(180);
    auto *sendLayout = new QHBoxLayout(sendPanel);
    sendLayout->setSpacing(4);
    sendLayout->setContentsMargins(4, 4, 4, 4);

    m_editSend = new QPlainTextEdit(sendPanel);
    m_editSend->setFont(QFont(QStringLiteral("Consolas"), 10));
    m_editSend->setPlaceholderText(tr("Enter data to send... (Ctrl+Enter to send)"));
    m_editSend->setStyleSheet(QString::fromLatin1(kEditBorder));
    sendLayout->addWidget(m_editSend, 1);

    auto *sendBtnCol = new QVBoxLayout();
    sendBtnCol->setSpacing(4);
    m_btnSend = new QPushButton(tr("Send"), sendPanel);
    m_btnSend->setMinimumWidth(70);
    m_btnSend->setEnabled(false);
    m_btnSend->setStyleSheet(QString::fromLatin1(kBtnBlue));
    sendBtnCol->addWidget(m_btnSend);
    m_btnClearSend = new QPushButton(tr("Clear"), sendPanel);
    m_btnClearSend->setMinimumWidth(70);
    sendBtnCol->addWidget(m_btnClearSend);
    sendBtnCol->addStretch();
    sendLayout->addLayout(sendBtnCol);
    leftLayout->addWidget(sendPanel, 1);
    mainLayout->addWidget(leftPanel, 5);

    // ========== 右侧设置栏 ==========
    auto *rightPanel = new QWidget(this);
    // 只留完整 IPv4 和端口的宽度，例如 255.255.255.255:65535。
    rightPanel->setMinimumWidth(240);
    rightPanel->setMaximumWidth(260);
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setSpacing(6);
    rightLayout->setContentsMargins(4, 0, 0, 0);

    m_groupConnection = new QGroupBox(tr("Connection"), rightPanel);
    auto *connGrid = new QGridLayout(m_groupConnection);
    connGrid->setSpacing(4);
    connGrid->setContentsMargins(6, 14, 6, 6);
    int r = 0;

    m_protocolLabel = new QLabel(tr("Protocol:"), m_groupConnection);
    connGrid->addWidget(m_protocolLabel, r, 0);
    m_cmbProtocol = new QComboBox(m_groupConnection);
    m_cmbProtocol->addItem(tr("TCP Server"), QVariant::fromValue(NetProtocol::TcpServer));
    m_cmbProtocol->addItem(tr("TCP Client"), QVariant::fromValue(NetProtocol::TcpClient));
    m_cmbProtocol->addItem(tr("UDP"), QVariant::fromValue(NetProtocol::Udp));
    connGrid->addWidget(m_cmbProtocol, r, 1);
    r++;

    m_addressLabel = new QLabel(tr("Address:"), m_groupConnection);
    connGrid->addWidget(m_addressLabel, r, 0);
    m_cmbLocalIp = new QComboBox(m_groupConnection);
    m_cmbLocalIp->addItem(tr("0.0.0.0 (All)"));
    for (const QHostAddress &addr : QNetworkInterface::allAddresses()) {
        if (addr.protocol() == QAbstractSocket::IPv4Protocol && addr != QHostAddress::LocalHost)
            m_cmbLocalIp->addItem(addr.toString());
    }
    m_cmbLocalIp->addItem(tr("127.0.0.1 (Local)"));
    connGrid->addWidget(m_cmbLocalIp, r, 1);
    r++;

    m_localPortLabel = new QLabel(tr("LocalPort:"), m_groupConnection);
    connGrid->addWidget(m_localPortLabel, r, 0);
    m_spinLocalPort = new QSpinBox(m_groupConnection);
    m_spinLocalPort->setRange(1, 65535);
    m_spinLocalPort->setValue(13601);
    connGrid->addWidget(m_spinLocalPort, r, 1);
    r++;

    m_remoteLabel = new QLabel(tr("Remote:"), m_groupConnection);
    connGrid->addWidget(m_remoteLabel, r, 0);
    m_cmbRemoteAddr = new QComboBox(m_groupConnection);
    m_cmbRemoteAddr->setEditable(true);
    m_cmbRemoteAddr->setToolTip(tr("127.0.0.1:80, www.example.com:443, https://www.example.com"));
    RemoteHost addr;
    addr.port = 13601;
    addr.address = QHostAddress(QStringLiteral("127.0.0.1"));
    addr.host = QStringLiteral("127.0.0.1");
    addHostAddr(addr);
    if (m_cmbRemoteAddr->lineEdit()) {
        m_cmbRemoteAddr->lineEdit()->setPlaceholderText(
            tr("host:port"));
        m_cmbRemoteAddr->lineEdit()->home(false);  // 窄下拉框停在开头，避免只看见域名尾巴
    }
    connGrid->addWidget(m_cmbRemoteAddr, r, 1);
    rightLayout->addWidget(m_groupConnection);

    m_btnOpen = new QPushButton(tr("Open"), rightPanel);
    m_btnOpen->setCheckable(true);
    m_btnOpen->setMinimumHeight(28);
    applyOpenButtonStyle(false);
    rightLayout->addWidget(m_btnOpen);

    m_btnConnect = new QPushButton(tr("Connect"), rightPanel);
    m_btnConnect->setEnabled(false);
    m_btnConnect->setMinimumHeight(28);
    applyConnectButtonStyle(false);
    rightLayout->addWidget(m_btnConnect);

    rightLayout->addWidget(makeHLine(rightPanel));

    m_groupSendSetting = new QGroupBox(tr("Send Settings"), rightPanel);
    auto *sendGrid = new QGridLayout(m_groupSendSetting);
    sendGrid->setSpacing(4);
    sendGrid->setContentsMargins(6, 14, 6, 6);
    r = 0;
    m_checkAutoSend = new QCheckBox(tr("Timed Send"), m_groupSendSetting);
    sendGrid->addWidget(m_checkAutoSend, r, 0, 1, 2);
    r++;

    auto *intervalRow = new QHBoxLayout();
    intervalRow->setSpacing(4);
    m_intervalLabel = new QLabel(tr("Interval:"), m_groupSendSetting);
    intervalRow->addWidget(m_intervalLabel);
    m_spinAutoSendInterval = new QSpinBox(m_groupSendSetting);
    m_spinAutoSendInterval->setRange(100, 60000);
    m_spinAutoSendInterval->setValue(1000);
    m_spinAutoSendInterval->setSuffix(tr("ms"));
    m_spinAutoSendInterval->setEnabled(false);
    intervalRow->addWidget(m_spinAutoSendInterval, 1);
    sendGrid->addLayout(intervalRow, r, 0, 1, 2);
    r++;

    m_checkHexSend = new QCheckBox(tr("Hex Send"), m_groupSendSetting);
    sendGrid->addWidget(m_checkHexSend, r, 0);
    m_checkAddModbusCrc16 = new QCheckBox(tr("CRC16"), m_groupSendSetting);
    sendGrid->addWidget(m_checkAddModbusCrc16, r, 1);
    r++;

    m_checkAppendCRLF = new QCheckBox(tr("Add CRLF"), m_groupSendSetting);
    sendGrid->addWidget(m_checkAppendCRLF, r, 0);
    m_checkBroadcastSend = new QCheckBox(tr("Broadcast"), m_groupSendSetting);
    sendGrid->addWidget(m_checkBroadcastSend, r, 1);
    rightLayout->addWidget(m_groupSendSetting);

    rightLayout->addWidget(makeHLine(rightPanel));

    m_groupRecvSetting = new QGroupBox(tr("Receive Settings"), rightPanel);
    auto *recvGrid = new QGridLayout(m_groupRecvSetting);
    recvGrid->setSpacing(4);
    recvGrid->setContentsMargins(6, 14, 6, 6);
    r = 0;

    auto *recvBtnRow = new QHBoxLayout();
    recvBtnRow->setSpacing(4);
    m_btnSaveLog = new QPushButton(tr("Save Log"), m_groupRecvSetting);
    recvBtnRow->addWidget(m_btnSaveLog);
    m_btnClearRecv = new QPushButton(tr("Clear Recv"), m_groupRecvSetting);
    recvBtnRow->addWidget(m_btnClearRecv);
    recvGrid->addLayout(recvBtnRow, r, 0, 1, 2);
    r++;

    m_checkHexRecv = new QCheckBox(tr("Hex Disp"), m_groupRecvSetting);
    recvGrid->addWidget(m_checkHexRecv, r, 0);
    m_checkTimestamp = new QCheckBox(tr("Timestamp"), m_groupRecvSetting);
    recvGrid->addWidget(m_checkTimestamp, r, 1);
    r++;

    m_scrollToBottom = new QCheckBox(tr("Auto Scroll"), m_groupRecvSetting);
    m_scrollToBottom->setChecked(true);
    recvGrid->addWidget(m_scrollToBottom, r, 0);
    m_checkShowRecvAddr = new QCheckBox(tr("Show Addr"), m_groupRecvSetting);
    recvGrid->addWidget(m_checkShowRecvAddr, r, 1);
    r++;

    m_checkShowSend = new QCheckBox(tr("Show Send"), m_groupRecvSetting);
    m_checkShowSend->setChecked(true);
    recvGrid->addWidget(m_checkShowSend, r, 0);
    m_checkShowDirection = new QCheckBox(tr("Show Dir"), m_groupRecvSetting);
    recvGrid->addWidget(m_checkShowDirection, r, 1);
    r++;

    m_autoSaveCheck = new QCheckBox(tr("Auto Save"), m_groupRecvSetting);
    m_autoSaveCheck->setToolTip(tr("Append new receive text to net_auto.txt in the log directory"));
    recvGrid->addWidget(m_autoSaveCheck, r, 0, 1, 2);
    rightLayout->addWidget(m_groupRecvSetting);

    m_btnResetCount = new QPushButton(tr("Reset Counter"), rightPanel);
    rightLayout->addWidget(m_btnResetCount);

    rightLayout->addWidget(makeHLine(rightPanel));

    auto *countRow = new QHBoxLayout();
    countRow->setSpacing(10);
    m_labelRecvCount = new QLabel(tr("R: %1 B").arg(0), rightPanel);
    countRow->addWidget(m_labelRecvCount);
    m_labelSendCount = new QLabel(tr("S: %1 B").arg(0), rightPanel);
    countRow->addWidget(m_labelSendCount);
    countRow->addStretch();
    rightLayout->addLayout(countRow);

    m_labelStatus = new QLabel(rightPanel);
    setStatus(QStringLiteral("Closed"), QStringLiteral("#f44336"));
    rightLayout->addWidget(m_labelStatus);

    m_labelClientCount = new QLabel(rightPanel);
    showClientText(QStringLiteral("Clients: %1"), 0);
    m_labelClientCount->setWordWrap(true);
    rightLayout->addWidget(m_labelClientCount);

    rightLayout->addStretch();
    mainLayout->addWidget(rightPanel);

    m_timerAutoSend = new QTimer(this);
}

void NetAssistWidget::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QWidget::changeEvent(event);
}

void NetAssistWidget::retranslateUi()
{
    setWindowTitle(tr("Network Debug Assistant"));
    m_editSend->setPlaceholderText(tr("Enter data to send... (Ctrl+Enter to send)"));
    m_btnSend->setText(tr("Send"));
    m_btnClearSend->setText(tr("Clear"));
    m_groupConnection->setTitle(tr("Connection"));
    m_protocolLabel->setText(tr("Protocol:"));
    m_cmbProtocol->setItemText(0, tr("TCP Server"));
    m_cmbProtocol->setItemText(1, tr("TCP Client"));
    m_cmbProtocol->setItemText(2, tr("UDP"));
    m_addressLabel->setText(tr("Address:"));
    const int allIp = m_cmbLocalIp->findText(QStringLiteral("0.0.0.0"), Qt::MatchStartsWith);
    if (allIp >= 0)
        m_cmbLocalIp->setItemText(allIp, tr("0.0.0.0 (All)"));
    const int localIp = m_cmbLocalIp->findText(QStringLiteral("127.0.0.1"), Qt::MatchStartsWith);
    if (localIp >= 0)
        m_cmbLocalIp->setItemText(localIp, tr("127.0.0.1 (Local)"));
    m_localPortLabel->setText(tr("LocalPort:"));
    m_remoteLabel->setText(tr("Remote:"));
    m_cmbRemoteAddr->setToolTip(tr("127.0.0.1:80, www.example.com:443, https://www.example.com"));
    if (m_cmbRemoteAddr->lineEdit())
        m_cmbRemoteAddr->lineEdit()->setPlaceholderText(tr("host:port"));
    m_groupSendSetting->setTitle(tr("Send Settings"));
    m_checkAutoSend->setText(tr("Timed Send"));
    m_intervalLabel->setText(tr("Interval:"));
    m_spinAutoSendInterval->setSuffix(tr("ms"));
    m_checkHexSend->setText(tr("Hex Send"));
    m_checkAddModbusCrc16->setText(tr("CRC16"));
    m_checkAppendCRLF->setText(tr("Add CRLF"));
    m_checkBroadcastSend->setText(tr("Broadcast"));
    m_groupRecvSetting->setTitle(tr("Receive Settings"));
    m_btnSaveLog->setText(tr("Save Log"));
    m_btnClearRecv->setText(tr("Clear Recv"));
    m_checkHexRecv->setText(tr("Hex Disp"));
    m_checkTimestamp->setText(tr("Timestamp"));
    m_scrollToBottom->setText(tr("Auto Scroll"));
    m_checkShowRecvAddr->setText(tr("Show Addr"));
    m_checkShowSend->setText(tr("Show Send"));
    m_checkShowDirection->setText(tr("Show Dir"));
    m_autoSaveCheck->setText(tr("Auto Save"));
    m_autoSaveCheck->setToolTip(tr("Append new receive text to net_auto.txt in the log directory"));
    m_btnResetCount->setText(tr("Reset Counter"));
    applyOpenButtonStyle(m_btnOpen->isChecked());
    setConnectLatched(m_showDisconnect);
    updateCountLabel();
    if (!m_statusEnglish.isEmpty())
        setStatus(m_statusEnglish, m_statusColor);
    if (!m_clientEnglish.isEmpty())
        showClientText(m_clientEnglish, m_clientNumber);
}

void NetAssistWidget::initConnect()
{
    connect(m_cmbProtocol, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &NetAssistWidget::onProtocolChanged);
    connect(m_btnOpen, &QPushButton::toggled, this, &NetAssistWidget::onOpenToggled);
    connect(m_btnConnect, &QPushButton::clicked, this, &NetAssistWidget::onConnectClicked);
    connect(m_btnClearRecv, &QPushButton::clicked, this, &NetAssistWidget::onClearRecv);
    connect(m_btnSaveLog, &QPushButton::clicked, this, &NetAssistWidget::onSaveLog);
    connect(m_autoSaveCheck, &QCheckBox::toggled, this, &NetAssistWidget::setAutoSaveEnabled);
    connect(m_checkAutoSend, &QCheckBox::toggled, this, &NetAssistWidget::onAutoSendToggled);
    connect(m_spinAutoSendInterval, QOverload<int>::of(&QSpinBox::valueChanged), this, &NetAssistWidget::onAutoSendIntervalChanged);
    connect(m_timerAutoSend, &QTimer::timeout, this, &NetAssistWidget::onSendClicked);
    connect(m_btnSend, &QPushButton::clicked, this, &NetAssistWidget::onSendClicked);
    connect(m_btnClearSend, &QPushButton::clicked, m_editSend, &QPlainTextEdit::clear);
    connect(m_btnResetCount, &QPushButton::clicked, this, [this]() {
        m_recvBytes = 0;
        m_sendBytes = 0;
        updateCountLabel();
    });
    auto *sendShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Return")), m_editSend);
    connect(sendShortcut, &QShortcut::activated, this, &NetAssistWidget::onSendClicked);
    auto *sendShortcutPad = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Enter")), m_editSend);
    connect(sendShortcutPad, &QShortcut::activated, this, &NetAssistWidget::onSendClicked);

    // 可选：输入框失焦自动提交
    connect(m_cmbRemoteAddr->lineEdit(), &QLineEdit::editingFinished, this, [this]{
        if (!commitCurrentHost()) {
            // 校验失败自定义提示，比如：
            m_cmbRemoteAddr->lineEdit()->setStyleSheet("border:1px solid red;");
        } else {
            m_cmbRemoteAddr->lineEdit()->setStyleSheet(""); // 校验通过恢复样式
        }
    });

    // 复选框状态自动同步
    connect(m_checkTimestamp, &QCheckBox::toggled, this, [this](bool checked){
        m_showTimestamp = checked;
    });
    connect(m_checkShowRecvAddr, &QCheckBox::toggled, this, [this](bool checked){
        m_showRecvAddr = checked;
    });
    connect(m_checkShowSend, &QCheckBox::toggled, this, [this](bool checked){
        m_showSend = checked;
    });
    connect(m_checkShowDirection, &QCheckBox::toggled, this, [this](bool checked){
        m_showDirection = checked;
    });
    connect(m_checkAddModbusCrc16, &QCheckBox::toggled, this, [this](bool checked){
        m_addModbusCrc16 = checked;
    });
    connect(m_checkAppendCRLF, &QCheckBox::toggled, this, [this](bool checked){
        m_appendCRLF = checked;
    });
    // 广播发送状态自动同步
    connect(m_checkBroadcastSend, &QCheckBox::toggled, this, [this](bool checked){
        m_broadcastSend = checked;
    });

    // 强制初始状态同步（避免UI设计器误勾选导致初始值不一致）
    m_checkTimestamp->setChecked(false);
    m_checkShowRecvAddr->setChecked(false);
    m_checkShowSend->setChecked(true);
    m_checkShowDirection->setChecked(false);
    m_checkAddModbusCrc16->setChecked(false);
    m_checkAppendCRLF->setChecked(false);
    m_checkBroadcastSend->setChecked(false);
}


void NetAssistWidget::onProtocolChanged(int idx)
{
    Q_UNUSED(idx)
    updateProtocolDependentUi();
}

void NetAssistWidget::updateProtocolDependentUi()
{
    const NetProtocol proto = m_cmbProtocol->currentData().value<NetProtocol>();
    const bool opened = m_btnOpen->isChecked();
    const bool isTcpClient = (proto == NetProtocol::TcpClient);
    const bool canBroadcast = (proto == NetProtocol::Udp || proto == NetProtocol::TcpServer);
    m_btnConnect->setEnabled(isTcpClient && opened);
    if (!isTcpClient || !opened)
        applyConnectButtonStyle(false);
    m_checkBroadcastSend->setEnabled(canBroadcast);
    if (!canBroadcast)
        m_checkBroadcastSend->setChecked(false);
}

void NetAssistWidget::onOpenToggled(bool checked)
{
    if (checked) {
        m_btnOpen->setText(tr("Close"));
        m_cmbProtocol->setEnabled(false);
        m_cmbLocalIp->setEnabled(false);
        m_spinLocalPort->setEnabled(false);
        m_btnSend->setEnabled(true);
        setStatus(QStringLiteral("Starting..."), QStringLiteral("#FF9800"));
        applyOpenButtonStyle(true);
        updateCountLabel();

        const NetProtocol proto = m_cmbProtocol->currentData().value<NetProtocol>();
        const QString localIp = m_cmbLocalIp->currentText().split(QLatin1Char(' ')).first();
        const quint16 localPort = static_cast<quint16>(m_spinLocalPort->value());
        emit sigOpenNetwork(proto, localIp, localPort);

        if (proto == NetProtocol::TcpClient) {
            showClientText(QStringLiteral("Not Connected"));
        } else if (proto == NetProtocol::TcpServer) {
            showClientText(QStringLiteral("Clients: %1"), 0);
        } else {
            showClientText(QStringLiteral("UDP"));
        }
        updateProtocolDependentUi();
        // 远程地址已经填好时，打开就要去连。否则状态会停在 Starting...，Connect 也还没发出。
        if (proto == NetProtocol::TcpClient)
            onConnectClicked();
    } else {
        m_btnOpen->setText(tr("Open"));
        m_cmbProtocol->setEnabled(true);
        m_cmbLocalIp->setEnabled(true);
        m_spinLocalPort->setEnabled(true);
        m_btnConnect->setEnabled(false);
        setConnectLatched(false);
        m_btnSend->setEnabled(false);
        m_checkAutoSend->setChecked(false);
        setStatus(QStringLiteral("Closed"), QStringLiteral("#f44336"));
        applyOpenButtonStyle(false);
        showClientText(QStringLiteral("Clients: %1"), 0);
        m_manualTcpDisconnect = true;
        emit sigCloseNetwork();
        updateProtocolDependentUi();
    }
}

void NetAssistWidget::onConnectClicked()
{
    if (!m_showDisconnect) {
        QString remoteIp;
        quint16 remotePort = 0;
        if (!currentRemote(&remoteIp, &remotePort)) {
            setStatus(QStringLiteral("Invalid address"), QStringLiteral("#f44336"));
            return;
        }
        m_manualTcpDisconnect = false;
        emit sigTcpConnect(remoteIp, remotePort);
        setConnectLatched(true);
        setStatus(QStringLiteral("Connecting..."), QStringLiteral("#FF9800"));
    } else {
        m_manualTcpDisconnect = true;
        emit sigTcpDisconnect();
        setConnectLatched(false);
        setStatus(QStringLiteral("Disconnected"), QStringLiteral("#f44336"));
        showClientText(QStringLiteral("Not Connected"));
    }
}

void NetAssistWidget::onClearRecv()
{
    m_editRecv->clear();
    m_recvBytes = 0;
    updateCountLabel();
}

void NetAssistWidget::onSaveLog()
{
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save Log"),
        SettingsWidget::logFilePath(QStringLiteral("netlog.txt")),
        tr("Text Files (*.txt)"));
    if (!path.isEmpty()) {
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(m_editRecv->getData());
            file.close();
            const QString oldStatus = m_labelStatus->text();
            const QString oldStyle = m_labelStatus->styleSheet();
            setStatus(QStringLiteral("Log saved"), QStringLiteral("#4CAF50"));
            QTimer::singleShot(2000, this, [this, oldStatus, oldStyle]() {
                if (m_labelStatus->text().contains(tr("Log saved"))) {
                    m_labelStatus->setText(oldStatus);
                    m_labelStatus->setStyleSheet(oldStyle);
                }
            });
        }
    }
}

void NetAssistWidget::onAutoSendToggled(bool checked)
{
    m_spinAutoSendInterval->setEnabled(checked);
    if (checked) m_timerAutoSend->start(m_spinAutoSendInterval->value());
    else m_timerAutoSend->stop();
}

void NetAssistWidget::onAutoSendIntervalChanged(int ms)
{
    if (m_checkAutoSend->isChecked()) m_timerAutoSend->start(ms);
}

void NetAssistWidget::onSendClicked()
{
    QString payload = m_editSend->toPlainText();
    if (payload.isEmpty())
        return;

    QByteArray data;
    if (m_checkHexSend->isChecked()) {
        data = ProtocolUtils::hexStringToBytes(payload);
        if (m_addModbusCrc16 && !data.isEmpty())
            data = ProtocolUtils::appendCrc16Modbus(data);
    } else {
        data = payload.toUtf8();
        if (m_appendCRLF)
            data.append("\r\n");
    }
    if (data.isEmpty())
        return;

    QString remoteIp;
    quint16 remotePort = 0;
    if (m_broadcastSend) {
        RemoteHost h = m_cmbRemoteAddr->currentData().value<RemoteHost>();
        remotePort = h.port ? h.port : static_cast<quint16>(m_spinLocalPort->value());
    } else if (!currentRemote(&remoteIp, &remotePort)) {
        const NetProtocol proto = m_cmbProtocol->currentData().value<NetProtocol>();
        if (proto != NetProtocol::TcpClient) {
            setStatus(QStringLiteral("Select a valid remote address"), QStringLiteral("#f44336"));
            return;
        }
    }

    emit sigSendData(data, remoteIp, remotePort);
    m_sendBytes += static_cast<quint64>(data.size());
    updateCountLabel();
    appendTrafficLog(true, data, remoteIp, remotePort);
}

void NetAssistWidget::onConnectAddrChange(QString ip, quint16 port)
{
    RemoteHost host;
    host.address = QHostAddress(ip);
    host.host = ip;
    host.port = port;
    host.lastSeen = QDateTime::currentMSecsSinceEpoch();
    addHostAddr(host);
}

void NetAssistWidget::onIoData(const IoPacket &packet)
{
    slotRecvData(packet.data, packet.peer, packet.port);
    emit ioDataReceived(packet);
}

void NetAssistWidget::slotRecvData(QByteArray data, QString fromIp, quint16 fromPort)
{
    m_recvBytes += data.size();
    updateCountLabel();
    appendTrafficLog(false, data, fromIp, fromPort);
}

void NetAssistWidget::slotClientConnected(QString ip, quint16 port)
{
    appendLog(tr("[Connect] %1:%2").arg(ip).arg(port), Qt::darkGreen);
}

void NetAssistWidget::slotClientDisconnected(QString ip, quint16 port)
{
    appendLog(tr("[Disconnect] %1:%2").arg(ip).arg(port), Qt::darkGray);
}

void NetAssistWidget::slotTcpConnected()
{
    appendLog(tr("TCP connected"), Qt::darkGreen);
    setStatus(QStringLiteral("Connected"), QStringLiteral("#4CAF50"));
    setConnectLatched(true);
    showClientText(QStringLiteral("Connected"));
}

void NetAssistWidget::slotTcpDisconnected()
{
    appendLog(tr("TCP disconnected"), Qt::darkGray);
    if (m_manualTcpDisconnect) {
        setStatus(QStringLiteral("Disconnected"), QStringLiteral("#f44336"));
        setConnectLatched(false);
    } else {
        setStatus(QStringLiteral("Reconnecting..."), QStringLiteral("#FF9800"));
        setConnectLatched(true);
    }
    showClientText(QStringLiteral("Not Connected"));
}

void NetAssistWidget::slotError(QString errStr)
{
    appendLog(tr("[Error] %1").arg(errStr), Qt::red);
    setStatus(errStr, QStringLiteral("#f44336"));
}

void NetAssistWidget::slotStateText(QString text)
{
    QString color = QStringLiteral("#FF9800");
    const QString lower = text.toLower();
    if (lower.contains(QLatin1String("fail")) || lower.contains(QLatin1String("error"))
        || lower.contains(QLatin1String("invalid")) || lower.contains(QLatin1String("closed"))
        || lower.contains(QLatin1String("disconnect"))) {
        color = QStringLiteral("#f44336");
    } else if (lower.contains(QLatin1String("connected")) || lower.contains(QLatin1String("listen"))
               || lower.contains(QLatin1String("udp"))) {
        color = QStringLiteral("#4CAF50");
    }
    setStatus(text, color);
}

void NetAssistWidget::slotClientCount(int count)
{
    showClientText(QStringLiteral("Clients: %1"), count);
}

QString NetAssistWidget::dataToText(const QByteArray &data)
{
    if (m_checkHexRecv->isChecked())
        return ProtocolUtils::bytesToHexString(data);
    return QString::fromUtf8(data);
}

void NetAssistWidget::updateCountLabel()
{
    m_labelRecvCount->setText(tr("R: %1 B").arg(m_recvBytes));
    m_labelSendCount->setText(tr("S: %1 B").arg(m_sendBytes));
}

void NetAssistWidget::setStatus(const QString &english, const QString &color)
{
    m_statusEnglish = english;
    m_statusColor = color;
    m_labelStatus->setText(QStringLiteral("● %1").arg(phrase(english)));
    m_labelStatus->setStyleSheet(
        QStringLiteral("QLabel { color: %1; font-weight: bold; }").arg(color));
}

void NetAssistWidget::showClientText(const QString &english, int number)
{
    m_clientEnglish = english;
    m_clientNumber = number;
    const QString text = phrase(english);
    m_labelClientCount->setText(number < 0 ? text : text.arg(number));
}

void NetAssistWidget::setConnectLatched(bool latched)
{
    m_showDisconnect = latched;
    m_btnConnect->setText(latched ? tr("Disconnect") : tr("Connect"));
    applyConnectButtonStyle(latched);
}

QString NetAssistWidget::phrase(const QString &english) const
{
    if (english == QLatin1String("Closed")) return tr("Closed");
    if (english == QLatin1String("Starting...")) return tr("Starting...");
    if (english == QLatin1String("Not Connected")) return tr("Not Connected");
    if (english == QLatin1String("Clients: %1")) return tr("Clients: %1");
    if (english == QLatin1String("UDP")) return tr("UDP");
    if (english == QLatin1String("Invalid address")) return tr("Invalid address");
    if (english == QLatin1String("Connecting...")) return tr("Connecting...");
    if (english == QLatin1String("Disconnected")) return tr("Disconnected");
    if (english == QLatin1String("Log saved")) return tr("Log saved");
    if (english == QLatin1String("Select a valid remote address")) return tr("Select a valid remote address");
    if (english == QLatin1String("Connected")) return tr("Connected");
    if (english == QLatin1String("Reconnecting...")) return tr("Reconnecting...");
    if (english == QLatin1String("UDP Listening")) return tr("UDP Listening");
    if (english == QLatin1String("TCP Server Listening")) return tr("TCP Server Listening");
    if (english == QLatin1String("Listen failed, reconnecting...")) return tr("Listen failed, reconnecting...");
    if (english == QLatin1String("Bind failed, reconnecting...")) return tr("Bind failed, reconnecting...");
    if (english == QLatin1String("Connection timed out")) return tr("Connection timed out");
    if (english.startsWith(QLatin1String("Cannot resolve host ")))
        return tr("Cannot resolve host %1").arg(english.mid(int(QLatin1String("Cannot resolve host ").size())));
    if (english.startsWith(QLatin1String("Client not found ")))
        return tr("Client not found %1:%2").arg(english.section(QLatin1Char(' '), 3, 3).section(QLatin1Char(':'), 0, 0),
                                                english.section(QLatin1Char(':'), 1, 1));
    if (english.startsWith(QLatin1String("Cannot open auto-save file: ")))
        return tr("Cannot open auto-save file: %1").arg(english.mid(int(QLatin1String("Cannot open auto-save file: ").size())));
    return english;
}

void NetAssistWidget::applyOpenButtonStyle(bool opened)
{
    m_btnOpen->setText(opened ? tr("Close") : tr("Open"));
    m_btnOpen->setStyleSheet(QString::fromLatin1(opened ? kBtnRed : kBtnGreen));
}

void NetAssistWidget::applyConnectButtonStyle(bool connected)
{
    m_btnConnect->setStyleSheet(QString::fromLatin1(connected ? kBtnRed : kBtnOrange));
}

QString NetAssistWidget::timestampPrefix() const
{
    if (!m_showTimestamp)
        return QString();
    return QStringLiteral("[%1] ").arg(ProtocolUtils::timestampMs());
}

void NetAssistWidget::appendLog(const QString &text, const QColor &color)
{
    Q_UNUSED(color)
    QString line = timestampPrefix() + text + QLatin1Char('\n');
    const QByteArray bytes = line.toUtf8();
    appendAutoSave(bytes);
    m_editRecv->addData(bytes);
    if (m_scrollToBottom->isChecked())
        m_editRecv->scrollToBottom();
}

void NetAssistWidget::appendTrafficLog(bool isSend, const QByteArray &data,
                                       const QString &peer, quint16 port)
{
    if (isSend && !m_showSend)
        return;

    QString line = timestampPrefix();
    const bool hasAddr = m_showRecvAddr && !peer.isEmpty();
    if (m_showDirection)
        line += isSend ? QStringLiteral("[TX]") : QStringLiteral("[RX]");
    if (hasAddr)
        line += QStringLiteral("[%1:%2]").arg(peer).arg(port);
    if (m_showDirection || hasAddr)
        line += QLatin1Char(' ');
    line += dataToText(data);

    const bool ownLine = isSend || m_showTimestamp || m_showDirection || hasAddr;
    if (ownLine)
        line += QStringLiteral("\r\n");
    const QByteArray bytes = line.toUtf8();
    appendAutoSave(bytes);
    m_editRecv->addData(bytes);
    if (m_scrollToBottom->isChecked())
        m_editRecv->scrollToBottom();
}

void NetAssistWidget::initNetWork()
{
    m_netWorker = new NetworkWorker;
    m_netWorker->moveToThread(&workerThread);

    // UI -> 网络线程
    QObject::connect(this, &NetAssistWidget::sigOpenNetwork, m_netWorker, &NetworkWorker::slotOpenNetwork);
    QObject::connect(this, &NetAssistWidget::sigCloseNetwork, m_netWorker, &NetworkWorker::slotCloseNetwork);
    QObject::connect(this, &NetAssistWidget::sigTcpConnect, m_netWorker, &NetworkWorker::slotTcpConnect);
    QObject::connect(this, &NetAssistWidget::sigTcpDisconnect, m_netWorker, &NetworkWorker::slotTcpDisconnect);
    QObject::connect(this, &NetAssistWidget::sigSendData, m_netWorker, &NetworkWorker::slotSendData);

    // 网络线程 -> UI（统一 IoPacket 出口，其它控件也可 connect ioSource()）
    QObject::connect(m_netWorker, &IoSource::ioDataReceived, this, &NetAssistWidget::onIoData);
    QObject::connect(m_netWorker, &NetworkWorker::sigClientConnected, this, &NetAssistWidget::slotClientConnected);
    QObject::connect(m_netWorker, &NetworkWorker::sigClientDisconnected, this, &NetAssistWidget::slotClientDisconnected);
    QObject::connect(m_netWorker, &NetworkWorker::sigTcpConnected, this, &NetAssistWidget::slotTcpConnected);
    QObject::connect(m_netWorker, &NetworkWorker::sigTcpDisconnected, this, &NetAssistWidget::slotTcpDisconnected);
    QObject::connect(m_netWorker, &NetworkWorker::sigError, this, &NetAssistWidget::slotError);
    QObject::connect(m_netWorker, &NetworkWorker::sigStateText, this, &NetAssistWidget::slotStateText);
    QObject::connect(m_netWorker, &NetworkWorker::sigClientCount, this, &NetAssistWidget::slotClientCount);
    QObject::connect(m_netWorker, &NetworkWorker::sigClientConnected, this, &NetAssistWidget::onConnectAddrChange);

    workerThread.start();
}

bool NetAssistWidget::hasHostInCombo(const RemoteHost &host)
{
    if(!m_cmbRemoteAddr)return true;
    for (int i = 0; i < m_cmbRemoteAddr->count(); ++i) {
        RemoteHost item = m_cmbRemoteAddr->itemData(i).value<RemoteHost>();
        if (item == host) { // 直接复用你写的operator==，仅对比地址+端口
            return true;
        }
    }
    return false;
}

bool NetAssistWidget::addHostAddr(const RemoteHost &host)
{
    if (hasHostInCombo(host))
        return false;

    QString displayText;
    if (host.address.protocol() == QAbstractSocket::IPv6Protocol) {
        displayText = QStringLiteral("[%1]:%2").arg(host.address.toString()).arg(host.port);
    } else {
        displayText = QStringLiteral("%1:%2").arg(host.endpoint()).arg(host.port);
    }

    m_cmbRemoteAddr->addItem(displayText, QVariant::fromValue(host));
    return true;
}

bool NetAssistWidget::commitCurrentHost()
{
    const QString input = m_cmbRemoteAddr->currentText().trimmed();
    RemoteHost host;
    if (!parseRemoteEndpoint(input, &host))
        return false;
    host.lastSeen = QDateTime::currentMSecsSinceEpoch();

    addHostAddr(host);
    for (int i = 0; i < m_cmbRemoteAddr->count(); ++i) {
        if (m_cmbRemoteAddr->itemData(i).value<RemoteHost>() == host) {
            m_cmbRemoteAddr->setCurrentIndex(i);
            if (m_cmbRemoteAddr->lineEdit())
                m_cmbRemoteAddr->lineEdit()->home(false);  // 窄下拉框停在开头，避免只看见域名尾巴
            break;
        }
    }
    return true;
}

bool NetAssistWidget::currentRemote(QString *ip, quint16 *port)
{
    if (!commitCurrentHost())
        return false;
    const RemoteHost h = m_cmbRemoteAddr->currentData().value<RemoteHost>();
    const QString target = h.endpoint();
    if (target.isEmpty() || h.port == 0)
        return false;
    if (ip)
        *ip = target;
    if (port)
        *port = h.port;
    return true;
}

void NetAssistWidget::loadSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("Network"));
    const int proto = settings.value(QStringLiteral("protocol"), 0).toInt();
    if (proto >= 0 && proto < m_cmbProtocol->count())
        m_cmbProtocol->setCurrentIndex(proto);

    const QString localIp = settings.value(QStringLiteral("localIp")).toString().section(QLatin1Char(' '), 0, 0);
    if (!localIp.isEmpty()) {
        int idx = m_cmbLocalIp->findText(localIp, Qt::MatchStartsWith);
        if (idx >= 0)
            m_cmbLocalIp->setCurrentIndex(idx);
    }
    m_spinLocalPort->setValue(settings.value(QStringLiteral("localPort"), 13601).toInt());
    m_checkHexRecv->setChecked(settings.value(QStringLiteral("hexRecv"), false).toBool());
    m_checkHexSend->setChecked(settings.value(QStringLiteral("hexSend"), false).toBool());
    m_checkTimestamp->setChecked(settings.value(QStringLiteral("timestamp"), false).toBool());
    m_checkShowRecvAddr->setChecked(settings.value(QStringLiteral("showAddr"), false).toBool());
    m_checkShowSend->setChecked(settings.value(QStringLiteral("showSend"), true).toBool());
    m_checkShowDirection->setChecked(settings.value(QStringLiteral("showDirection"), false).toBool());
    m_scrollToBottom->setChecked(settings.value(QStringLiteral("scrollBottom"), true).toBool());
    m_checkAddModbusCrc16->setChecked(settings.value(QStringLiteral("crc16"), false).toBool());
    m_checkAppendCRLF->setChecked(settings.value(QStringLiteral("crlf"), false).toBool());
    m_autoSaveCheck->setChecked(settings.value(QStringLiteral("autoSave"), false).toBool());
    settings.endGroup();
    updateProtocolDependentUi();
}

void NetAssistWidget::saveSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("Network"));
    settings.setValue(QStringLiteral("protocol"), m_cmbProtocol->currentIndex());
    settings.setValue(QStringLiteral("localIp"), m_cmbLocalIp->currentText());
    settings.setValue(QStringLiteral("localPort"), m_spinLocalPort->value());
    settings.setValue(QStringLiteral("hexRecv"), m_checkHexRecv->isChecked());
    settings.setValue(QStringLiteral("hexSend"), m_checkHexSend->isChecked());
    settings.setValue(QStringLiteral("timestamp"), m_checkTimestamp->isChecked());
    settings.setValue(QStringLiteral("showAddr"), m_checkShowRecvAddr->isChecked());
    settings.setValue(QStringLiteral("showSend"), m_checkShowSend->isChecked());
    settings.setValue(QStringLiteral("showDirection"), m_checkShowDirection->isChecked());
    settings.setValue(QStringLiteral("scrollBottom"), m_scrollToBottom->isChecked());
    settings.setValue(QStringLiteral("crc16"), m_checkAddModbusCrc16->isChecked());
    settings.setValue(QStringLiteral("crlf"), m_checkAppendCRLF->isChecked());
    settings.setValue(QStringLiteral("autoSave"), m_autoSaveCheck->isChecked());
    settings.endGroup();
}

void NetAssistWidget::setAutoSaveEnabled(bool enabled)
{
    if (!enabled) {
        delete m_autoSaveFile;
        m_autoSaveFile = nullptr;
        return;
    }
    openAutoSave();
}

bool NetAssistWidget::openAutoSave()
{
    const QString path = QFileInfo(SettingsWidget::logFilePath(QStringLiteral("net_auto.txt"))).absoluteFilePath();
    if (m_autoSaveFile && m_autoSaveFile->isOpen() && m_autoSaveFile->fileName() == path)
        return true;
    delete m_autoSaveFile;
    m_autoSaveFile = new QFile(path, this);
    if (m_autoSaveFile->open(QIODevice::WriteOnly | QIODevice::Append))
        return true;
    delete m_autoSaveFile;
    m_autoSaveFile = nullptr;
    // 失败时取消勾选，避免每个包都再试一次并刷状态栏。
    m_autoSaveCheck->blockSignals(true);
    m_autoSaveCheck->setChecked(false);
    m_autoSaveCheck->blockSignals(false);
    setStatus(QStringLiteral("Cannot open auto-save file: %1").arg(path), QStringLiteral("#f44336"));
    return false;
}

void NetAssistWidget::appendAutoSave(const QByteArray &bytes)
{
    if (!m_autoSaveCheck->isChecked() || bytes.isEmpty())
        return;
    if (!openAutoSave())
        return;
    m_autoSaveFile->write(bytes);
    m_autoSaveFile->flush();  // 立刻落盘，方便一边收一边用别的程序打开看
}

