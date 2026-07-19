#include "NetAssistWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QCheckBox>
#include <QPlainTextEdit>
#include "FastTextView.h"
#include <QFont>
#include <QTimer>
#include <QNetworkInterface>
#include <QFileDialog>
#include <QFile>
#include <QTextCursor>
#include <QDateTime>
#include <QHostAddress>
#include <QLineEdit>
#include "NetworkWorker.h"

NetAssistWidget::NetAssistWidget(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle("网络调试助手");
    resize(850, 600);
    initUi();
    initConnect();
    initNetWork();
}

NetAssistWidget::~NetAssistWidget()
{
    // 安全退出顺序：先停止线程事件循环，等待线程结束，再delete Worker
    workerThread.quit();
    workerThread.wait();
//    delete m_netWorker; // 线程结束后直接删除，不依赖事件循环，100%释放内存
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
    m_editSend->setPlaceholderText("输入要发送的数据...");
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
    connLayout->addWidget(new QLabel("远程地址:端口"));
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
    RemoteHost addr;
    addr.port=13601;
    addr.address=QHostAddress("127.0.0.1");
    addHostAddr(addr);
    connLayout->addWidget(m_cmbRemoteAddr);

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
    recvSettingLayout->addWidget(m_checkRecvTimestamp);
    recvSettingLayout->addWidget(m_checkShowRecvAddr);

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
    m_scrollToBottom = new QCheckBox("显示末尾数据");
    sendSettingLayout->addWidget(m_checkAddModbusCrc16);
    sendSettingLayout->addWidget(m_checkAppendCRLF);
    sendSettingLayout->addWidget(m_checkBroadcastSend);
    sendSettingLayout->addWidget(m_scrollToBottom);
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
    NetProtocol proto = m_cmbProtocol->currentData().value<NetProtocol>();
    bool isTcpClient = (proto == NetProtocol::TcpClient);
//    m_cmbRemoteIp->setEnabled(isTcpClient && m_btnOpen->isChecked());
//    m_spinRemotePort->setEnabled(isTcpClient && m_btnOpen->isChecked());
    m_btnConnect->setEnabled(isTcpClient && m_btnOpen->isChecked());
}

void NetAssistWidget::onOpenToggled(bool checked)
{
    if (checked) {
        m_btnOpen->setText("关闭");
        m_cmbProtocol->setEnabled(false);
        m_cmbLocalIp->setEnabled(false);
        m_spinLocalPort->setEnabled(false);
        m_btnSend->setEnabled(true);
        m_labelStatus->setText("启动中...");
        m_recvBytes = 0;
        m_sendBytes = 0;
        updateCountLabel();

        NetProtocol proto = m_cmbProtocol->currentData().value<NetProtocol>();
        QString localIp = m_cmbLocalIp->currentText().split(" ").first();
        quint16 localPort = m_spinLocalPort->value();
        emit sigOpenNetwork(proto, localIp, localPort);

        if (proto == NetProtocol::TcpClient||proto == NetProtocol::Udp) {
//            m_cmbRemoteIp->setEnabled(true);
//            m_spinRemotePort->setEnabled(true);
            m_btnConnect->setEnabled(true);
            m_labelClientCount->setText("未连接");
        } else if (proto == NetProtocol::TcpServer) {
            m_labelClientCount->setText("连接数: 0");
        } else {
            m_labelClientCount->setText("UDP模式");
        }
    }
    else {
        m_btnOpen->setText("打开");
        m_cmbProtocol->setEnabled(true);
        m_cmbLocalIp->setEnabled(true);
        m_spinLocalPort->setEnabled(true);
//        m_cmbRemoteIp->setEnabled(false);
//        m_spinRemotePort->setEnabled(false);
        m_btnConnect->setEnabled(false);
        m_btnConnect->setText("连接");
        m_btnSend->setEnabled(false);
        m_checkAutoSend->setChecked(false);
        m_labelStatus->setText("未打开");
        m_labelClientCount->setText("连接数: 0");
        emit sigCloseNetwork();
    }
}

