#ifndef NETCOMMON_H
#define NETCOMMON_H

#include <QtGlobal>
#include <QMetaType>

enum class NetProtocol : quint8
{
    TcpServer = 0,
    TcpClient = 1,
    Udp       = 2
};
Q_DECLARE_METATYPE(NetProtocol)

#endif // NETCOMMON_H
