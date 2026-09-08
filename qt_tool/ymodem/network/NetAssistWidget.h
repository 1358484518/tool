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
 * 打开/连接/发送用信号丢过去；收包走 onIoData。
 */
class NetAssistWidget : public QWidget
{
    Q_OBJECT
public:
    explicit NetAssistWidget(QWidget *parent = nullptr);
    ~NetAssistWidget();

    IoSource *ioSource() const;  // 即 NetworkWorker，给主窗口和其它控件用

signals:
    void sigOpenNetwork(NetProtocol proto, QString localIp, quint16 localPort);
    void sigCloseNetwork();
    void sigTcpConnect(QString remoteIp, quint16 remotePort);
    void sigTcpDisconnect();
    void sigSendData(QByteArray data, QString remoteIp, quint16 remotePort);
    void ioDataReceived(const IoPacket &packet);

public slots:
    void onIoData(const IoPacket &packet);
    void slotRecvData(QByteArray data, QString fromIp, quint16 fromPort);
    void slotClientConnected(QString ip, quint16 port);
    void slotClientDisconnected(QString ip, quint16 port);
    void slotTcpConnected();
    void slotTcpDisconnected();
    void slotError(QString errStr);
    void slotStateText(QString text);
    void slotClientCount(int count);

private slots:
    void onProtocolChanged(int idx);
    void onOpenToggled(bool checked);
    void onConnectClicked();
    void onClearRecv();
    void onSaveLog();
    void onAutoSendToggled(bool checked);
    void onAutoSendIntervalChanged(int ms);
    void onSendClicked();
    void onConnectAddrChange(QString ip, quint16 port);  // UDP 发现对端时写入下拉框
private:
    void initUi();
    void initConnect();
    void updateProtocolDependentUi();
    void loadSettings();
    void saveSettings();
    QString dataToText(const QByteArray &data);
    void updateCountLabel();
    void appendLog(const QString &text, const QColor &color = Qt::black);
    void setStatus(const QString &text, const QString &color);
    void applyOpenButtonStyle(bool opened);
    void applyConnectButtonStyle(bool connected);

    void initNetWork();  // 创建 NetworkWorker 并移到 workerThread

    bool hasHostInCombo(const RemoteHost &host);
    bool addHostAddr(const RemoteHost &host);  // 已存在则 false
    bool commitCurrentHost();
    bool currentRemote(QString *ip, quint16 *port);

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
