#ifndef SERIALMANAGER_H
#define SERIALMANAGER_H

#include "common/IoData.h"

#include <QObject>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QTimer>
#include <QByteArray>
#include <QList>
#include <QString>
#include <QMetaType>

/**
 * Serial port backend. Must live on a worker thread (see TMX_TOOL).
 * QSerialPort and timers are created in initWorker()/open(), never on the UI thread.
 *
 * Data outlet is IoSource::ioDataReceived. Other widgets:
 *   connect(serial, &IoSource::ioDataReceived, widget, &Widget::onIoData);
 */
class SerialManager : public IoSource
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

public slots:
    void initWorker();
    void setConfig(const SerialManager::SerialConfig &config);
    void openWithConfig(const SerialManager::SerialConfig &config);
    bool open();
    void close();
    qint64 sendBinary(const QByteArray &data);
    qint64 sendString(const QString &text, bool appendCRLF = false);
    qint64 sendHex(const QString &hexStr);
    void setDtr(bool enabled);
    void setRts(bool enabled);
    void flush();
    void clearReceiveBuffer();

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
    void emitReceived(const QByteArray &data);
    qint64 write(const QByteArray &data);

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
