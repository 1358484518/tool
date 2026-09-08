#ifndef IODATA_H
#define IODATA_H

#include <QByteArray>
#include <QDateTime>
#include <QMetaType>
#include <QObject>
#include <QString>

/**
 * 串口和网络共用的一包数据。
 * 其它控件用同一个槽接收即可：
 *   void onIoData(const IoPacket &packet);
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
    QString peer;      // 串口名，或对端 IP/域名
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

protected:
    void emitIoData(const IoPacket &packet)
    {
        emit ioDataReceived(packet);
    }
};

#endif // IODATA_H
