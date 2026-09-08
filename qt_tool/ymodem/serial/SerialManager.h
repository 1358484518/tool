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
 * 收：IoSource::ioDataReceived；发：IoSource::sendIoData。
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

    explicit SerialManager(QObject *parent = nullptr);                 // 默认 115200 8N1
    explicit SerialManager(const SerialConfig &config, QObject *parent = nullptr);  // 带初始参数，仍要 initWorker
    ~SerialManager() override;                                         // close 并毁掉 QSerialPort

    SerialConfig getConfig() const;                     // 当前串口参数
    static QList<QSerialPortInfo> availablePorts();     // 系统里能看到的 COM 口

    bool isOpen() const;                                // 已打开且状态为 Connected
    ConnectionState connectionState() const;
    QString lastError() const;                          // 最近一次失败原因

public slots:
    void initWorker();          // 线程 started 后调用，创建定时器
    void setConfig(const SerialManager::SerialConfig &config);          // 未打开时改参数
    void openWithConfig(const SerialManager::SerialConfig &config);     // 先关再按新参数打开
    bool open();                // 按 m_config 打开端口
    void close();               // 手动关闭，之后不会自动重连
    qint64 sendBinary(const QByteArray &data);          // 转 sendIoData，留给旧连接
    qint64 sendString(const QString &text, bool appendCRLF = false);    // 文本转本地编码后发送
    qint64 sendHex(const QString &hexStr);              // HEX 文本转字节后发送
    void setDtr(bool enabled);  // 拉高/拉低 DTR
    void setRts(bool enabled);  // 拉高/拉低 RTS
    void flush();               // 把发送缓冲刷到设备
    void clearReceiveBuffer();  // 丢掉尚未组完的粘包缓冲

signals:
    void connectionStateChanged(SerialManager::ConnectionState state);  // 连接状态变化（含 Error）
    void dataReceived(const QByteArray &data);  // 原始字节，YModem 订这个
    void errorOccurred(int error, const QString &errorString);  // error 即 QSerialPort::SerialPortError
    void portConnected();       // 打开成功
    void portDisconnected();    // 关掉或掉线
    void openResult(bool ok, const QString &errorString);       // 给 UI 弹失败原因

private slots:
    void onReadyRead();         // 串口有数据，写入粘包缓冲
    void onErrorOccurred(QSerialPort::SerialPortError error);   // 只负责排队，不在这里 close
    void handlePortError(int error, const QString &errorString); // 延后关端口并决定是否重连
    void onReconnectTimer();    // 到点再 open 一次
    void onReadBufferTimeout(); // 粘包等待结束，把缓冲发出去

private:
    bool createSerialPort();    // new QSerialPort 并接线
    void destroySerialPort();   // 断开信号后 close，deleteLater
    void setState(ConnectionState state);  // 更新状态并发 connectionStateChanged
    bool applyConfig();         // 波特率等写入 QSerialPort
    void flushReceiveBuffer();  // 把 m_receiveBuffer 作为一包发出
    void emitReceived(const QByteArray &data);  // 同时发 dataReceived 和 IoPacket
    qint64 write(const QByteArray &data);       // 真正 write，未打开返回 -1

protected:
    bool writeIoData(const IoPacket &packet) override;  // IoSource 发送入口

private:
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
