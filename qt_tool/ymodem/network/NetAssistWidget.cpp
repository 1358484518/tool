#include "NetAssistWidget.h"
#include "FastTextView.h"
#include "NetworkWorker.h"
#include "common/ProtocolUtils.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
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

NetAssistWidget::NetAssistWidget(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle("网络调试助手");
    resize(850, 600);
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
        disconnect(m_netWorker, nullptr, this, nullptr);
        disconnect(this, nullptr, m_netWorker, nullptr);
    }
    if (m_netWorker && workerThread.isRunning()) {
        QMetaObject::invokeMethod(m_netWorker, "slotCloseNetwork", Qt::BlockingQueuedConnection);
        workerThread.quit();
        workerThread.wait(3000);
    }
    if (m_netWorker) {
        m_netWorker->moveToThread(QThread::currentThread());
        delete m_netWorker;
        m_netWorker = nullptr;
    }
}

void NetAssistWidget::initUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(4);
    mainLayout->setContentsMargins(6, 6, 6, 6);

    QHBoxLayout *contentLayout = new QHBoxLayout();
    contentLayout->setSpacing(6);

    // 左侧日志区
    QVBoxLayout *logLayout = new QVBoxLayout();
    logLayout->setSpacing(4);

    QGroupBox *groupRecvLog = new QGroupBox("接收区");
    QVBoxLayout *recvLogLayout = new QVBoxLayout(groupRecvLog);
    recvLogLayout->setContentsMargins(6, 10, 6, 6);
    m_editRecv = new FastTextView;
//    m_editRecv->setReadOnly(true);
//    m_editRecv->setFont(QFont("Consolas", 10));
    recvLogLayout->addWidget(m_editRecv);
    logLayout->addWidget(groupRecvLog, 1);

    QGroupBox *groupSendLog = new QGroupBox("发送区");
    QVBoxLayout *sendLogLayout = new QVBoxLayout(groupSendLog);
    sendLogLayout->setContentsMargins(6, 10, 6, 6);
    m_editSend = new QPlainTextEdit();
    m_editSend->setFixedHeight(90);
    m_editSend->setFont(QFont("Consolas", 10));
    m_editSend->setPlaceholderText(QStringLiteral("输入要发送的数据... (Ctrl+Enter 发送)"));
    sendLogLayout->addWidget(m_editSend);
    logLayout->addWidget(groupSendLog);

    contentLayout->addLayout(logLayout, 7);

    // 右侧设置区
    QVBoxLayout *settingLayout = new QVBoxLayout();
    settingLayout->setSpacing(4);
    const int settingWidth = 220;

    // 连接设置
    m_groupConnection = new QGroupBox("连接设置");
    m_groupConnection->setFixedWidth(settingWidth);
    QVBoxLayout *connLayout = new QVBoxLayout(m_groupConnection);
    connLayout->setSpacing(3);
    connLayout->setContentsMargins(8, 10, 8, 8);

    connLayout->addWidget(new QLabel("协议类型:"));
    m_cmbProtocol = new QComboBox();
    m_cmbProtocol->setFixedHeight(22);
    m_cmbProtocol->addItem("TCP 服务端", QVariant::fromValue(NetProtocol::TcpServer));
    m_cmbProtocol->addItem("TCP 客户端", QVariant::fromValue(NetProtocol::TcpClient));
    m_cmbProtocol->addItem("UDP", QVariant::fromValue(NetProtocol::Udp));
    connLayout->addWidget(m_cmbProtocol);

    connLayout->addWidget(new QLabel("本地地址:"));
    m_cmbLocalIp = new QComboBox();
    m_cmbLocalIp->setFixedHeight(22);
    m_cmbLocalIp->addItem("0.0.0.0 (所有网卡)");
    for (const QHostAddress &addr : QNetworkInterface::allAddresses()) {
        if (addr.protocol() == QAbstractSocket::IPv4Protocol && addr != QHostAddress::LocalHost) {
            m_cmbLocalIp->addItem(addr.toString());
        }
    }
    m_cmbLocalIp->addItem("127.0.0.1 (本机回环)");
    connLayout->addWidget(m_cmbLocalIp);

    connLayout->addWidget(new QLabel("本地端口:"));
    m_spinLocalPort = new QSpinBox();
    m_spinLocalPort->setFixedHeight(22);
    m_spinLocalPort->setRange(1, 65535);
    m_spinLocalPort->setValue(13601);
    connLayout->addWidget(m_spinLocalPort);

    m_btnOpen = new QPushButton("打开");
    m_btnOpen->setCheckable(true);
    m_btnOpen->setFixedHeight(24);
    connLayout->addWidget(m_btnOpen);

    connLayout->addSpacing(3);
    connLayout->addWidget(new QLabel("远程地址（IP/网址:端口）"));
