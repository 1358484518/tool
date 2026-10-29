#include "TftpProtocol.h"

#include <QDir>
#include <QFileInfo>

namespace TftpQt {

namespace {

quint16 read16(const QByteArray &raw, int offset)
{
    return quint16((quint8(raw.at(offset)) << 8) | quint8(raw.at(offset + 1)));
}

void append16(QByteArray &out, quint16 value)
{
    out.append(char((value >> 8) & 0xFF));
    out.append(char(value & 0xFF));
}

void appendZ(QByteArray &out, const QByteArray &text)
{
    out.append(text);
    out.append('\0');
}

bool readZ(const QByteArray &raw, int &offset, QByteArray &out)
{
    if (offset < 0 || offset >= raw.size())
        return false;
    const int end = raw.indexOf('\0', offset);
    if (end < 0)
        return false;
    out = raw.mid(offset, end - offset);
    offset = end + 1;
    return true;
}

bool isInsideRoot(const QString &root, const QString &path)
{
    const QString cleanRoot = QDir::cleanPath(root);
    const QString cleanPath = QDir::cleanPath(path);
    const Qt::CaseSensitivity sensitivity =
#ifdef Q_OS_WIN
        Qt::CaseInsensitive;
#else
        Qt::CaseSensitive;
#endif
    if (QString::compare(cleanPath, cleanRoot, sensitivity) == 0)
        return true;
    const QString prefix = cleanRoot.endsWith(QLatin1Char('/'))
        ? cleanRoot
        : cleanRoot + QLatin1Char('/');
    return cleanPath.startsWith(prefix, sensitivity);
}

} // namespace

Packet decodePacket(const QByteArray &raw)
{
    Packet packet;
    if (raw.size() < 2)
        return packet;

    const quint16 opcode = read16(raw, 0);
    packet.opcode = static_cast<Opcode>(opcode);

    switch (packet.opcode) {
    case Opcode::Rrq:
    case Opcode::Wrq: {
        int offset = 2;
        if (!readZ(raw, offset, packet.filename) || packet.filename.isEmpty())
            return packet;
        if (!readZ(raw, offset, packet.mode) || packet.mode.isEmpty())
            return packet;
        while (offset < raw.size()) {
            Option option;
            if (!readZ(raw, offset, option.name) || option.name.isEmpty())
                return packet;
            if (!readZ(raw, offset, option.value))
                return packet;
            packet.options.append(option);
        }
        packet.valid = true;
        return packet;
    }
    case Opcode::Data:
        if (raw.size() < 4)
            return packet;
        packet.block = read16(raw, 2);
        packet.data = raw.mid(4);
        packet.valid = true;
        return packet;
    case Opcode::Ack:
        if (raw.size() < 4)
            return packet;
        packet.block = read16(raw, 2);
        packet.valid = true;
        return packet;
    case Opcode::Error: {
        if (raw.size() < 4)
            return packet;
        packet.errorCode = read16(raw, 2);
        if (raw.size() > 4) {
            int offset = 4;
            if (!readZ(raw, offset, packet.errorMessage))
                packet.errorMessage = raw.mid(4);
        }
        packet.valid = true;
        return packet;
    }
    case Opcode::Oack: {
        int offset = 2;
        while (offset < raw.size()) {
            Option option;
            if (!readZ(raw, offset, option.name) || option.name.isEmpty())
                return packet;
            if (!readZ(raw, offset, option.value))
                return packet;
            packet.options.append(option);
        }
        packet.valid = !packet.options.isEmpty();
        return packet;
    }
    }
    return packet;
}

QByteArray encodeRequest(Opcode opcode, const QByteArray &filename, const QByteArray &mode,
                         const QVector<Option> &options)
{
    QByteArray out;
    out.reserve(2 + filename.size() + mode.size() + 8);
    append16(out, quint16(opcode));
    appendZ(out, filename);
    appendZ(out, mode);
    for (const Option &option : options) {
        appendZ(out, option.name);
        appendZ(out, option.value);
    }
    return out;
}

QByteArray encodeData(quint16 block, const QByteArray &data)
{
    QByteArray out;
    out.reserve(4 + data.size());
    append16(out, quint16(Opcode::Data));
    append16(out, block);
    out.append(data);
    return out;
}

QByteArray encodeAck(quint16 block)
{
    QByteArray out;
    append16(out, quint16(Opcode::Ack));
    append16(out, block);
    return out;
}

QByteArray encodeError(quint16 code, const QByteArray &message)
{
    QByteArray out;
    append16(out, quint16(Opcode::Error));
    append16(out, code);
    appendZ(out, message);
    return out;
}

QByteArray encodeOack(const QVector<Option> &options)
{
    QByteArray out;
    append16(out, quint16(Opcode::Oack));
    for (const Option &option : options) {
        appendZ(out, option.name);
        appendZ(out, option.value);
    }
    return out;
}

bool optionNameIs(const QByteArray &name, const char *expected)
{
    return QString::compare(QString::fromLatin1(name), QLatin1String(expected), Qt::CaseInsensitive) == 0;
}

QByteArray optionValue(const QVector<Option> &options, const char *name)
{
    for (const Option &option : options) {
        if (optionNameIs(option.name, name))
            return option.value;
    }
    return {};
}

QString opcodeName(Opcode opcode)
{
    switch (opcode) {
    case Opcode::Rrq: return QStringLiteral("RRQ");
    case Opcode::Wrq: return QStringLiteral("WRQ");
    case Opcode::Data: return QStringLiteral("DATA");
    case Opcode::Ack: return QStringLiteral("ACK");
    case Opcode::Error: return QStringLiteral("ERROR");
    case Opcode::Oack: return QStringLiteral("OACK");
    }
    return QStringLiteral("UNKNOWN");
}

QString errorCodeName(quint16 code)
{
    switch (code) {
    case 0: return QString::fromUtf8(u8"未定义");
    case 1: return QString::fromUtf8(u8"文件不存在");
    case 2: return QString::fromUtf8(u8"访问被拒绝");
    case 3: return QString::fromUtf8(u8"磁盘已满");
    case 4: return QString::fromUtf8(u8"非法操作");
    case 5: return QString::fromUtf8(u8"未知传输号");
    case 6: return QString::fromUtf8(u8"文件已存在");
    case 7: return QString::fromUtf8(u8"没有此用户");
    case 8: return QString::fromUtf8(u8"选项被拒绝");
    default: return QString::fromUtf8(u8"错误 %1").arg(code);
    }
}

bool samePeer(const QHostAddress &left, const QHostAddress &right)
{
    if (left.protocol() == QAbstractSocket::IPv4Protocol
        && right.protocol() == QAbstractSocket::IPv4Protocol) {
        return left.toIPv4Address() == right.toIPv4Address();
    }
    return left.isEqual(right, QHostAddress::TolerantConversion);
}

QString safeChildPath(const QString &root, const QByteArray &relativeName, QString *errorMessage)
{
    const auto fail = [errorMessage](const QString &message) -> QString {
        if (errorMessage)
            *errorMessage = message;
        return {};
    };

    if (relativeName.isEmpty() || relativeName.contains('\0'))
        return fail(QString::fromUtf8(u8"文件名为空"));

    for (unsigned char byte : relativeName) {
        if (byte < 32)
            return fail(QString::fromUtf8(u8"文件名含有控制字符"));
    }

    QString relative = QString::fromUtf8(relativeName);
    relative.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (relative.isEmpty() || QDir::isAbsolutePath(relative))
        return fail(QString::fromUtf8(u8"拒绝绝对路径"));
#ifdef Q_OS_WIN
    if (relative.contains(QLatin1Char(':')))
        return fail(QString::fromUtf8(u8"拒绝绝对路径"));
#endif

    const QString rootCanon = QFileInfo(root).canonicalFilePath();
    if (rootCanon.isEmpty() || !QFileInfo(rootCanon).isDir())
        return fail(QString::fromUtf8(u8"根目录不存在"));

    const QString combined = QDir::cleanPath(QDir(rootCanon).filePath(relative));
    const QFileInfo info(combined);
    if (info.exists()) {
        const QString canonical = info.canonicalFilePath();
        if (canonical.isEmpty() || !isInsideRoot(rootCanon, canonical))
            return fail(QString::fromUtf8(u8"路径超出根目录"));
        return QDir::cleanPath(canonical);
    }

    const QString parentCanon = QFileInfo(info.absolutePath()).canonicalFilePath();
    if (parentCanon.isEmpty() || !isInsideRoot(rootCanon, parentCanon))
        return fail(QString::fromUtf8(u8"路径超出根目录"));
    if (!QFileInfo(parentCanon).isDir())
        return fail(QString::fromUtf8(u8"目录不存在"));
    return QDir::cleanPath(parentCanon + QLatin1Char('/') + info.fileName());
}

} // namespace TftpQt
