#pragma once

#include <QObject>
#include <QTimer>
#include <QByteArray>
#include <QString>
#include <QSerialPort>
#include "comm_manager.h"

class ExampleCommon : public QObject
{
    Q_OBJECT
public:
    explicit ExampleCommon(QObject *parent = nullptr);
    ~ExampleCommon() override;

    /**
     * @brief Connect to serial port, default 8N1 no flow control
     * @param portName e.g. "COM3" "/dev/ttyUSB0"
     * @param baudrate default 115200
     */
    void connectToSerial(const QString& portName, qint32 baudrate = 115200);

    /**
     * @brief Connect to TCP server, enable low latency and keepalive by default
     * @param host IP or domain
     * @param port TCP port
     */
    void connectToTcp(const QString& host, quint16 port);

    /**
     * @brief Connect to WebSocket server, default OCPP1.6 subprotocol
     * @param url ws:// or wss:// address
     * @param subProtocol default "ocpp1.6" for charger scenario
     * @param ignoreSslErrors set true for self-signed certificate debug
     */
    void connectToWebSocket(const QString& url, const QString& subProtocol = "ocpp1.6", bool ignoreSslErrors = true);

    /**
     * @brief Disconnect from current device
     * @param graceful true: protocol level close, false: force abort
     */
    void disconnect(bool graceful = true);

    /**
     * @brief Send raw binary data
     * @return sent bytes count, -1 if failed
     */
    qint64 sendRaw(const QByteArray& data);

    /**
     * @brief Send hex string, auto convert to binary (support space separated format)
     * @param hexString e.g. "01 03 00 00 00 0A C5 CD"
     * @return sent bytes count, -1 if failed
     */
    qint64 sendHex(const QString& hexString);

    /**
     * @brief Get current connection state
     */
    CommState state() const;

    // Static hex conversion utils
    static QByteArray hexStringToBytes(const QString& hex);
    static QString bytesToHexString(const QByteArray& bytes);

signals:
    void connected();
    void disconnected();
    /**
     * @brief Data received signal
     * @param rawData raw binary data for protocol parsing
     * @param hexStr hex string for UI display
     */
    void dataReceived(const QByteArray& rawData, const QString& hexStr);
    void errorOccurred(const QString& errMsg);
    /**
     * @brief Log message signal, bind to UI log view directly
     * @param color HTML color code
     */
    void logMessage(const QString& msg, const QString& color = "#000000");

private slots:
    void onCommConnected();
    void onCommDisconnected();
    void onCommDataReceived(const QByteArray& data);
    void onCommError(const QString& err);
    void onReconnectTimeout();

private:
    CommunicationManager* m_comm;
    QTimer* m_reconnectTimer;
    bool m_manualDisconnect = false;
    CommConfig m_lastConfig;

    void startConnect(const CommConfig& cfg);
};
