#pragma once

#include <QObject>
#include <QIODevice>
#include <QSerialPort>
#include <QTcpSocket>
#include <QtWebSockets/QWebSocket>
#include <QTimer>

struct CommConfig {
    enum CommType {
        Serial,
        Tcp,
        WebSocket // New: WebSocket support
    };

    CommType type = Serial;

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

    // WebSocket parameters (new)
    QString wsUrl; // Support ws:// and wss://, e.g. wss://ocpp.example.com/CP001
    QString wsSubProtocol; // Optional: for OCPP etc.
    bool wsIgnoreSslErrors = false; // For self-signed certificate debug
};

enum CommState {
    Unconnected,
    Connecting,
    Connected,
    Error
};

class CommunicationManager : public QObject
{
    Q_OBJECT
public:
    explicit CommunicationManager(QObject *parent = nullptr);
    ~CommunicationManager() override;

    bool open(const CommConfig& config);
    void close();
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
    void onWsError(QAbstractSocket::SocketError wsErr); // New: WebSocket error handler

private:
    QObject* m_device = nullptr; // Changed from QIODevice* to QObject* to support non-QIODevice devices
    CommState m_state = Unconnected;
    CommConfig m_config;

    void cleanupDevice();
};
