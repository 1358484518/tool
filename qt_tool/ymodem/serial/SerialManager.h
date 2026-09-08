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
 * 串口后端，必须活在工作线程（见 TMX_TOOL::initSerialBackend）。
 * QSerialPort 和定时器只在 initWorker()/open() 里创建，不要在 UI 线程 new。
 * 收到的数据走 IoSource::ioDataReceived。
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
        int readBufferTimeoutMs = 16;  // 粘包等待，超时后整包发出
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
    void initWorker();          // 线程 started 后调用，创建定时器
    void setConfig(const SerialManager::SerialConfig &config);
    void openWithConfig(const SerialManager::SerialConfig &config);
    bool open();
    void close();               // 手动关闭，不会自动重连
    qint64 sendBinary(const QByteArray &data);
    qint64 sendString(const QString &text, bool appendCRLF = false);
    qint64 sendHex(const QString &hexStr);
    void setDtr(bool enabled);
    void setRts(bool enabled);
    void flush();
    void clearReceiveBuffer();

signals:
    void connectionStateChanged(SerialManager::ConnectionState state);
    void dataReceived(const QByteArray &data);  // YModem 也订阅这个
    void errorOccurred(int error, const QString &errorString);  // error 即 QSerialPort::SerialPortError
    void portConnected();
    void portDisconnected();
    void openResult(bool ok, const QString &errorString);

private slots:
    void onReadyRead();
    void onErrorOccurred(QSerialPort::SerialPortError error);
    void handlePortError(int error, const QString &errorString);  // 必须延后执行，不能在 QSerialPort 回调里 close
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
    bool m_manualClose = false;  // true 时禁止自动重连
};

Q_DECLARE_METATYPE(SerialManager::SerialConfig)

#endif // SERIALMANAGER_H
