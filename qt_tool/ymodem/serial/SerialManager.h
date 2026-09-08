#ifndef SERIALMANAGER_H
#define SERIALMANAGER_H

#include <QObject>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QTimer>
#include <QByteArray>
#include <QList>
#include <QString>
#include <QMetaType>

/**
 * Serial port backend. Lives on a worker thread owned by TMX_TOOL.
 *
 * Features:
 * - Open/close with configurable parameters
 * - Auto-reconnect on unexpected disconnect
 * - Receive coalescing via a short buffer timeout
 * - Signal/slot notifications for UI
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
        int readBufferTimeoutMs = 16;
    };

    explicit SerialManager(QObject *parent = nullptr);
    explicit SerialManager(const SerialConfig &config, QObject *parent = nullptr);
    ~SerialManager() override;

    SerialConfig getConfig() const;
    static QList<QSerialPortInfo> availablePorts();

    bool isOpen() const;
    ConnectionState connectionState() const;
    QString lastError() const;
    qint64 bytesAvailable() const;
    qint64 bytesToWrite() const;

    qint64 write(const QByteArray &data);
    qint64 write(const char *data, qint64 len);
    void flush();
    void clearReceiveBuffer();

public slots:
    void setConfig(const SerialManager::SerialConfig &config);
    void openWithConfig(const SerialManager::SerialConfig &config);
    bool open();
    void close();
    qint64 sendBinary(const QByteArray &data);
    qint64 sendString(const QString &text, bool appendCRLF = false);
    qint64 sendHex(const QString &hexStr);
    void setDtr(bool enabled);
    void setRts(bool enabled);

signals:
    void connectionStateChanged(SerialManager::ConnectionState state);
    void dataReceived(const QByteArray &data);
    void errorOccurred(QSerialPort::SerialPortError error, const QString &errorString);
    void portConnected();
    void portDisconnected();
    void openResult(bool ok, const QString &errorString);

private slots:
    void onReadyRead();
    void onErrorOccurred(QSerialPort::SerialPortError error);
    void onReconnectTimer();
    void onReadBufferTimeout();

private:
    bool createSerialPort();
    void destroySerialPort();
    void setState(ConnectionState state);
    bool applyConfig();
    void flushReceiveBuffer();

    QSerialPort *m_serial = nullptr;
    SerialConfig m_config;
    ConnectionState m_state = Disconnected;
    QTimer *m_reconnectTimer = nullptr;
    QTimer *m_readBufferTimer = nullptr;
    QByteArray m_receiveBuffer;
    QString m_lastError;
    bool m_manualClose = false;
};

Q_DECLARE_METATYPE(SerialManager::SerialConfig)

#endif // SERIALMANAGER_H
