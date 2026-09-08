#ifndef NETASSISTWIDGET_H
#define NETASSISTWIDGET_H

#include <QWidget>
#include <QColor>
#include "NetCommon.h"
#include "common/IoData.h"
#include "QThread"

class QGroupBox;
class QComboBox;
class QSpinBox;
class QPushButton;
class QCheckBox;
class QPlainTextEdit;
class QLabel;
class QTimer;
class FastTextView;
class NetworkWorker;

/**
 * 网络页 UI。套接字都在 m_netWorker 所在线程。
 * 打开/连接由本页管理；收 onIoData，发走 IoSource::sendIoData。
 */
class NetAssistWidget : public QWidget
{
    Q_OBJECT
public:
    explicit NetAssistWidget(QWidget *parent = nullptr);  // 建 UI、工作线程、NetworkWorker
    ~NetAssistWidget();                                   // 先 close 网络再 handover，再停线程

    IoSource *ioSource() const;  // 返回 NetworkWorker，给其它控件收发用

signals:
    void sigOpenNetwork(NetProtocol proto, QString localIp, quint16 localPort);  // 让工作线程 bind/listen
    void sigCloseNetwork();                         // 关掉当前协议的套接字
    void sigTcpConnect(QString remoteIp, quint16 remotePort);  // TCP 客户端去连对端
    void sigTcpDisconnect();                        // TCP 客户端主动断开
    void sigSendData(QByteArray data, QString remoteIp, quint16 remotePort);  // 旧发送口，内部转 sendIoData
    void ioDataReceived(const IoPacket &packet);    // 转发收到的网络包

public slots:
    void onIoData(const IoPacket &packet);          // 统一收包：写入接收窗口并计数
    void slotRecvData(QByteArray data, QString fromIp, quint16 fromPort);  // 兼容旧信号，转成显示
    void slotClientConnected(QString ip, quint16 port);     // TCP 服务端有新客户端
    void slotClientDisconnected(QString ip, quint16 port);  // TCP 服务端客户端离开
    void slotTcpConnected();                        // TCP 客户端连上
    void slotTcpDisconnected();                     // TCP 客户端断开，决定是否提示重连
    void slotError(QString errStr);                 // 显示工作线程报的错误
    void slotStateText(QString text);               // 显示 Listening / Connecting 等状态
    void slotClientCount(int count);                // 刷新已连接客户端个数

private slots:
    void onProtocolChanged(int idx);                // 切换 TCP 服务/客户端/UDP，刷新控件
    void onOpenToggled(bool checked);               // 打开/关闭本地口或监听
    void onConnectClicked();                        // TCP 客户端连接或断开
    void onClearRecv();                             // 清空接收窗口
    void onSaveLog();                               // 把接收内容存成文件
    void onAutoSendToggled(bool checked);           // 定时发送开关
    void onAutoSendIntervalChanged(int ms);         // 改定时发送间隔
    void onSendClicked();                           // 按 HEX/CRC/换行选项组包后发出
    void onConnectAddrChange(QString ip, quint16 port);  // UDP 发现对端时写入下拉框
private:
    void initUi();                                  // 拼连接区、接收区、发送区
    void initConnect();                             // 把按钮/信号接到槽和 NetworkWorker
    void updateProtocolDependentUi();               // 按协议显示或隐藏连接按钮、广播等
    void loadSettings();                            // 恢复上次协议、端口、选项
    void saveSettings();                            // 把当前网络页参数写入 QSettings
    QString dataToText(const QByteArray &data);     // 按 HEX/文本把字节变成显示字符串
    void updateCountLabel();                        // 刷新 RX/TX 字节计数
    void appendLog(const QString &text, const QColor &color = Qt::black);  // 往接收窗口追加一行
    void setStatus(const QString &text, const QString &color);             // 底部状态文字
    void applyOpenButtonStyle(bool opened);         // 打开按钮的按下/松开样式
    void applyConnectButtonStyle(bool connected);   // 连接按钮的按下/松开样式

    void initNetWork();                             // 创建 NetworkWorker 并移到 workerThread

    bool hasHostInCombo(const RemoteHost &host);    // 对端下拉里是否已有这个地址
    bool addHostAddr(const RemoteHost &host);       // 加入对端下拉，已存在则返回 false
    bool commitCurrentHost();                       // 把编辑框当前内容解析后写入下拉
    bool currentRemote(QString *ip, quint16 *port); // 取出当前要对端的 IP 和端口

    QGroupBox   *m_groupConnection;
    QComboBox   *m_cmbProtocol;
    QComboBox   *m_cmbLocalIp;
    QSpinBox    *m_spinLocalPort;
    QPushButton *m_btnOpen;
//    QComboBox   *m_cmbRemoteIp;
//    QSpinBox    *m_spinRemotePort;
    QComboBox   *m_cmbRemoteAddr;  // 对端，可填 IP、域名、host:port

    QPushButton *m_btnConnect;

    QGroupBox    *m_groupRecvSetting;
    QCheckBox    *m_checkHexRecv;
    QPushButton  *m_btnClearRecv;
    QPushButton  *m_btnSaveLog;
    QLabel       *m_labelRecvCount;
    FastTextView *m_editRecv;

    QGroupBox    *m_groupSendSetting;
    QCheckBox    *m_checkHexSend;
    QCheckBox    *m_checkAutoSend;

    QCheckBox   *m_checkRecvTimestamp;
    QCheckBox   *m_checkShowRecvAddr;
    QCheckBox   *m_checkAddModbusCrc16;
    QCheckBox   *m_checkAppendCRLF;
    QCheckBox   *m_checkBroadcastSend;
    QCheckBox   *m_scrollToBottom;
    bool m_recvTimestamp  = false;
    bool m_showRecvAddr   = false;
    bool m_addModbusCrc16 = false;
    bool m_appendCRLF     = false;
    bool m_broadcastSend  = false;

    QSpinBox     *m_spinAutoSendInterval;
    QPushButton  *m_btnSend;
    QPushButton  *m_btnClearSend;
    QPushButton  *m_btnResetCount;
    QLabel       *m_labelSendCount;
    QPlainTextEdit *m_editSend;

    QLabel *m_labelStatus;
    QLabel *m_labelClientCount;

    QTimer  *m_timerAutoSend;
    quint64 m_recvBytes = 0;
    quint64 m_sendBytes = 0;
private:
    NetworkWorker *m_netWorker = nullptr;  // 运行时无 parent
    QThread workerThread;
    bool m_manualTcpDisconnect = false;    // 用户点断开，不要立刻自动当失败重连
};

#endif // NETASSISTWIDGET_H
