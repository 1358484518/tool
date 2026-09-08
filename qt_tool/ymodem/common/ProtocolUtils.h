#ifndef PROTOCOLUTILS_H
#define PROTOCOLUTILS_H

#include <QByteArray>
#include <QDateTime>
#include <QString>

/**
 * Shared encode/decode helpers used by both the serial and network assistants.
 */
namespace ProtocolUtils {

inline QByteArray hexStringToBytes(const QString &str)
{
    const QByteArray latin = str.toLatin1();
    QByteArray hex;
    hex.reserve(latin.size());
    for (char c : latin) {
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))
            hex.append(c);
    }
    if (hex.size() % 2 != 0)
        hex.chop(1);
    return QByteArray::fromHex(hex);
}

inline QString bytesToHexString(const QByteArray &data)
{
    return QString::fromLatin1(data.toHex(' ')).toUpper();
}

inline quint16 crc16Modbus(const QByteArray &data)
{
    quint16 crc = 0xFFFF;
    for (unsigned char byte : data) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x0001)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    return crc;
}

inline QByteArray appendCrc16Modbus(QByteArray data)
{
    if (data.isEmpty())
        return data;
    const quint16 crc = crc16Modbus(data);
    data.append(static_cast<char>(crc & 0xFF));
    data.append(static_cast<char>((crc >> 8) & 0xFF));
    return data;
}

inline QString timestampMs()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
}

} // namespace ProtocolUtils

#endif // PROTOCOLUTILS_H
