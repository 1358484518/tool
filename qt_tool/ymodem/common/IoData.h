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
 * 订阅方式：
 *   connect(source, &IoSource::ioDataReceived, receiver, &Receiver::onIoData);
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
    QString peer;      // 串口名，或对端 IP / 域名
    quint16 port = 0;  // 串口为 0
    qint64 timestampMs = 0;

    static IoPacket fromSerial(const QByteArray &payload, const QString &portName)
    {
        IoPacket packet;
        packet.channel = Serial;
        packet.data = payload;
        packet.peer = portName;
        packet.port = 0;
        packet.timestampMs = QDateTime::currentMSecsSinceEpoch();
        return packet;
    }

    static IoPacket fromNetwork(Channel ch, const QByteArray &payload,
                               const QString &ip, quint16 port)
    {
        IoPacket packet;
        packet.channel = ch;
        packet.data = payload;
        packet.peer = ip;
        packet.port = port;
        packet.timestampMs = QDateTime::currentMSecsSinceEpoch();
        return packet;
    }
};
Q_DECLARE_METATYPE(IoPacket)

/** 串口 / 网络后端的共同基类：发收包，退出时把线程亲和性交回 UI。 */
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

protected:
    void emitIoData(const IoPacket &packet)
    {
        emit ioDataReceived(packet);
    }
};

#endif // IODATA_H