void NetAssistWidget::onConnectClicked()
{
    if (m_btnConnect->text() == "连接") {
//        QString remoteIp = m_cmbRemoteIp->currentText();
//        quint16 remotePort = m_spinRemotePort->value();
        RemoteHost h = m_cmbRemoteAddr->currentData().value<RemoteHost>();
        QString remoteIp = h.address.toString();
        quint16 remotePort = h.port;

        emit sigTcpConnect(remoteIp, remotePort);
        m_btnConnect->setText("断开");
        m_labelStatus->setText("连接中...");
    }
    else {
        emit sigTcpDisconnect();
        m_btnConnect->setText("连接");
        m_labelStatus->setText("已断开");
        m_labelClientCount->setText("未连接");
    }
}

void NetAssistWidget::onClearRecv()
{
    m_editRecv->clear();
    m_recvBytes = 0;
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
    QByteArray data = m_editSend->toPlainText().toUtf8();
    if (data.isEmpty()) return;

    if (m_checkHexSend->isChecked()) {
        data = hexStringToBytes(m_editSend->toPlainText());
        if (m_addModbusCrc16 && !data.isEmpty()) {
            quint16 crc = crc16Modbus(data);
            data.append(static_cast<char>(crc & 0xFF));
            data.append(static_cast<char>((crc >> 8) & 0xFF));
        }
    }else{
        if(m_appendCRLF){
            data.append('\r');
            data.append('\n');
        }
    }

//    QString remoteIp = m_cmbRemoteIp->currentText();
//    quint16 remotePort = m_spinRemotePort->value();
    RemoteHost h = m_cmbRemoteAddr->currentData().value<RemoteHost>();
    QString remoteIp = h.address.toString();
    quint16 remotePort = h.port;
    qDebug()<<"remote addr"<<remoteIp<<remotePort;
    emit sigSendData(data, remoteIp, remotePort);

    m_sendBytes += data.size();
    updateCountLabel();
    appendLog(QString("[TX] %1").arg(dataToText(data)), Qt::blue);
}

void NetAssistWidget::onConnectAddrChange(QString ip, quint16 port)
{
    // ==================== 1. 构造标准条目 ====================
    RemoteHost host;
    host.address = QHostAddress(ip);
    host.port = port;
    host.lastSeen = QDateTime::currentMSecsSinceEpoch();

    // ==================== 2. 添加并选中 ====================
    addHostAddr(host);
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
    appendLog("TCP连接断开", Qt::darkGray);
    m_labelStatus->setText("重连中...");
    m_labelClientCount->setText("未连接");
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
    if (m_checkHexRecv->isChecked()) {
        return data.toHex(' ').toUpper();
    } else {
        return QString::fromUtf8(data);
    }
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

    // 网络线程 -> UI
    QObject::connect(m_netWorker, &NetworkWorker::sigRecvData, this, &NetAssistWidget::slotRecvData);
    QObject::connect(m_netWorker, &NetworkWorker::sigClientConnected, this, &NetAssistWidget::slotClientConnected);
    QObject::connect(m_netWorker, &NetworkWorker::sigClientDisconnected, this, &NetAssistWidget::slotClientDisconnected);
    QObject::connect(m_netWorker, &NetworkWorker::sigTcpConnected, this, &NetAssistWidget::slotTcpConnected);
    QObject::connect(m_netWorker, &NetworkWorker::sigTcpDisconnected, this, &NetAssistWidget::slotTcpDisconnected);
    QObject::connect(m_netWorker, &NetworkWorker::sigError, this, &NetAssistWidget::slotError);
    QObject::connect(m_netWorker, &NetworkWorker::sigStateText, this, &NetAssistWidget::slotStateText);
    QObject::connect(m_netWorker, &NetworkWorker::sigClientCount, this, &NetAssistWidget::slotClientCount);
    QObject::connect(&workerThread, &QThread::finished, m_netWorker, &QObject::deleteLater);
    //    onConnectAddrChange
    QObject::connect(m_netWorker,&NetworkWorker::sigClientConnected,this, &NetAssistWidget::onConnectAddrChange);

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
    // 已存在则直接返回，不重复添加
    if (hasHostInCombo( host)) {
        return false;
    }

    // 格式化显示文本，IPv6自动加方括号
    QString displayText;
    if (host.address.protocol() == QAbstractSocket::IPv6Protocol) {
        displayText = QString("[%1]:%2").arg(host.address.toString()).arg(host.port);
    } else {
        displayText = QString("%1:%2").arg(host.address.toString()).arg(host.port);
    }

    m_cmbRemoteAddr->addItem(displayText, QVariant::fromValue(host));
    return true;
}

