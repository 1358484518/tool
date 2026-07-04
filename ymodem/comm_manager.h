#pragma once

#include <QObject>
#include <QIODevice>
#include <QSerialPort>
#include <QTcpSocket>
#include <QtWebSockets/QWebSocket>
#include <QtWebSockets/QWebSocketProtocol>
#include <QTimer>
#include <QUrl>
#include <QtGlobal>
#include <QSslError>

struct CommConfig {
    enum CommType {
        Serial,
        Tcp,
        WebSocket
    };

    CommType type = Serial;
    quint32 connectTimeoutMs = 5000;
    quint32 gracefulCloseTimeoutMs = 2000;
    bool lowLatencyMode = true;
    bool tcpKeepAlive = true;

    // Serial port parameters
    QString serialPort;
    qint32 baudRate = 115200;
    QSerialPort::DataBits dataBits = QSerialPort::Data8;
    QSerialPort::Parity parity = QSerialPort::NoParity;
    QSerialPort::StopBits stopBits = QSerialPort::OneStop;
    QSerialPort::FlowControl flowControl = QSerialPort::NoFlowControl;

    // TCP parameters
    QString tcpHost;
    quint16 tcpPort = 0;

    // WebSocket parameters
    QString wsUrl;
    QString wsSubProtocol;
    quint32 wsPingIntervalMs = 30000;
    bool wsIgnoreSslErrors = false; // WARNING: Debug only, disable in production
};

enum CommState {
    Unconnected,
    Connecting,
    Connected,
    Closing,
    Error
};

class CommunicationManager : public QObject
{
    Q_OBJECT
public:
    explicit CommunicationManager(QObject *parent = nullptr);
    ~CommunicationManager() override;

    bool open(const CommConfig& config);
    void close(bool graceful = true);
    qint64 send(const QByteArray& data);
    CommState state() const;

signals:
    void connected();
    void disconnected();
    void dataReceived(const QByteArray& data);
    void errorOccurred(const QString& errMsg);

private slots:
    void onReadyRead();
    void onDeviceError(QSerialPort::SerialPortError serialErr);
    void onTcpError(QAbstractSocket::SocketError tcpErr);
    void onWsError(QAbstractSocket::SocketError wsErr);
    void onConnectTimeout();
    void onGracefulCloseTimeout();

private:
    QObject* m_device = nullptr;
    CommState m_state = Unconnected;
    CommConfig m_config;
    QTimer* m_connectTimer = nullptr;
    QTimer* m_closeTimer = nullptr;
    QTimer* m_wsPingTimer = nullptr; // Manual ping timer for Qt5.14 compatibility

    void cleanupDevice();
    void forceAbort();
    void handleDisconnect();
};
