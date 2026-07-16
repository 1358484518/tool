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

private:
    void initUi();
    void initConnect();
    QString dataToText(const QByteArray &data);
    void updateCountLabel();
    void appendLog(const QString &text, const QColor &color = Qt::black);

    void initNetWork();

    QGroupBox   *m_groupConnection;
    QComboBox   *m_cmbProtocol;
    QComboBox   *m_cmbLocalIp;
    QSpinBox    *m_spinLocalPort;
    QPushButton *m_btnOpen;
    QComboBox   *m_cmbRemoteIp;
    QSpinBox    *m_spinRemotePort;
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