bool NetAssistWidget::commitCurrentHost()
{
    QString input = m_cmbRemoteAddr->currentText().trimmed();
    if (input.isEmpty()) return false; // 空输入直接拦截

    QHostAddress addr;
    quint16 port = 0;

    // ==================== 1. 严格IP+端口校验 ====================
    if (input.startsWith('[')) {
        // ---------- IPv6: 必须是[addr]:port标准格式 ----------
        int rb = input.indexOf(']');
        if (rb < 2 || rb+2 >= input.size() || input[rb+1] != ':') return false;

        QString ipStr = input.mid(1, rb-1).trimmed();
        QString portStr = input.mid(rb+2).trimmed();
        if (ipStr.isEmpty() || portStr.isEmpty()) return false;

        if (!addr.setAddress(ipStr) || addr.protocol() != QAbstractSocket::IPv6Protocol) return false;

        // 端口校验：全数字、1-65535
        bool ok = true;
        for (QChar c : portStr) if (!c.isDigit()) { ok = false; break; }
        if (!ok) return false;
        port = portStr.toUShort(&ok);
        if (!ok || port == 0) return false;

    } else {
        // ---------- IPv4: 必须是x.x.x.x:port标准格式 ----------
        int colonCnt = input.count(':');
        if (colonCnt != 1) return false;

        int colonPos = input.lastIndexOf(':');
        QString ipStr = input.left(colonPos).trimmed();
        QString portStr = input.mid(colonPos+1).trimmed();
        if (ipStr.isEmpty() || portStr.isEmpty()) return false;

        if (!addr.setAddress(ipStr) || addr.protocol() != QAbstractSocket::IPv4Protocol) return false;
        if (addr.toString() != ipStr) return false;

        // 端口校验：全数字、1-65535
        bool ok = true;
        for (QChar c : portStr) if (!c.isDigit()) { ok = false; break; }
        if (!ok) return false;
        port = portStr.toUShort(&ok);
        if (!ok || port == 0) return false;
    }

    // ==================== 2. 构造标准条目 ====================
    RemoteHost host;
    host.address = addr;
    host.port = port;
    host.lastSeen = QDateTime::currentMSecsSinceEpoch();

    // ==================== 3. 添加并选中 ====================
    addHostAddr(host);
    for (int i = 0; i < m_cmbRemoteAddr->count(); ++i) {
        if (m_cmbRemoteAddr->itemData(i).value<RemoteHost>() == host) {
            m_cmbRemoteAddr->setCurrentIndex(i);
            break;
        }
    }

    return true;
}

quint16 NetAssistWidget::crc16Modbus(const QByteArray &data) const
{
    quint16 crc = 0xFFFF;
    for (int i = 0; i < data.size(); i++) {
        crc ^= static_cast<quint8>(data[i]);
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

QByteArray NetAssistWidget::hexStringToBytes(const QString &str) const
{
    QByteArray data;
    QString cleanHex = str;
    cleanHex.remove(QRegExp("[^0-9A-Fa-f]"));
    for (int i = 0; i < cleanHex.length(); i += 2) {
        bool ok;
        quint8 byte = cleanHex.mid(i, 2).toUInt(&ok, 16);
        if (ok) data.append(static_cast<char>(byte));
    }
    return data;
}

