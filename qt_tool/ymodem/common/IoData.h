#ifndef IODATA_H
#define IODATA_H

#include <QByteArray>
#include <QDateTime>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QThread>

/**
 * 串口、TCP、UDP 共用的一包数据。
 *
 * 收：
 *   connect(source, &IoSource::ioDataReceived, widget, &W::onIoData);
 * 发（QueuedConnection，控件不必和链路同线程）：
 *   connect(widget, &W::payloadReady, source,
 *           QOverload<const QByteArray &>::of(&IoSource::sendIoData));
 *   // 指定 UDP/TCP 对端时带上 peer/port：
 *   IoPacket pkt = IoPacket::fromNetwork(IoPacket::Udp, data, ip, port);
 *   QMetaObject::invokeMethod(source, "sendIoData", Qt::QueuedConnection,
 *                             Q_ARG(IoPacket, pkt));
 */
struct IoPacket
{
    enum Channel : quint8 {
        Serial = 0,
        TcpServer,
        TcpClient,
        Udp
    };

    Channel channel = Serial;
    QByteArray data;
    QString peer;      // 串口名，或对端 IP / 域名；发送时网络可空（TCP 客户端 / 广播）
    quint16 port = 0;  // 串口为 0
    qint64 timestampMs = 0;

    static IoPacket fromBytes(const QByteArray &payload)
    {
        IoPacket packet;
        packet.data = payload;
        packet.timestampMs = QDateTime::currentMSecsSinceEpoch();
        return packet;
    }

    static IoPacket fromSerial(const QByteArray &payload, const QString &portName)
    {
        IoPacket packet = fromBytes(payload);
        packet.channel = Serial;
        packet.peer = portName;
        return packet;
    }

    static IoPacket fromNetwork(Channel ch, const QByteArray &payload,
                               const QString &ip, quint16 port)
    {
        IoPacket packet = fromBytes(payload);
        packet.channel = ch;
        packet.peer = ip;
        packet.port = port;
        return packet;
    }
};
Q_DECLARE_METATYPE(IoPacket)

/**
 * 串口 / 网络后端的共同基类。
 * 新控件只连这个对象：收 ioDataReceived，发 sendIoData，不要自己开关串口或套接字。
 */
class IoSource : public QObject
{
    Q_OBJECT
public:
    explicit IoSource(QObject *parent = nullptr)
        : QObject(parent)
    {
        qRegisterMetaType<IoPacket>("IoPacket");
        qRegisterMetaType<IoPacket::Channel>("IoPacket::Channel");
    }

signals:
    void ioDataReceived(const IoPacket &packet);
    void ioDataSent(const IoPacket &packet);  // 实际写出之后，便于其它界面记 TX

public slots:
    /** 必须在本对象当前线程调用。Qt 只能“推”线程，不能从 UI 线程硬拉。 */
    void handoverTo(QObject *target)
    {
        if (!target)
            return;
        QThread *dest = target->thread();
        if (!dest || thread() == dest)
            return;
        moveToThread(dest);
    }

    void sendIoData(const IoPacket &packet)
    {
        if (packet.data.isEmpty())
            return;
        IoPacket out = packet;
        if (out.timestampMs == 0)
            out.timestampMs = QDateTime::currentMSecsSinceEpoch();
        if (writeIoData(out))
            emit ioDataSent(out);
    }

    void sendIoData(const QByteArray &data)
    {
        sendIoData(IoPacket::fromBytes(data));
    }

protected:
    virtual bool writeIoData(const IoPacket &packet) = 0;

    void emitIoData(const IoPacket &packet)
    {
        emit ioDataReceived(packet);
    }
};

#endif // IODATA_H
