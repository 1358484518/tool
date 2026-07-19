#ifndef NETCOMMON_H
#define NETCOMMON_H

#include <QtGlobal>
#include <QMetaType>
#include <QHostAddress>

enum class NetProtocol : quint8
{
    TcpServer = 0,
    TcpClient = 1,
    Udp       = 2
};
Q_DECLARE_METATYPE(NetProtocol)

// 远程主机信息
struct RemoteHost
{
    QHostAddress address;
    quint16      port = 0;
    qint64       lastSeen = 0;

    bool operator==(const RemoteHost &other) const {
        return address == other.address && port == other.port;
    }
};
Q_DECLARE_METATYPE(RemoteHost)

#endif // NETCOMMON_H
