#ifndef NETASSISTWIDGET_H
#define NETASSISTWIDGET_H

#include <QWidget>
#include <QColor>
#include "NetCommon.h"
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



class NetAssistWidget : public QWidget
{
    Q_OBJECT
public:
    explicit NetAssistWidget(QWidget *parent = nullptr);
    ~NetAssistWidget();
signals:
    void sigOpenNetwork(NetProtocol proto, QString localIp, quint16 localPort);
    void sigCloseNetwork();
    void sigTcpConnect(QString remoteIp, quint16 remotePort);
    void sigTcpDisconnect();
    void sigSendData(QByteArray data, QString remoteIp, quint16 remotePort);

public slots:
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
    //udp
    void onConnectAddrChange(QString ip, quint16 port);
private:
    void initUi();
    void initConnect();
    QString dataToText(const QByteArray &data);
    void updateCountLabel();
    void appendLog(const QString &text, const QColor &color = Qt::black);

    void initNetWork();

    // 把一个 RemoteHost 添加到 comboBox
    // 判断下拉框中是否已存在该主机
    bool hasHostInCombo( const RemoteHost &host);
    // 添加前自动去重，返回true=新增成功，false=已存在未添加
    bool addHostAddr(const RemoteHost &host);
    bool commitCurrentHost(); // 提交当前编辑的远程地址
    quint16 crc16Modbus(const QByteArray &data) const;
    QByteArray hexStringToBytes(const QString &str) const;

    QGroupBox   *m_groupConnection;
    QComboBox   *m_cmbProtocol;
    QComboBox   *m_cmbLocalIp;
    QSpinBox    *m_spinLocalPort;
    QPushButton *m_btnOpen;
//    QComboBox   *m_cmbRemoteIp;
//    QSpinBox    *m_spinRemotePort;
    QComboBox   *m_cmbRemoteAddr;

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

    QCheckBox   *m_checkRecvTimestamp;  // 1. 接收数据加时间戳、按包分包显示
    QCheckBox   *m_checkShowRecvAddr;   // 2. 显示接收/对端地址
    QCheckBox   *m_checkAddModbusCrc16; // 3. 16进制发送时自动追加CRC16 Modbus校验
    QCheckBox   *m_checkAppendCRLF;     // 4. 发送数据自动追加回车换行(\r\n)
    QCheckBox   *m_checkBroadcastSend;  // 新增：广播发送（可勾选开关）

    // 复选框状态值，默认false=未勾选
    bool m_recvTimestamp  = false; // 加时间戳分包显示
    bool m_showRecvAddr   = false; // 显示接收地址
    bool m_addModbusCrc16 = false; // 16进制发送自动加CRC16 Modbus
    bool m_appendCRLF     = false; // 发送自动追加回车换行
    bool m_broadcastSend  = false; // 广播发送开关，默认false=关闭广播走单播

    QSpinBox     *m_spinAutoSendInterval;
    QPushButton  *m_btnSend;
    QLabel       *m_labelSendCount;
    QPlainTextEdit *m_editSend;

    QLabel *m_labelStatus;
    QLabel *m_labelClientCount;

    QTimer  *m_timerAutoSend;
    quint64 m_recvBytes = 0;
    quint64 m_sendBytes = 0;
private:
    NetworkWorker *m_netWorker;
    QThread workerThread;
};

#endif // NETASSISTWIDGET_H
