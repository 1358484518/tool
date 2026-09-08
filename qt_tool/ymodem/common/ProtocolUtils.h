#ifndef PROTOCOLUTILS_H
#define PROTOCOLUTILS_H

#include <QByteArray>
#include <QDateTime>
#include <QString>

/** HEX 编解码、CRC16 Modbus、时间戳。串口和网络发送前都会用到。 */
namespace ProtocolUtils {

/** 从文本里抽出 0-9A-Fa-f，奇数个半字节会丢掉最后一个。 */
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

/** 字节转空格分隔的大写 HEX，例如 "01 0A FF"。 */
inline QString bytesToHexString(const QByteArray &data)
{
    return QString::fromLatin1(data.toHex(' ')).toUpper();
}

/** Modbus CRC16，低字节在前。 */
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

/** 在数据末尾追加 Modbus CRC16（低字节在前）。空数据原样返回。 */
inline QByteArray appendCrc16Modbus(QByteArray data)
{
    if (data.isEmpty())
        return data;
    const quint16 crc = crc16Modbus(data);
    data.append(static_cast<char>(crc & 0xFF));
    data.append(static_cast<char>((crc >> 8) & 0xFF));
    return data;
}

/** 当前时间，格式 HH:mm:ss.zzz，给接收区当时间戳。 */
inline QString timestampMs()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
}

} // namespace ProtocolUtils

#endif // PROTOCOLUTILS_H
