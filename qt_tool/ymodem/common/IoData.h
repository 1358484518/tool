#ifndef IODATA_H
#define IODATA_H

#include <QByteArray>
#include <QDateTime>
#include <QMetaType>
#include <QObject>
#include <QString>

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

    static IoPacket fromBytes(const QByteArray &payload)          // 只填数据和时间，channel 默认 Serial
    {
        IoPacket packet;
        packet.data = payload;
        packet.timestampMs = QDateTime::currentMSecsSinceEpoch();
        return packet;
    }

    static IoPacket fromSerial(const QByteArray &payload, const QString &portName)  // 串口收/发包
    {
        IoPacket packet = fromBytes(payload);
        packet.channel = Serial;
        packet.peer = portName;
        return packet;
    }

    static IoPacket fromNetwork(Channel ch, const QByteArray &payload,
                               const QString &ip, quint16 port)   // 网络收/发包，可带对端
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
    explicit IoSource(QObject *parent = nullptr)  // 登记 IoPacket 的元类型，方便跨线程排队
        : QObject(parent)
    {
        qRegisterMetaType<IoPacket>("IoPacket");
        qRegisterMetaType<IoPacket::Channel>("IoPacket::Channel");
    }

signals:
    void ioDataReceived(const IoPacket &packet);  // 链路收到一包，UI 或其它控件在这里显示
    void ioDataSent(const IoPacket &packet);      // 链路真正写出之后，用来记 TX

public slots:
    /** 经当前已打开的串口/网口发送一包；可带对端。空数据直接忽略。 */
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

    /** 只发载荷，对端用链路当前连接（TCP 客户端）或广播。 */
    void sendIoData(const QByteArray &data)
    {
        sendIoData(IoPacket::fromBytes(data));
    }

protected:
    /** 子类真正往串口/套接字写；成功返回 true。 */
    virtual bool writeIoData(const IoPacket &packet) = 0;

    /** 子类收到数据时调用，对外发出 ioDataReceived。 */
    void emitIoData(const IoPacket &packet)
    {
        emit ioDataReceived(packet);
    }
};

#endif // IODATA_H