//    m_cmbRemoteIp = new QComboBox();
//    m_cmbRemoteIp->setFixedHeight(22);
//    m_cmbRemoteIp->setEditable(true);
//    m_cmbRemoteIp->addItem("127.0.0.1");
//    m_cmbRemoteIp->setEnabled(false);
//    connLayout->addWidget(m_cmbRemoteIp);

//    connLayout->addWidget(new QLabel("远程端口:"));
//    m_spinRemotePort = new QSpinBox();
//    m_spinRemotePort->setFixedHeight(22);
//    m_spinRemotePort->setRange(1, 65535);
//    m_spinRemotePort->setValue(13649);
//    m_spinRemotePort->setEnabled(false);

    m_cmbRemoteAddr = new QComboBox();
    m_cmbRemoteAddr->setEditable(true);
    m_cmbRemoteAddr->setToolTip(QStringLiteral("支持 127.0.0.1:80、www.example.com:443、https://www.example.com"));
    RemoteHost addr;
    addr.port = 13601;
    addr.address = QHostAddress(QStringLiteral("127.0.0.1"));
    addr.host = QStringLiteral("127.0.0.1");
    addHostAddr(addr);
    connLayout->addWidget(m_cmbRemoteAddr);
    if (m_cmbRemoteAddr->lineEdit()) {
        m_cmbRemoteAddr->lineEdit()->setPlaceholderText(
            QStringLiteral("127.0.0.1:80 或 www.example.com:443"));
    }

    m_btnConnect = new QPushButton("连接");
    m_btnConnect->setEnabled(false);
    m_btnConnect->setFixedHeight(24);
    connLayout->addWidget(m_btnConnect);
    settingLayout->addWidget(m_groupConnection);

    // 接收设置
    m_groupRecvSetting = new QGroupBox("接收设置");
    m_groupRecvSetting->setFixedWidth(settingWidth);
    QVBoxLayout *recvSettingLayout = new QVBoxLayout(m_groupRecvSetting);
    recvSettingLayout->setSpacing(3);
    recvSettingLayout->setContentsMargins(8, 10, 8, 8);

    m_checkHexRecv = new QCheckBox("十六进制显示");
    recvSettingLayout->addWidget(m_checkHexRecv);

    m_checkRecvTimestamp = new QCheckBox("加时间戳,分包显示");
    m_checkShowRecvAddr = new QCheckBox("显示接收/对端地址");
    m_scrollToBottom = new QCheckBox("显示最新接收数据");

    recvSettingLayout->addWidget(m_checkRecvTimestamp);
    recvSettingLayout->addWidget(m_checkShowRecvAddr);
    recvSettingLayout->addWidget(m_scrollToBottom);

    m_btnClearRecv = new QPushButton("清空接收");
    m_btnClearRecv->setFixedHeight(24);
    recvSettingLayout->addWidget(m_btnClearRecv);

    m_btnSaveLog = new QPushButton("保存日志");
    m_btnSaveLog->setFixedHeight(24);
    recvSettingLayout->addWidget(m_btnSaveLog);

    recvSettingLayout->addSpacing(2);
    m_labelRecvCount = new QLabel("接收: 0 字节");
    recvSettingLayout->addWidget(m_labelRecvCount);
    settingLayout->addWidget(m_groupRecvSetting);

    // 发送设置
    m_groupSendSetting = new QGroupBox("发送设置");
    m_groupSendSetting->setFixedWidth(settingWidth);
    QVBoxLayout *sendSettingLayout = new QVBoxLayout(m_groupSendSetting);
    sendSettingLayout->setSpacing(3);
    sendSettingLayout->setContentsMargins(8, 10, 8, 8);

    m_checkHexSend = new QCheckBox("十六进制发送");
    sendSettingLayout->addWidget(m_checkHexSend);

    m_checkAutoSend = new QCheckBox("定时发送");

    m_checkAddModbusCrc16 = new QCheckBox("CRC16 Modbus");
    m_checkAppendCRLF = new QCheckBox("加回车换行");
    m_checkBroadcastSend = new QCheckBox("广播发送");

    sendSettingLayout->addWidget(m_checkAddModbusCrc16);
    sendSettingLayout->addWidget(m_checkAppendCRLF);
    sendSettingLayout->addWidget(m_checkBroadcastSend);
    sendSettingLayout->addWidget(m_checkAutoSend);

    m_spinAutoSendInterval = new QSpinBox();
    m_spinAutoSendInterval->setFixedHeight(22);
    m_spinAutoSendInterval->setRange(100, 60000);
    m_spinAutoSendInterval->setValue(1000);
    m_spinAutoSendInterval->setSuffix(" ms");
    m_spinAutoSendInterval->setEnabled(false);
    sendSettingLayout->addWidget(m_spinAutoSendInterval);

    m_btnSend = new QPushButton("发送");
    m_btnSend->setEnabled(false);
    m_btnSend->setFixedHeight(24);
    sendSettingLayout->addWidget(m_btnSend);

    sendSettingLayout->addSpacing(2);
    m_labelSendCount = new QLabel("发送: 0 字节");
    sendSettingLayout->addWidget(m_labelSendCount);
    settingLayout->addWidget(m_groupSendSetting);
    settingLayout->addStretch();

    contentLayout->addLayout(settingLayout, 3);
    mainLayout->addLayout(contentLayout, 1);

    // 状态栏
    QHBoxLayout *statusLayout = new QHBoxLayout();
    statusLayout->setContentsMargins(3, 0, 3, 0);
    m_labelStatus = new QLabel("未打开");
    m_labelClientCount = new QLabel("连接数: 0");
    statusLayout->addWidget(m_labelStatus);
    statusLayout->addStretch();
    statusLayout->addWidget(m_labelClientCount);
    mainLayout->addLayout(statusLayout);

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
    m_scrollToBottom->setChecked(false);
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
        m_labelStatus->setText(QStringLiteral("启动中..."));
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
        m_btnSend->setEnabled(false);
        m_checkAutoSend->setChecked(false);
        m_labelStatus->setText(QStringLiteral("未打开"));
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
            m_labelStatus->setText(QStringLiteral("远程地址无效"));
            return;
        }
        m_manualTcpDisconnect = false;
        emit sigTcpConnect(remoteIp, remotePort);
        m_btnConnect->setText(QStringLiteral("断开"));
        m_labelStatus->setText(QStringLiteral("连接中..."));
    } else {
        m_manualTcpDisconnect = true;
        emit sigTcpDisconnect();
        m_btnConnect->setText(QStringLiteral("连接"));
        m_labelStatus->setText(QStringLiteral("已断开"));
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
            QString oldStatus = m_labelStatus->text();
            m_labelStatus->setText("日志已保存");
            QTimer::singleShot(2000, this, [=](){
                if (m_labelStatus->text() == "日志已保存") {
                    m_labelStatus->setText(oldStatus);
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
            m_labelStatus->setText(QStringLiteral("请选择有效的远程地址"));
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
    appendLog("TCP连接成功", Qt::darkGreen);
    m_labelStatus->setText("已连接");
    m_labelClientCount->setText("已连接");
}

void NetAssistWidget::slotTcpDisconnected()
{
    appendLog(QStringLiteral("TCP连接断开"), Qt::darkGray);
    if (m_manualTcpDisconnect) {
        m_labelStatus->setText(QStringLiteral("已断开"));
        m_btnConnect->setText(QStringLiteral("连接"));
    } else {
        m_labelStatus->setText(QStringLiteral("重连中..."));
        m_btnConnect->setText(QStringLiteral("断开"));
    }
    m_labelClientCount->setText(QStringLiteral("未连接"));
}

void NetAssistWidget::slotError(QString errStr)
{
    appendLog("[错误] " + errStr, Qt::red);
}

void NetAssistWidget::slotStateText(QString text)
{
    m_labelStatus->setText(text);
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
    m_labelRecvCount->setText(QString("接收: %1 字节").arg(m_recvBytes));
    m_labelSendCount->setText(QString("发送: %1 字节").arg(m_sendBytes));
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
    m_scrollToBottom->setChecked(settings.value(QStringLiteral("scrollBottom"), false).toBool());
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

