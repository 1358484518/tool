#include "NetAssistWidget.h"
#include "FastTextView.h"
#include "NetworkWorker.h"
#include "common/ProtocolUtils.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
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
    setWindowTitle(QStringLiteral("网络调试助手"));
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
        if (workerThread.isRunning()) {
            QMetaObject::invokeMethod(m_netWorker, "slotCloseNetwork", Qt::BlockingQueuedConnection);
            workerThread.quit();
            workerThread.wait(5000);
        }
        if (!workerThread.isRunning()) {
            m_netWorker->moveToThread(QThread::currentThread());
            m_netWorker->setParent(this);
        }
        m_netWorker = nullptr;
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
    m_editSend->setPlaceholderText(QStringLiteral("输入要发送的数据... (Ctrl+Enter 发送)"));
    m_editSend->setStyleSheet(QString::fromLatin1(kEditBorder));
    sendLayout->addWidget(m_editSend, 1);

    auto *sendBtnCol = new QVBoxLayout();
    sendBtnCol->setSpacing(4);
    m_btnSend = new QPushButton(QStringLiteral("发送"), sendPanel);
    m_btnSend->setMinimumWidth(70);
    m_btnSend->setEnabled(false);
    m_btnSend->setStyleSheet(QString::fromLatin1(kBtnBlue));
    sendBtnCol->addWidget(m_btnSend);
    m_btnClearSend = new QPushButton(QStringLiteral("清空"), sendPanel);
    m_btnClearSend->setMinimumWidth(70);
    sendBtnCol->addWidget(m_btnClearSend);
    sendBtnCol->addStretch();
    sendLayout->addLayout(sendBtnCol);
    leftLayout->addWidget(sendPanel, 1);
    mainLayout->addWidget(leftPanel, 5);

    // ========== 右侧设置栏 ==========
    auto *rightPanel = new QWidget(this);
    rightPanel->setMaximumWidth(220);
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setSpacing(6);
    rightLayout->setContentsMargins(4, 0, 0, 0);

    m_groupConnection = new QGroupBox(QStringLiteral("连接设置"), rightPanel);
    auto *connGrid = new QGridLayout(m_groupConnection);
    connGrid->setSpacing(4);
    connGrid->setContentsMargins(6, 14, 6, 6);
    int r = 0;

    connGrid->addWidget(new QLabel(QStringLiteral("协议:"), m_groupConnection), r, 0);
    m_cmbProtocol = new QComboBox(m_groupConnection);
    m_cmbProtocol->addItem(QStringLiteral("TCP 服务端"), QVariant::fromValue(NetProtocol::TcpServer));
    m_cmbProtocol->addItem(QStringLiteral("TCP 客户端"), QVariant::fromValue(NetProtocol::TcpClient));
    m_cmbProtocol->addItem(QStringLiteral("UDP"), QVariant::fromValue(NetProtocol::Udp));
    connGrid->addWidget(m_cmbProtocol, r, 1);
    r++;

    connGrid->addWidget(new QLabel(QStringLiteral("地址:"), m_groupConnection), r, 0);
    m_cmbLocalIp = new QComboBox(m_groupConnection);
    m_cmbLocalIp->addItem(QStringLiteral("0.0.0.0 (所有网卡)"));
    for (const QHostAddress &addr : QNetworkInterface::allAddresses()) {
        if (addr.protocol() == QAbstractSocket::IPv4Protocol && addr != QHostAddress::LocalHost)
            m_cmbLocalIp->addItem(addr.toString());
    }
    m_cmbLocalIp->addItem(QStringLiteral("127.0.0.1 (本机回环)"));
    connGrid->addWidget(m_cmbLocalIp, r, 1);
    r++;

    connGrid->addWidget(new QLabel(QStringLiteral("端口:"), m_groupConnection), r, 0);
    m_spinLocalPort = new QSpinBox(m_groupConnection);
    m_spinLocalPort->setRange(1, 65535);
    m_spinLocalPort->setValue(13601);
    connGrid->addWidget(m_spinLocalPort, r, 1);
    r++;

    connGrid->addWidget(new QLabel(QStringLiteral("远程:"), m_groupConnection), r, 0);
    m_cmbRemoteAddr = new QComboBox(m_groupConnection);
    m_cmbRemoteAddr->setEditable(true);
    m_cmbRemoteAddr->setToolTip(QStringLiteral("支持 127.0.0.1:80、www.example.com:443、https://www.example.com"));
    RemoteHost addr;
    addr.port = 13601;
    addr.address = QHostAddress(QStringLiteral("127.0.0.1"));
    addr.host = QStringLiteral("127.0.0.1");
    addHostAddr(addr);
    if (m_cmbRemoteAddr->lineEdit()) {
        m_cmbRemoteAddr->lineEdit()->setPlaceholderText(
            QStringLiteral("IP/网址:端口"));
    }
    connGrid->addWidget(m_cmbRemoteAddr, r, 1);
    rightLayout->addWidget(m_groupConnection);

    m_btnOpen = new QPushButton(QStringLiteral("打开"), rightPanel);
    m_btnOpen->setCheckable(true);
    m_btnOpen->setMinimumHeight(28);
    applyOpenButtonStyle(false);
    rightLayout->addWidget(m_btnOpen);

    m_btnConnect = new QPushButton(QStringLiteral("连接"), rightPanel);
    m_btnConnect->setEnabled(false);
    m_btnConnect->setMinimumHeight(28);
    applyConnectButtonStyle(false);
    rightLayout->addWidget(m_btnConnect);

    rightLayout->addWidget(makeHLine(rightPanel));

    m_groupSendSetting = new QGroupBox(QStringLiteral("发送设置"), rightPanel);
    auto *sendGrid = new QGridLayout(m_groupSendSetting);
    sendGrid->setSpacing(4);
    sendGrid->setContentsMargins(6, 14, 6, 6);
    r = 0;
    m_checkAutoSend = new QCheckBox(QStringLiteral("定时发送"), m_groupSendSetting);
    sendGrid->addWidget(m_checkAutoSend, r, 0, 1, 2);
    r++;

    auto *intervalRow = new QHBoxLayout();
    intervalRow->setSpacing(4);
    intervalRow->addWidget(new QLabel(QStringLiteral("间隔:"), m_groupSendSetting));
    m_spinAutoSendInterval = new QSpinBox(m_groupSendSetting);
    m_spinAutoSendInterval->setRange(100, 60000);
    m_spinAutoSendInterval->setValue(1000);
    m_spinAutoSendInterval->setSuffix(QStringLiteral("ms"));
    m_spinAutoSendInterval->setEnabled(false);
    intervalRow->addWidget(m_spinAutoSendInterval, 1);
    sendGrid->addLayout(intervalRow, r, 0, 1, 2);
    r++;

    m_checkHexSend = new QCheckBox(QStringLiteral("十六进制"), m_groupSendSetting);
    sendGrid->addWidget(m_checkHexSend, r, 0);
    m_checkAddModbusCrc16 = new QCheckBox(QStringLiteral("CRC16"), m_groupSendSetting);
    sendGrid->addWidget(m_checkAddModbusCrc16, r, 1);
    r++;

    m_checkAppendCRLF = new QCheckBox(QStringLiteral("加回车换行"), m_groupSendSetting);
    sendGrid->addWidget(m_checkAppendCRLF, r, 0);
    m_checkBroadcastSend = new QCheckBox(QStringLiteral("广播"), m_groupSendSetting);
    sendGrid->addWidget(m_checkBroadcastSend, r, 1);
    rightLayout->addWidget(m_groupSendSetting);

    rightLayout->addWidget(makeHLine(rightPanel));

    m_groupRecvSetting = new QGroupBox(QStringLiteral("接收设置"), rightPanel);
    auto *recvGrid = new QGridLayout(m_groupRecvSetting);
    recvGrid->setSpacing(4);
    recvGrid->setContentsMargins(6, 14, 6, 6);
    r = 0;

    auto *recvBtnRow = new QHBoxLayout();
    recvBtnRow->setSpacing(4);
    m_btnSaveLog = new QPushButton(QStringLiteral("保存日志"), m_groupRecvSetting);
    recvBtnRow->addWidget(m_btnSaveLog);
    m_btnClearRecv = new QPushButton(QStringLiteral("清空接收"), m_groupRecvSetting);
    recvBtnRow->addWidget(m_btnClearRecv);
    recvGrid->addLayout(recvBtnRow, r, 0, 1, 2);
    r++;

    m_checkHexRecv = new QCheckBox(QStringLiteral("十六进制"), m_groupRecvSetting);
    recvGrid->addWidget(m_checkHexRecv, r, 0);
    m_checkRecvTimestamp = new QCheckBox(QStringLiteral("时间戳"), m_groupRecvSetting);
    recvGrid->addWidget(m_checkRecvTimestamp, r, 1);
    r++;

    m_scrollToBottom = new QCheckBox(QStringLiteral("自动滚屏"), m_groupRecvSetting);
    m_scrollToBottom->setChecked(true);
    recvGrid->addWidget(m_scrollToBottom, r, 0);
    m_checkShowRecvAddr = new QCheckBox(QStringLiteral("显示地址"), m_groupRecvSetting);
    recvGrid->addWidget(m_checkShowRecvAddr, r, 1);
    rightLayout->addWidget(m_groupRecvSetting);

    m_btnResetCount = new QPushButton(QStringLiteral("计数清零"), rightPanel);
    rightLayout->addWidget(m_btnResetCount);

    rightLayout->addWidget(makeHLine(rightPanel));

    auto *countRow = new QHBoxLayout();
    countRow->setSpacing(10);
    m_labelRecvCount = new QLabel(QStringLiteral("R: 0 B"), rightPanel);
    countRow->addWidget(m_labelRecvCount);
    m_labelSendCount = new QLabel(QStringLiteral("S: 0 B"), rightPanel);
    countRow->addWidget(m_labelSendCount);
    countRow->addStretch();
    rightLayout->addLayout(countRow);

    m_labelStatus = new QLabel(rightPanel);
    setStatus(QStringLiteral("未打开"), QStringLiteral("#f44336"));
    rightLayout->addWidget(m_labelStatus);

    m_labelClientCount = new QLabel(QStringLiteral("连接数: 0"), rightPanel);
    m_labelClientCount->setWordWrap(true);
    rightLayout->addWidget(m_labelClientCount);

    rightLayout->addStretch();
    mainLayout->addWidget(rightPanel);

    m_timerAutoSend = new QTimer(this);
}



