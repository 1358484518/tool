#ifndef NETCOMMON_H
#define NETCOMMON_H

#include <QtGlobal>
#include <QMetaType>
#include <QAbstractSocket>
#include <QHostAddress>
#include <QString>
#include <QUrl>

enum class NetProtocol : quint8
{
    TcpServer = 0,
    TcpClient = 1,
    Udp       = 2
};
Q_DECLARE_METATYPE(NetProtocol)

// 远程主机信息，host 可以是 IP 或域名
struct RemoteHost
{
    QHostAddress address;
    QString      host;
    quint16      port = 0;
    qint64       lastSeen = 0;

    QString endpoint() const {
        if (!host.isEmpty())
            return host;
        return address.toString();
    }

    bool operator==(const RemoteHost &other) const {
        if (port != other.port)
            return false;
        return endpoint().compare(other.endpoint(), Qt::CaseInsensitive) == 0;
    }
};
Q_DECLARE_METATYPE(RemoteHost)

inline bool parseTcpPort(const QString &portStr, quint16 *port)
{
    if (!port || portStr.isEmpty())
        return false;
    for (const QChar c : portStr) {
        if (!c.isDigit())
            return false;
    }
    bool ok = false;
    const uint value = portStr.toUInt(&ok);
    if (!ok || value == 0 || value > 65535)
        return false;
    *port = static_cast<quint16>(value);
    return true;
}

inline bool isPlausibleHostName(const QString &host)
{
    if (host.isEmpty() || host.size() > 253)
        return false;
    if (host.contains(QLatin1Char(' ')) || host.contains(QLatin1Char('/'))
        || host.contains(QLatin1Char('?')) || host.contains(QLatin1Char('#'))) {
        return false;
    }
    return true;
}

// 支持 127.0.0.1:80、[::1]:80、www.example.com:443、https://www.example.com/
inline bool parseRemoteEndpoint(const QString &input, RemoteHost *out)
{
    if (!out)
        return false;
    const QString text = input.trimmed();
    if (text.isEmpty())
        return false;

    QString host;
    quint16 port = 0;
    QHostAddress addr;

    if (text.contains(QLatin1String("://"))) {
        const QUrl url(text);
        if (!url.isValid() || url.host().isEmpty())
            return false;
        host = url.host();
        int urlPort = url.port(-1);
        if (urlPort <= 0) {
            const QString scheme = url.scheme().toLower();
            urlPort = (scheme == QLatin1String("https") || scheme == QLatin1String("wss"))
                          ? 443
                          : 80;
        }
        if (urlPort <= 0 || urlPort > 65535)
            return false;
        port = static_cast<quint16>(urlPort);
        addr.setAddress(host);
    } else if (text.startsWith(QLatin1Char('['))) {
        const int rb = text.indexOf(QLatin1Char(']'));
        if (rb < 2 || rb + 2 >= text.size() || text.at(rb + 1) != QLatin1Char(':'))
            return false;
        const QString ipStr = text.mid(1, rb - 1).trimmed();
        const QString portStr = text.mid(rb + 2).trimmed();
        if (!addr.setAddress(ipStr) || addr.protocol() != QAbstractSocket::IPv6Protocol)
            return false;
        if (!parseTcpPort(portStr, &port))
            return false;
        host = addr.toString();
    } else {
        const int colonPos = text.lastIndexOf(QLatin1Char(':'));
        if (colonPos < 0) {
            host = text;
            port = 80;
        } else {
            host = text.left(colonPos).trimmed();
            if (!parseTcpPort(text.mid(colonPos + 1).trimmed(), &port))
                return false;
        }
        if (!isPlausibleHostName(host))
            return false;
        addr.setAddress(host);
        if (!addr.isNull() && addr.protocol() == QAbstractSocket::IPv4Protocol
            && addr.toString() != host) {
            return false;
        }
    }

    if (!isPlausibleHostName(host) || port == 0)
        return false;

    out->host = host;
    out->address = addr;
    out->port = port;
    return true;
}

#endif // NETCOMMON_H
