#ifndef SERIALMANAGER_H
#define SERIALMANAGER_H
#include <QObject>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QTimer>
#include <QByteArray>
#include <QList>
/**
 * @brief Enhanced Serial Port Manager for Qt
 *
 * Features:
 * - Open/close serial port with configurable parameters
 * - Thread-safe data send/receive
 * - Automatic reconnection on unexpected disconnection
 * - Error handling and status notification
 * - Receive buffering with configurable timeout
 * - Signal-slot based event notification
 */
class SerialManager : public QObject
{
    Q_OBJECT
public:
    enum ConnectionState {
        Disconnected = 0,
        Connecting,
        Connected,
        Error
    };
    Q_ENUM(ConnectionState)
    struct SerialConfig {
        QString portName;
        qint32 baudRate = QSerialPort::Baud115200;
        QSerialPort::DataBits dataBits = QSerialPort::Data8;
        QSerialPort::Parity parity = QSerialPort::NoParity;
        QSerialPort::StopBits stopBits = QSerialPort::OneStop;
        QSerialPort::FlowControl flowControl = QSerialPort::NoFlowControl;
        bool autoReconnect = true;
        int reconnectIntervalMs = 3000;
        int readBufferTimeoutMs = 50;
    };
    explicit SerialManager(QObject *parent = nullptr);
    explicit SerialManager(const SerialConfig &config, QObject *parent = nullptr);
    ~SerialManager() override;
    // Configuration
    void setConfig(const SerialConfig &config);
    SerialConfig getConfig() const;
    static QList<QSerialPortInfo> availablePorts();
    // Connection management
    bool open();
    void close();
    bool isOpen() const;
    ConnectionState connectionState() const;
    // Data transmission
    qint64 write(const QByteArray &data);
    qint64 write(const char *data, qint64 len);
    void flush();
    void clearReceiveBuffer();
    // Status
    QString lastError() const;
    qint64 bytesAvailable() const;
    qint64 bytesToWrite() const;
public slots:
    /**
     * @brief Send raw binary data
     * @param data Binary byte array to send
     * @return Number of bytes actually written, -1 if not connected
     */
    qint64 sendBinary(const QByteArray &data);
    /**
     * @brief Send text string (local 8-bit encoding)
     * @param text Text string to send, automatically adds \r\n if appendCRLF is true
     * @param appendCRLF If true, append carriage return + line feed (0x0D 0x0A)
     * @return Number of bytes actually written, -1 if not connected
     */
    qint64 sendString(const QString &text, bool appendCRLF = false);
    /**
     * @brief Send hex formatted string
     * @param hexStr Hex string like "FF 01 02 AA", spaces and case-insensitive
     * @return Number of bytes actually written, -1 if not connected or invalid hex
     */
    qint64 sendHex(const QString &hexStr);
    /**
     * @brief Set DTR pin state
     * @param enabled true to set DTR on, false to set off
     */
    void setDtr(bool enabled);
    /**
     * @brief Set RTS pin state
     * @param enabled true to set RTS on, false to set off
     */
    void setRts(bool enabled);
signals:
    void connectionStateChanged(SerialManager::ConnectionState state);
    void dataReceived(const QByteArray &data);
    void errorOccurred(QSerialPort::SerialPortError error, const QString &errorString);
    void portConnected();
    void portDisconnected();
private slots:
    void onReadyRead();
    void onErrorOccurred(QSerialPort::SerialPortError error);
    void onReconnectTimer();
    void onReadBufferTimeout();
private:
    bool createSerialPort(); // 新增：创建新串口实例
    void setState(ConnectionState state);
    bool applyConfig();
    QSerialPort *m_serial;
    SerialConfig m_config;
    ConnectionState m_state;
    QTimer *m_reconnectTimer;
    QTimer *m_readBufferTimer;
    QByteArray m_receiveBuffer;
    QString m_lastError;
    bool m_manualClose;
};
#endif // SERIALMANAGER_H