void NetAssistWidget::initConnect()
{
    connect(m_cmbProtocol, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &NetAssistWidget::onProtocolChanged);
    connect(m_btnOpen, &QPushButton::toggled, this, &NetAssistWidget::onOpenToggled);
    connect(m_btnConnect, &QPushButton::clicked, this, &NetAssistWidget::onConnectClicked);
    connect(m_btnClearRecv, &QPushButton::clicked, this, &NetAssistWidget::onClearRecv);
    connect(m_btnSaveLog, &QPushButton::clicked, this, &NetAssistWidget::onSaveLog);
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
    connect(m_checkRecvTimestamp, &QCheckBox::toggled, this, [this](bool checked){
        m_recvTimestamp = checked;
    });
    connect(m_checkShowRecvAddr, &QCheckBox::toggled, this, [this](bool checked){
        m_showRecvAddr = checked;
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
    m_checkRecvTimestamp->setChecked(false);
    m_checkShowRecvAddr->setChecked(false);
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
        m_btnOpen->setText(QStringLiteral("关闭"));
        m_cmbProtocol->setEnabled(false);
        m_cmbLocalIp->setEnabled(false);
        m_spinLocalPort->setEnabled(false);
        m_btnSend->setEnabled(true);
        setStatus(QStringLiteral("启动中..."), QStringLiteral("#FF9800"));
        applyOpenButtonStyle(true);
        updateCountLabel();

        const NetProtocol proto = m_cmbProtocol->currentData().value<NetProtocol>();
        const QString localIp = m_cmbLocalIp->currentText().split(QLatin1Char(' ')).first();
        const quint16 localPort = static_cast<quint16>(m_spinLocalPort->value());
        emit sigOpenNetwork(proto, localIp, localPort);

        if (proto == NetProtocol::TcpClient) {
            m_labelClientCount->setText(QStringLiteral("未连接"));
        } else if (proto == NetProtocol::TcpServer) {
            m_labelClientCount->setText(QStringLiteral("连接数: 0"));
        } else {
            m_labelClientCount->setText(QStringLiteral("UDP模式"));
        }
        updateProtocolDependentUi();
    } else {
        m_btnOpen->setText(QStringLiteral("打开"));
        m_cmbProtocol->setEnabled(true);
        m_cmbLocalIp->setEnabled(true);
        m_spinLocalPort->setEnabled(true);
        m_btnConnect->setEnabled(false);
        m_btnConnect->setText(QStringLiteral("连接"));
        applyConnectButtonStyle(false);
        m_btnSend->setEnabled(false);
        m_checkAutoSend->setChecked(false);
        setStatus(QStringLiteral("未打开"), QStringLiteral("#f44336"));
        applyOpenButtonStyle(false);
        m_labelClientCount->setText(QStringLiteral("连接数: 0"));
        m_manualTcpDisconnect = true;
        emit sigCloseNetwork();
        updateProtocolDependentUi();
    }
}

void NetAssistWidget::onConnectClicked()
{
    if (m_btnConnect->text() == QStringLiteral("连接")) {
        QString remoteIp;
        quint16 remotePort = 0;
        if (!currentRemote(&remoteIp, &remotePort)) {
            setStatus(QStringLiteral("远程地址无效"), QStringLiteral("#f44336"));
            return;
        }
        m_manualTcpDisconnect = false;
        emit sigTcpConnect(remoteIp, remotePort);
        m_btnConnect->setText(QStringLiteral("断开"));
        applyConnectButtonStyle(true);
        setStatus(QStringLiteral("连接中..."), QStringLiteral("#FF9800"));
    } else {
        m_manualTcpDisconnect = true;
        emit sigTcpDisconnect();
        m_btnConnect->setText(QStringLiteral("连接"));
        applyConnectButtonStyle(false);
        setStatus(QStringLiteral("已断开"), QStringLiteral("#f44336"));
        m_labelClientCount->setText(QStringLiteral("未连接"));
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
    QString path = QFileDialog::getSaveFileName(this, "保存日志", "netlog.txt", "文本文件(*.txt)");
    if (!path.isEmpty()) {
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(m_editRecv->getData());
            file.close();
            const QString oldStatus = m_labelStatus->text();
            const QString oldStyle = m_labelStatus->styleSheet();
            setStatus(QStringLiteral("日志已保存"), QStringLiteral("#4CAF50"));
            QTimer::singleShot(2000, this, [this, oldStatus, oldStyle]() {
                if (m_labelStatus->text().contains(QStringLiteral("日志已保存"))) {
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
            setStatus(QStringLiteral("请选择有效的远程地址"), QStringLiteral("#f44336"));
            return;
        }
    }

    emit sigSendData(data, remoteIp, remotePort);
    m_sendBytes += static_cast<quint64>(data.size());
    updateCountLabel();
    appendLog(QStringLiteral("[TX] %1").arg(dataToText(data)), Qt::blue);
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
    QString line=QString();
//    QString time = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
//    QString line = QString("[%1] %2\n").arg(time).arg(dataToText(data));

//    QString prefix;
//    if (!fromIp.isEmpty()) {
//        prefix = QString("[RX][%1:%2] ").arg(fromIp).arg(fromPort);
//    } else {
//        prefix = "[RX] ";
//    }
//    appendLog(prefix + dataToText(data), Qt::black);
    if(m_recvTimestamp)line+=QString("[%1] ").arg(QDateTime::currentDateTime().toString("hh:mm:ss.zzz"));
    if(m_showRecvAddr)line+=QString("[RX][%1:%2] ").arg(fromIp).arg(fromPort);
    line += dataToText(data);
    if(m_recvTimestamp||m_showRecvAddr)line+="\r\n";
    m_editRecv->addData(line.toUtf8());
    if(m_scrollToBottom->isChecked())m_editRecv->scrollToBottom();
}

void NetAssistWidget::slotClientConnected(QString ip, quint16 port)
{
    appendLog(QString("[新连接] %1:%2").arg(ip).arg(port), Qt::darkGreen);
}

void NetAssistWidget::slotClientDisconnected(QString ip, quint16 port)
{
    appendLog(QString("[断开连接] %1:%2").arg(ip).arg(port), Qt::darkGray);
}

void NetAssistWidget::slotTcpConnected()
{
    appendLog(QStringLiteral("TCP连接成功"), Qt::darkGreen);
    setStatus(QStringLiteral("已连接"), QStringLiteral("#4CAF50"));
    applyConnectButtonStyle(true);
    m_labelClientCount->setText(QStringLiteral("已连接"));
}

void NetAssistWidget::slotTcpDisconnected()
{
    appendLog(QStringLiteral("TCP连接断开"), Qt::darkGray);
    if (m_manualTcpDisconnect) {
        setStatus(QStringLiteral("已断开"), QStringLiteral("#f44336"));
        m_btnConnect->setText(QStringLiteral("连接"));
        applyConnectButtonStyle(false);
    } else {
        setStatus(QStringLiteral("重连中..."), QStringLiteral("#FF9800"));
        m_btnConnect->setText(QStringLiteral("断开"));
        applyConnectButtonStyle(true);
    }
    m_labelClientCount->setText(QStringLiteral("未连接"));
}

void NetAssistWidget::slotError(QString errStr)
{
    appendLog("[错误] " + errStr, Qt::red);
    setStatus(errStr, QStringLiteral("#f44336"));
}

void NetAssistWidget::slotStateText(QString text)
{
    QString color = QStringLiteral("#FF9800");
    if (text.contains(QStringLiteral("失败")) || text.contains(QStringLiteral("错误"))
        || text.contains(QStringLiteral("未打开")) || text.contains(QStringLiteral("已断开"))
        || text.contains(QStringLiteral("已关闭")) || text.contains(QStringLiteral("无效"))) {
        color = QStringLiteral("#f44336");
    } else if (text.contains(QStringLiteral("已连接")) || text.contains(QStringLiteral("监听"))
               || text.contains(QStringLiteral("UDP"))) {
        color = QStringLiteral("#4CAF50");
    }
    setStatus(text, color);
}

void NetAssistWidget::slotClientCount(int count)
{
    m_labelClientCount->setText(QString("连接数: %1").arg(count));
}

QString NetAssistWidget::dataToText(const QByteArray &data)
{
    if (m_checkHexRecv->isChecked())
        return ProtocolUtils::bytesToHexString(data);
    return QString::fromUtf8(data);
}

void NetAssistWidget::updateCountLabel()
{
    m_labelRecvCount->setText(QStringLiteral("R: %1 B").arg(m_recvBytes));
    m_labelSendCount->setText(QStringLiteral("S: %1 B").arg(m_sendBytes));
}

void NetAssistWidget::setStatus(const QString &text, const QString &color)
{
    m_labelStatus->setText(QStringLiteral("● %1").arg(text));
    m_labelStatus->setStyleSheet(
        QStringLiteral("QLabel { color: %1; font-weight: bold; }").arg(color));
}

void NetAssistWidget::applyOpenButtonStyle(bool opened)
{
    m_btnOpen->setText(opened ? QStringLiteral("关闭") : QStringLiteral("打开"));
    m_btnOpen->setStyleSheet(QString::fromLatin1(opened ? kBtnRed : kBtnGreen));
}

void NetAssistWidget::applyConnectButtonStyle(bool connected)
{
    m_btnConnect->setStyleSheet(QString::fromLatin1(connected ? kBtnRed : kBtnOrange));
}

void NetAssistWidget::appendLog(const QString &text, const QColor &color)
{
    Q_UNUSED(color) // 颜色参数暂时不用
    QString time = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
    QString line = QString("[%1] %2\n").arg(time).arg(text);
    m_editRecv->addData(line.toUtf8());
    if(m_scrollToBottom->isChecked())m_editRecv->scrollToBottom(); // 需要自动跟随就加这句
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

    const QString localIp = settings.value(QStringLiteral("localIp")).toString();
    if (!localIp.isEmpty()) {
        int idx = m_cmbLocalIp->findText(localIp, Qt::MatchStartsWith);
        if (idx >= 0)
            m_cmbLocalIp->setCurrentIndex(idx);
    }
    m_spinLocalPort->setValue(settings.value(QStringLiteral("localPort"), 13601).toInt());
    m_checkHexRecv->setChecked(settings.value(QStringLiteral("hexRecv"), false).toBool());
    m_checkHexSend->setChecked(settings.value(QStringLiteral("hexSend"), false).toBool());
    m_checkRecvTimestamp->setChecked(settings.value(QStringLiteral("timestamp"), false).toBool());
    m_checkShowRecvAddr->setChecked(settings.value(QStringLiteral("showAddr"), false).toBool());
    m_scrollToBottom->setChecked(settings.value(QStringLiteral("scrollBottom"), true).toBool());
    m_checkAddModbusCrc16->setChecked(settings.value(QStringLiteral("crc16"), false).toBool());
    m_checkAppendCRLF->setChecked(settings.value(QStringLiteral("crlf"), false).toBool());
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
    settings.setValue(QStringLiteral("timestamp"), m_checkRecvTimestamp->isChecked());
    settings.setValue(QStringLiteral("showAddr"), m_checkShowRecvAddr->isChecked());
    settings.setValue(QStringLiteral("scrollBottom"), m_scrollToBottom->isChecked());
    settings.setValue(QStringLiteral("crc16"), m_checkAddModbusCrc16->isChecked());
    settings.setValue(QStringLiteral("crlf"), m_checkAppendCRLF->isChecked());
    settings.endGroup();
}

