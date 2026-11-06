#include "FwmpPack.h"

#include "SettingsWidget.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QRadioButton>
#include <QRegularExpression>
#include <QTextCursor>
#include <QAbstractItemView>
#include <QBrush>
#include <QColor>
#include <QFont>
#include <QHeaderView>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QVector>
#include <QVBoxLayout>

namespace {

QByteArray fwmpHeader(const QByteArray &payload, quint8 imageType, quint8 target)  // 64 字节，和小端 Python struct 一致
{
    const QByteArray hash = QCryptographicHash::hash(payload, QCryptographicHash::Sha256);
    QByteArray header;
    QDataStream stream(&header, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("FWMP", 4);
    stream << quint8(1) << quint16(64) << quint8(0);
    stream << imageType << quint8(0) << quint8(0) << quint8(0) << quint16(0) << target << quint8(0);
    stream.writeRawData("\0\0\0\0", 4);
    stream << quint32(payload.size());
    stream.writeRawData(hash.constData(), hash.size());
    stream.writeRawData(QByteArray(8, '\0').constData(), 8);
    return header;
}

QString fileBeside(const QString &path, const QString &suffix)  // 同目录、同名，换成给定扩展名
{
    const QFileInfo info(path);
    if (info.fileName().isEmpty())
        return QString();
    return QDir::toNativeSeparators(info.dir().filePath(info.completeBaseName() + suffix));
}

QString imageKind(quint8 imageType)  // 0 是非安全侧，1 是安全侧，其它照数字显示
{
    if (imageType == 0)
        return QStringLiteral("NS");
    if (imageType == 1)
        return QStringLiteral("S");
    return QString::number(imageType);
}

// 签名镜像按槽补齐：S 512 KB，NS 1 MB。对不上时返回提醒文字，对得上返回空。
QString firmwareSizeWarning(quint8 imageType, qint64 actual)
{
    if (imageType == 1 && actual != qint64(512) * 1024)
        return QObject::tr("S firmware is %1 bytes, expected 512 KB").arg(actual);
    if (imageType == 0 && actual != qint64(1024) * 1024)
        return QObject::tr("NS firmware is %1 bytes, expected 1 MB").arg(actual);
    return QString();
}

} // namespace

bool readFwmpPayload(const QString &path, FwmpPayload *payload, QString *error)
{
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("Cannot read FWMP: %1").arg(path);
        return false;
    }
    const QByteArray file = input.readAll();
    input.close();
    if (file.size() < 64 || !file.startsWith("FWMP")) {
        if (error)
            *error = QObject::tr("Not an FWMP file: %1").arg(path);
        return false;
    }
    QDataStream stream(file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.skipRawData(4);
    quint8 version = 0;
    quint16 headerSize = 0;
    quint8 ignored = 0;
    stream >> version >> headerSize >> ignored;
    if (stream.status() != QDataStream::Ok || version != 1 || headerSize != 64) {
        if (error)
            *error = QObject::tr("Invalid FWMP header size: %1").arg(headerSize);
        return false;
    }
    quint8 imageType = 0;
    quint8 boardType = 0;
    quint8 reservedByte = 0;
    quint16 reservedWord = 0;
    quint8 target = 0;
    stream >> imageType >> boardType >> reservedByte >> reservedByte >> reservedWord >> target >> reservedByte;
    Q_UNUSED(boardType)
    Q_UNUSED(reservedWord)
    Q_UNUSED(target)
    stream.skipRawData(4);
    quint32 payloadSize = 0;
    stream >> payloadSize;
    QByteArray storedHash(32, 0);
    if (stream.readRawData(storedHash.data(), storedHash.size()) != storedHash.size()) {
        if (error)
            *error = QObject::tr("Not an FWMP file: %1").arg(path);
        return false;
    }
    if (quint64(headerSize) + payloadSize != quint64(file.size())) {
        if (error)
            *error = QObject::tr("FWMP length does not match the file: %1").arg(path);
        return false;
    }
    const QByteArray bytes = file.mid(headerSize, int(payloadSize));
    if (QCryptographicHash::hash(bytes, QCryptographicHash::Sha256) != storedHash) {
        if (error)
            *error = QObject::tr("FWMP SHA-256 does not match");
        return false;
    }
    if (!payload)
        return false;
    payload->bytes = bytes;
    payload->imageType = imageType;
    return true;
}

namespace {

const quint32 kImageMagic = 0x96f3b83d;
const quint16 kTlvInfoMagic = 0x6907;
const quint16 kTlvProtMagic = 0x6908;
const quint16 kTlvKeyHash = 0x01;
const quint16 kTlvPubKey = 0x02;
const quint16 kTlvSha256 = 0x10;
const quint16 kTlvEcdsa = 0x22;
const quint16 kTlvSecCnt = 0x50;

struct TlvRecord {
    quint16 type = 0;
    QByteArray data;
};

struct ImageInfo {
    bool magicOk = false;
    QString version;
    quint16 headerSize = 0;
    quint32 imageSize = 0;
    bool hasSecCnt = false;
    quint32 secCnt = 0;
    QByteArray sha256;
    QByteArray keyHash;
    QByteArray publicKey;  // TLV 0x02。没有 KEYHASH 时，用它的 SHA-256 做对比
    bool hasEcdsa = false;
};

struct PackageReport {
    QString title;
    QString path;
    QString packState;
    QString packVersion;
    QString image;
    QString payloadLength;
    QString packHash;
    QString packHashHeader;
    QString packHashComputed;
    QString signState;
    QString mcuVersion;
    QString headerSize;
    QString imageSize;
    QString secCnt;
    QString sha256;
    QString keyHash;
    QString ecdsa;
};

QString blankValue()
{
    return QStringLiteral("\u2014");
}

bool readU8(const QByteArray &bytes, int offset, quint8 *out)
{
    if (offset < 0 || offset >= bytes.size())
        return false;
    *out = quint8(bytes.at(offset));
    return true;
}

bool readU16(const QByteArray &bytes, int offset, quint16 *out)
{
    quint8 lo = 0;
    quint8 hi = 0;
    if (!readU8(bytes, offset, &lo) || !readU8(bytes, offset + 1, &hi))
        return false;
    *out = quint16(lo) | (quint16(hi) << 8);
    return true;
}

bool readU32(const QByteArray &bytes, int offset, quint32 *out)
{
    quint16 lo = 0;
    quint16 hi = 0;
    if (!readU16(bytes, offset, &lo) || !readU16(bytes, offset + 2, &hi))
        return false;
    *out = quint32(lo) | (quint32(hi) << 16);
    return true;
}

int hexNibble(char ch)
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    return -1;
}

struct HexChunk {
    quint32 addr = 0;
    QByteArray data;
};

bool collectIntelHex(const QByteArray &text, QVector<HexChunk> *chunks, quint32 *minAddr, quint32 *maxAddr)
{
    quint32 upper = 0;
    bool any = false;
    quint32 lowest = 0;
    quint32 highest = 0;
    const QList<QByteArray> lines = text.split('\n');
    for (QByteArray line : lines) {
        line = line.trimmed();
        if (line.isEmpty() || line.at(0) != ':')
            continue;
        QByteArray raw;
        unsigned sum = 0;
        for (int i = 1; i + 1 < line.size(); i += 2) {
            const int hi = hexNibble(line.at(i));
            const int lo = hexNibble(line.at(i + 1));
            if (hi < 0 || lo < 0)
                return false;
            const quint8 byte = quint8((hi << 4) | lo);
            raw.append(char(byte));
            sum += byte;
        }
        if (raw.size() < 5 || (sum & 0xff) != 0)
            return false;
        const int count = quint8(raw.at(0));
        if (raw.size() != count + 5)
            return false;
        const quint16 addr16 = (quint16(quint8(raw.at(1))) << 8) | quint8(raw.at(2));
        const quint8 type = quint8(raw.at(3));
        const QByteArray data = raw.mid(4, count);
        if (type == 0x00) {
            if (data.isEmpty())
                continue;
            const quint32 start = upper + addr16;
            const quint32 end = start + quint32(data.size() - 1);
            if (!any || start < lowest)
                lowest = start;
            if (!any || end > highest)
                highest = end;
            any = true;
            HexChunk chunk;
            chunk.addr = start;
            chunk.data = data;
            chunks->append(chunk);
        } else if (type == 0x01) {
            break;
        } else if (type == 0x02) {
            if (data.size() != 2)
                return false;
            upper = (quint32(quint8(data.at(0))) << 8 | quint8(data.at(1))) << 4;
        } else if (type == 0x04) {
            if (data.size() != 2)
                return false;
            upper = (quint32(quint8(data.at(0))) << 8 | quint8(data.at(1))) << 16;
        } else if (type == 0x03 || type == 0x05) {
            continue;
        } else {
            return false;
        }
    }
    if (!any)
        return false;
    *minAddr = lowest;
    *maxAddr = highest;
    return true;
}

bool decodeIntelHex(const QByteArray &text, QByteArray *out)  // 内存里收成 bin，不写文件
{
    QVector<HexChunk> chunks;
    quint32 minAddr = 0;
    quint32 maxAddr = 0;
    if (!collectIntelHex(text, &chunks, &minAddr, &maxAddr))
        return false;
    const quint64 span = quint64(maxAddr) - minAddr + 1;
    if (span == 0 || span > quint64(16) * 1024 * 1024)
        return false;
    QByteArray binary(int(span), char(0xff));
    for (const HexChunk &chunk : chunks) {
        const int at = int(chunk.addr - minAddr);
        if (at < 0 || at + chunk.data.size() > binary.size())
            return false;
        binary.replace(at, chunk.data.size(), chunk.data);
    }
    *out = binary;
    return true;
}

bool readTlvs(const QByteArray &bytes, int offset, quint16 expectMagic, QVector<TlvRecord> *records, int *next)
{
    quint16 magic = 0;
    quint16 total = 0;
    if (!readU16(bytes, offset, &magic) || magic != expectMagic)
        return false;
    if (!readU16(bytes, offset + 2, &total) || total < 4)
        return false;
    if (quint64(offset) + total > quint64(bytes.size()))
        return false;
    const int end = offset + int(total);
    int cursor = offset + 4;
    while (cursor < end) {
        quint16 type = 0;
        quint16 length = 0;
        if (cursor + 4 > end || !readU16(bytes, cursor, &type) || !readU16(bytes, cursor + 2, &length))
            return false;
        cursor += 4;
        if (cursor + int(length) > end)
            return false;
        TlvRecord record;
        record.type = type;
        record.data = bytes.mid(cursor, length);
        records->append(record);
        cursor += int(length);
    }
    *next = end;
    return true;
}

ImageInfo parseMcuBoot(const QByteArray &bytes)
{
    ImageInfo info;
    quint32 magic = 0;
    quint32 loadAddr = 0;
    quint16 headerSize = 0;
    quint16 protectedSize = 0;
    quint32 imageSize = 0;
    quint32 flags = 0;
    quint8 major = 0;
    quint8 minor = 0;
    quint16 revision = 0;
    quint32 build = 0;
    if (!readU32(bytes, 0, &magic) || magic != kImageMagic)
        return info;
    if (!readU32(bytes, 4, &loadAddr) || !readU16(bytes, 8, &headerSize) || !readU16(bytes, 10, &protectedSize)
        || !readU32(bytes, 12, &imageSize) || !readU32(bytes, 16, &flags)
        || !readU8(bytes, 20, &major) || !readU8(bytes, 21, &minor)
        || !readU16(bytes, 22, &revision) || !readU32(bytes, 24, &build))
        return info;
    Q_UNUSED(loadAddr)
    Q_UNUSED(protectedSize)
    Q_UNUSED(flags)
    if (headerSize < 32)
        return info;
    info.magicOk = true;
    info.version = QStringLiteral("%1.%2.%3+%4").arg(major).arg(minor).arg(revision).arg(build);
    info.headerSize = headerSize;
    info.imageSize = imageSize;

    const quint64 tlvOffset = quint64(headerSize) + imageSize;
    if (tlvOffset > quint64(bytes.size()))
        return info;
    int offset = int(tlvOffset);
    quint16 areaMagic = 0;
    if (!readU16(bytes, offset, &areaMagic))
        return info;
    if (areaMagic == kTlvProtMagic) {
        QVector<TlvRecord> protectedRecords;
        int next = offset;
        if (!readTlvs(bytes, offset, kTlvProtMagic, &protectedRecords, &next))
            return info;
        for (const TlvRecord &record : protectedRecords) {
            if (record.type == kTlvSecCnt && record.data.size() >= 1 && record.data.size() <= 4) {
                quint32 value = 0;
                for (int i = 0; i < record.data.size(); ++i)
                    value |= quint32(quint8(record.data.at(i))) << (8 * i);
                info.hasSecCnt = true;
                info.secCnt = value;
            }
        }
        offset = next;
        if (!readU16(bytes, offset, &areaMagic))
            return info;
    }
    if (areaMagic != kTlvInfoMagic)
        return info;
    QVector<TlvRecord> records;
    int next = offset;
    if (!readTlvs(bytes, offset, kTlvInfoMagic, &records, &next))
        return info;
    for (const TlvRecord &record : records) {
        if (record.type == kTlvSha256)
            info.sha256 = record.data;
        else if (record.type == kTlvKeyHash)
            info.keyHash = record.data;
        else if (record.type == kTlvPubKey)
            info.publicKey = record.data;
        else if (record.type == kTlvEcdsa)
            info.hasEcdsa = true;
    }
    return info;
}

QByteArray loadImageBytes(const QString &path, const QByteArray &file)  // hex 先转成 bin，后面的对比都用这份
{
    const bool hexName = path.endsWith(QLatin1String(".hex"), Qt::CaseInsensitive);
    const QByteArray trimmed = file.trimmed();
    const bool looksHex = !trimmed.isEmpty() && trimmed.at(0) == ':';
    if (!hexName && !looksHex)
        return file;
    QByteArray decoded;
    if (!decodeIntelHex(file, &decoded))
        return QByteArray();
    return decoded;
}

PackageReport inspectOne(const QString &path)  // 只以 ReadOnly 打开
{
    PackageReport report;
    report.path = path;
    report.title = QFileInfo(path).fileName();
    const QString blank = blankValue();
    report.packState = QObject::tr("Not packed");
    report.packVersion = blank;
    report.image = blank;
    report.payloadLength = blank;
    report.packHash = blank;
    report.packHashHeader = blank;
    report.packHashComputed = blank;
    report.signState = QObject::tr("Not signed");
    report.mcuVersion = blank;
    report.headerSize = blank;
    report.imageSize = blank;
    report.secCnt = blank;
    report.sha256 = blank;
    report.keyHash = blank;
    report.ecdsa = QObject::tr("ECDSA absent");

    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) {
        report.packState = QObject::tr("Cannot read");
        return report;
    }
    const QByteArray file = input.readAll();
    input.close();
    const QByteArray bytes = loadImageBytes(path, file);

    QByteArray image;
    if (bytes.size() >= 4 && bytes.startsWith("FWMP")) {
        report.packState = QObject::tr("Packed");
        quint8 version = 0;
        quint16 headerSize = 0;
        quint8 imageType = 0;
        quint32 payloadSize = 0;
        const bool headerRead = bytes.size() >= 64
            && readU8(bytes, 4, &version)
            && readU16(bytes, 5, &headerSize)
            && readU8(bytes, 8, &imageType)
            && readU32(bytes, 20, &payloadSize);
        if (headerRead) {
            report.packVersion = QString::number(version);
            report.image = imageKind(imageType);
            report.payloadLength = QString::number(payloadSize);
        }
        const QByteArray storedHash = bytes.mid(24, 32);
        const bool lengthOk = headerRead && version == 1 && headerSize == 64 && storedHash.size() == 32
            && quint64(headerSize) + payloadSize == quint64(bytes.size());
        if (!lengthOk) {
            report.packState = QObject::tr("Invalid header");
        } else {
            image = bytes.mid(int(headerSize), int(payloadSize));
            const QByteArray computedHash = QCryptographicHash::hash(image, QCryptographicHash::Sha256);
            report.packHashHeader = QString::fromLatin1(storedHash.toHex());
            report.packHashComputed = QString::fromLatin1(computedHash.toHex());
            report.packHash = computedHash == storedHash
                ? QObject::tr("SHA-256 matches")
                : QObject::tr("SHA-256 does not match");
        }
    } else {
        image = bytes;
    }

    const ImageInfo info = parseMcuBoot(image);
    if (!info.magicOk)
        return report;
    report.mcuVersion = info.version;
    report.headerSize = QString::number(info.headerSize);
    report.imageSize = QString::number(info.imageSize);
    if (info.hasSecCnt)
        report.secCnt = QString::number(info.secCnt);
    if (!info.sha256.isEmpty())
        report.sha256 = QString::fromLatin1(info.sha256.toHex());
    if (!info.keyHash.isEmpty())
        report.keyHash = QString::fromLatin1(info.keyHash.toHex());
    else if (!info.publicKey.isEmpty())
        report.keyHash = QString::fromLatin1(QCryptographicHash::hash(info.publicKey, QCryptographicHash::Sha256).toHex());
    report.ecdsa = info.hasEcdsa ? QObject::tr("ECDSA present") : QObject::tr("ECDSA absent");
    report.signState = info.hasEcdsa ? QObject::tr("Signed") : QObject::tr("Not signed");
    return report;
}

enum CompareRow {
    RowPackSection,
    RowPackState,
    RowPackVersion,
    RowImage,
    RowPayload,
    RowPackHash,
    RowPackHashHeader,
    RowPackHashComputed,
    RowSignSection,
    RowSignState,
    RowMcuVersion,
    RowHeaderSize,
    RowImageSize,
    RowSecCnt,
    RowSha256,
    RowKeyHash,
    RowEcdsa,
    RowCount
};

bool compareRowIsSection(int row)
{
    return row == RowPackSection || row == RowSignSection;
}

QString compareRowName(int row)
{
    switch (row) {
    case RowPackSection: return QObject::tr("Package information");
    case RowPackState: return QObject::tr("Pack status");
    case RowPackVersion: return QObject::tr("Pack version");
    case RowImage: return QObject::tr("Image side");
    case RowPayload: return QObject::tr("Payload length");
    case RowPackHash: return QObject::tr("SHA-256");
    case RowPackHashHeader: return QObject::tr("Header SHA-256");
    case RowPackHashComputed: return QObject::tr("Computed SHA-256");
    case RowSignSection: return QObject::tr("Signature information");
    case RowSignState: return QObject::tr("Signature");
    case RowMcuVersion: return QObject::tr("MCUboot version");
    case RowHeaderSize: return QObject::tr("Header size");
    case RowImageSize: return QObject::tr("Image size");
    case RowSecCnt: return QObject::tr("Security counter");
    case RowSha256: return QObject::tr("Image SHA-256");
    case RowKeyHash: return QObject::tr("Public key hash");
    case RowEcdsa: return QObject::tr("ECDSA signature");
    default: return QString();
    }
}

QString compareRowValue(const PackageReport &report, int row)
{
    switch (row) {
    case RowPackState: return report.packState;
    case RowPackVersion: return report.packVersion;
    case RowImage: return report.image;
    case RowPayload: return report.payloadLength;
    case RowPackHash: return report.packHash;
    case RowPackHashHeader: return report.packHashHeader;
    case RowPackHashComputed: return report.packHashComputed;
    case RowSignState: return report.signState;
    case RowMcuVersion: return report.mcuVersion;
    case RowHeaderSize: return report.headerSize;
    case RowImageSize: return report.imageSize;
    case RowSecCnt: return report.secCnt;
    case RowSha256: return report.sha256;
    case RowKeyHash: return report.keyHash;
    case RowEcdsa: return report.ecdsa;
    default: return QString();
    }
}

QString columnTitle(const QString &path, const QStringList &paths)
{
    const QFileInfo info(path);
    int same = 0;
    for (const QString &other : paths) {
        if (QFileInfo(other).fileName().compare(info.fileName(), Qt::CaseInsensitive) == 0)
            ++same;
    }
    if (same > 1)
        return info.dir().dirName() + QLatin1Char('/') + info.fileName();
    return info.fileName();
}

struct RestoreParts {
    QByteArray unpacked;
    bool packed = false;
    QString fatalError;
    QByteArray raw;
    bool haveRaw = false;
    QString rawError;
};

RestoreParts splitRestore(const QString &path)
{
    RestoreParts parts;
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) {
        parts.fatalError = QObject::tr("Cannot read");
        return parts;
    }
    const QByteArray file = input.readAll();
    input.close();
    const bool hexName = path.endsWith(QLatin1String(".hex"), Qt::CaseInsensitive);
    const QByteArray trimmed = file.trimmed();
    const bool looksHex = !trimmed.isEmpty() && trimmed.at(0) == ':';
    const QByteArray bytes = loadImageBytes(path, file);
    if ((hexName || looksHex) && bytes.isEmpty()) {
        parts.fatalError = QObject::tr("Hex could not be decoded.");
        return parts;
    }
    QByteArray image = bytes;
    if (bytes.size() >= 4 && bytes.startsWith("FWMP")) {
        quint8 version = 0;
        quint16 headerSize = 0;
        quint32 payloadSize = 0;
        const bool headerRead = bytes.size() >= 64
            && readU8(bytes, 4, &version)
            && readU16(bytes, 5, &headerSize)
            && readU32(bytes, 20, &payloadSize);
        const QByteArray storedHash = bytes.mid(24, 32);
        const bool lengthOk = headerRead && version == 1 && headerSize == 64 && storedHash.size() == 32
            && quint64(headerSize) + payloadSize == quint64(bytes.size());
        if (!lengthOk) {
            parts.fatalError = QObject::tr("Invalid header");
            return parts;
        }
        image = bytes.mid(int(headerSize), int(payloadSize));
        if (QCryptographicHash::hash(image, QCryptographicHash::Sha256) != storedHash) {
            parts.fatalError = QObject::tr("FWMP SHA-256 does not match");
            return parts;
        }
        parts.packed = true;
        parts.unpacked = image;
    }
    const ImageInfo info = parseMcuBoot(image);
    if (!info.magicOk) {
        parts.rawError = QObject::tr("Not signed");
        return parts;
    }
    const quint64 end = quint64(info.headerSize) + info.imageSize;
    if (info.headerSize < 32 || end > quint64(image.size())) {
        parts.rawError = QObject::tr("Image size exceeds the file.");
        return parts;
    }
    parts.raw = image.mid(int(info.headerSize), int(info.imageSize));
    parts.haveRaw = true;
    return parts;
}

bool writeBinTo(const QString &source, const QString &out, const QByteArray &bytes, QString *written, QString *error)
{
    if (QFileInfo(out).absoluteFilePath().compare(QFileInfo(source).absoluteFilePath(), Qt::CaseInsensitive) == 0) {
        if (error)
            *error = QObject::tr("Cannot write BIN: %1").arg(QDir::toNativeSeparators(out));
        return false;
    }
    QFile output(out);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size()) {
        if (error)
            *error = QObject::tr("Cannot write %1: %2").arg(QDir::toNativeSeparators(out), output.errorString());
        return false;
    }
    output.close();
    if (written)
        *written = QDir::toNativeSeparators(QFileInfo(out).absoluteFilePath());
    return true;
}

} // namespace

PackageInspectDialog::PackageInspectDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowFlags(Qt::Window
                   | Qt::WindowTitleHint
                   | Qt::WindowSystemMenuHint
                   | Qt::WindowMinimizeButtonHint
                   | Qt::WindowMaximizeButtonHint
                   | Qt::WindowCloseButtonHint);
    resize(980, 640);
    auto *layout = new QVBoxLayout(this);
    auto *buttons = new QHBoxLayout();
    m_add = new QPushButton(this);
    m_remove = new QPushButton(this);
    m_compare = new QPushButton(this);
    buttons->addWidget(m_add);
    buttons->addWidget(m_remove);
    buttons->addWidget(m_compare);
    buttons->addStretch();
    layout->addLayout(buttons);

    m_files = new QListWidget(this);
    m_files->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_files->setMaximumHeight(140);
    layout->addWidget(m_files);

    m_table = new QTableWidget(this);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setWordWrap(false);
    m_table->setTextElideMode(Qt::ElideNone);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    layout->addWidget(m_table, 1);

    m_note = new QLabel(this);
    m_note->setWordWrap(true);
    layout->addWidget(m_note);

    connect(m_add, &QPushButton::clicked, this, &PackageInspectDialog::addFiles);
    connect(m_remove, &QPushButton::clicked, this, &PackageInspectDialog::removeFiles);
    connect(m_compare, &QPushButton::clicked, this, &PackageInspectDialog::compare);
    retranslateUi();
    loadPaths();
}

void PackageInspectDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QDialog::changeEvent(event);
}

void PackageInspectDialog::retranslateUi()
{
    setWindowTitle(tr("Inspect package"));
    m_add->setText(tr("Add files"));
    m_remove->setText(tr("Remove selected"));
    m_compare->setText(tr("Update"));
    if (m_table->columnCount() > 0)
        compare();
    else
        m_note->setText(tr("Read only. The file was not modified."));
}

void PackageInspectDialog::addFiles()
{
    QString start = QFileInfo(SettingsWidget::firmwareFilePath(QStringLiteral("firmware.bin"))).absolutePath();
    if (m_files->count() > 0) {
        const QString last = m_files->item(m_files->count() - 1)->text();
        const QString dir = QFileInfo(last).absolutePath();
        if (!dir.isEmpty())
            start = dir;
    }
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Inspect package"), start,
        tr("Firmware (*.bin *.fwmp *.hex);;All files (*)"));
    bool added = false;
    for (const QString &file : files) {
        const QString native = QDir::toNativeSeparators(file);
        bool exists = false;
        for (int i = 0; i < m_files->count(); ++i) {
            if (m_files->item(i)->text().compare(native, Qt::CaseInsensitive) == 0) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            m_files->addItem(native);
            added = true;
        }
    }
    if (added) {
        savePaths();
        compare();
    }
}

void PackageInspectDialog::removeFiles()
{
    const QList<QListWidgetItem *> selected = m_files->selectedItems();
    if (selected.isEmpty())
        return;
    for (QListWidgetItem *item : selected)
        delete item;
    savePaths();
    compare();
}

void PackageInspectDialog::compare()
{
    QStringList paths;
    for (int i = 0; i < m_files->count(); ++i)
        paths << m_files->item(i)->text();
    if (paths.isEmpty()) {
        m_table->clear();
        m_table->setRowCount(0);
        m_table->setColumnCount(0);
        m_note->setText(tr("Add a firmware file first.") + QLatin1Char('\n')
                        + tr("Read only. The file was not modified."));
        return;
    }

    QVector<PackageReport> reports;
    reports.reserve(paths.size());
    for (const QString &path : paths)
        reports.append(inspectOne(path));

    m_table->clear();
    m_table->setColumnCount(reports.size());
    m_table->setRowCount(RowCount);
    for (int column = 0; column < reports.size(); ++column) {
        auto *header = new QTableWidgetItem(columnTitle(reports.at(column).path, paths));
        header->setToolTip(QDir::toNativeSeparators(reports.at(column).path));
        m_table->setHorizontalHeaderItem(column, header);
    }
    const QColor sectionColor(236, 239, 241);
    const QColor diffColor(255, 224, 130);
    for (int row = 0; row < RowCount; ++row) {
        auto *name = new QTableWidgetItem(compareRowName(row));
        if (compareRowIsSection(row)) {
            QFont font = name->font();
            font.setBold(true);
            name->setFont(font);
        }
        m_table->setVerticalHeaderItem(row, name);
        bool differ = false;
        if (!compareRowIsSection(row) && reports.size() > 1) {
            const QString first = compareRowValue(reports.at(0), row);
            for (int column = 1; column < reports.size(); ++column) {
                if (compareRowValue(reports.at(column), row) != first) {
                    differ = true;
                    break;
                }
            }
        }
        for (int column = 0; column < reports.size(); ++column) {
            auto *cell = new QTableWidgetItem(compareRowIsSection(row) ? QString() : compareRowValue(reports.at(column), row));
            cell->setFlags(Qt::ItemIsEnabled);
            if (compareRowIsSection(row))
                cell->setBackground(QBrush(sectionColor));
            else if (differ)
                cell->setBackground(QBrush(diffColor));
            m_table->setItem(row, column, cell);
        }
    }
    m_note->setText(tr("Read only. The file was not modified."));
}

void PackageInspectDialog::loadPaths()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("ProgramDownload"));
    const QStringList files = settings.value(QStringLiteral("inspectFiles")).toStringList();
    settings.endGroup();
    for (const QString &file : files) {
        const QString native = QDir::toNativeSeparators(file.trimmed());
        if (!native.isEmpty())
            m_files->addItem(native);
    }
    if (m_files->count() > 0)
        compare();
}

void PackageInspectDialog::savePaths() const
{
    QStringList files;
    for (int i = 0; i < m_files->count(); ++i)
        files << m_files->item(i)->text();
    QSettings settings;
    settings.beginGroup(QStringLiteral("ProgramDownload"));
    settings.setValue(QStringLiteral("inspectFiles"), files);
    settings.endGroup();
}

FirmwareRestoreDialog::FirmwareRestoreDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowFlags(Qt::Window
                   | Qt::WindowTitleHint
                   | Qt::WindowSystemMenuHint
                   | Qt::WindowMinimizeButtonHint
                   | Qt::WindowMaximizeButtonHint
                   | Qt::WindowCloseButtonHint);
    resize(720, 560);
    auto *layout = new QVBoxLayout(this);
    auto *fileRow = new QHBoxLayout();
    m_choose = new QPushButton(this);
    m_path = new QLabel(this);
    m_path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_path->setWordWrap(true);
    fileRow->addWidget(m_choose);
    fileRow->addWidget(m_path, 1);
    layout->addLayout(fileRow);

    m_table = new QTableWidget(this);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setWordWrap(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    layout->addWidget(m_table, 1);

    auto addOutRow = [this, layout](QLabel **label, QLineEdit **edit, QPushButton **browse) {
        auto *row = new QHBoxLayout();
        *label = new QLabel(this);
        *edit = new QLineEdit(this);
        *browse = new QPushButton(this);
        row->addWidget(*label);
        row->addWidget(*edit, 1);
        row->addWidget(*browse);
        layout->addLayout(row);
    };
    addOutRow(&m_unpackedOutLabel, &m_unpackedOut, &m_unpackedBrowse);
    addOutRow(&m_unsignedOutLabel, &m_unsignedOut, &m_unsignedBrowse);

    auto *actions = new QHBoxLayout();
    m_unpacked = new QPushButton(this);
    m_unsigned = new QPushButton(this);
    actions->addWidget(m_unpacked);
    actions->addWidget(m_unsigned);
    actions->addStretch();
    layout->addLayout(actions);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    connect(m_choose, &QPushButton::clicked, this, &FirmwareRestoreDialog::chooseFile);
    connect(m_unpackedBrowse, &QPushButton::clicked, this, &FirmwareRestoreDialog::browseUnpacked);
    connect(m_unsignedBrowse, &QPushButton::clicked, this, &FirmwareRestoreDialog::browseUnsigned);
    connect(m_unpacked, &QPushButton::clicked, this, &FirmwareRestoreDialog::writeUnpacked);
    connect(m_unsigned, &QPushButton::clicked, this, &FirmwareRestoreDialog::writeUnsigned);
    connect(m_unpackedOut, &QLineEdit::editingFinished, this, &FirmwareRestoreDialog::savePaths);
    connect(m_unsignedOut, &QLineEdit::editingFinished, this, &FirmwareRestoreDialog::savePaths);
    loadPaths();
    retranslateUi();
}

void FirmwareRestoreDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QDialog::changeEvent(event);
}

void FirmwareRestoreDialog::retranslateUi()
{
    setWindowTitle(tr("Restore firmware"));
    m_choose->setText(tr("Select firmware"));
    m_unpackedOutLabel->setText(tr("Unpacked output:"));
    m_unsignedOutLabel->setText(tr("Unsigned output:"));
    m_unpackedBrowse->setText(tr("Select path"));
    m_unsignedBrowse->setText(tr("Select path"));
    m_unpacked->setText(tr("Output unpacked firmware"));
    m_unsigned->setText(tr("Output unsigned firmware"));
    if (m_file.isEmpty())
        m_status->setText(tr("Select a firmware file."));
    else
        refresh();
}

void FirmwareRestoreDialog::chooseFile()
{
    QString start = m_file.isEmpty()
        ? QFileInfo(SettingsWidget::firmwareFilePath(QStringLiteral("firmware.bin"))).absolutePath()
        : QFileInfo(m_file).absolutePath();
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Restore firmware"), start,
        tr("Firmware (*.bin *.fwmp *.hex);;All files (*)"));
    if (file.isEmpty())
        return;
    m_file = QDir::toNativeSeparators(file);
    m_path->setText(m_file);
    fillOutputPaths();
    savePaths();
    refresh();
}

void FirmwareRestoreDialog::loadPaths()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("ProgramDownload"));
    const QString file = settings.value(QStringLiteral("restoreFile")).toString().trimmed();
    const QString unpacked = settings.value(QStringLiteral("restoreUnpacked")).toString();
    const QString unsignedPath = settings.value(QStringLiteral("restoreUnsigned")).toString();
    settings.endGroup();
    if (file.isEmpty())
        return;
    m_file = QDir::toNativeSeparators(file);
    m_path->setText(m_file);
    m_unpackedOut->setText(unpacked);
    m_unsignedOut->setText(unsignedPath);
    if (m_unpackedOut->text().trimmed().isEmpty())
        m_unpackedOut->setText(fileBeside(m_file, QStringLiteral("_unpacked.bin")));
    if (m_unsignedOut->text().trimmed().isEmpty())
        m_unsignedOut->setText(fileBeside(m_file, QStringLiteral("_unsigned.bin")));
}

void FirmwareRestoreDialog::savePaths() const
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("ProgramDownload"));
    settings.setValue(QStringLiteral("restoreFile"), m_file);
    settings.setValue(QStringLiteral("restoreUnpacked"), m_unpackedOut->text().trimmed());
    settings.setValue(QStringLiteral("restoreUnsigned"), m_unsignedOut->text().trimmed());
    settings.endGroup();
}

void FirmwareRestoreDialog::fillOutputPaths()
{
    if (m_file.isEmpty())
        return;
    m_unpackedOut->setText(fileBeside(m_file, QStringLiteral("_unpacked.bin")));
    m_unsignedOut->setText(fileBeside(m_file, QStringLiteral("_unsigned.bin")));
}

void FirmwareRestoreDialog::browseUnpacked()
{
    QString suggested = m_unpackedOut->text().trimmed();
    if (suggested.isEmpty() && !m_file.isEmpty())
        suggested = fileBeside(m_file, QStringLiteral("_unpacked.bin"));
    const QString chosen = askSavePath(suggested);
    if (chosen.isEmpty())
        return;
    m_unpackedOut->setText(chosen);
    savePaths();
}

void FirmwareRestoreDialog::browseUnsigned()
{
    QString suggested = m_unsignedOut->text().trimmed();
    if (suggested.isEmpty() && !m_file.isEmpty())
        suggested = fileBeside(m_file, QStringLiteral("_unsigned.bin"));
    const QString chosen = askSavePath(suggested);
    if (chosen.isEmpty())
        return;
    m_unsignedOut->setText(chosen);
    savePaths();
}

void FirmwareRestoreDialog::refresh()
{
    if (m_file.isEmpty()) {
        m_table->clear();
        m_table->setRowCount(0);
        m_table->setColumnCount(0);
        m_status->setText(tr("Select a firmware file."));
        return;
    }
    const PackageReport report = inspectOne(m_file);
    m_table->clear();
    m_table->setColumnCount(1);
    m_table->setRowCount(RowCount);
    m_table->setHorizontalHeaderLabels(QStringList() << report.title);
    const QColor sectionColor(236, 239, 241);
    for (int row = 0; row < RowCount; ++row) {
        auto *name = new QTableWidgetItem(compareRowName(row));
        name->setFlags(Qt::ItemIsEnabled);
        auto *value = new QTableWidgetItem(compareRowIsSection(row) ? QString() : compareRowValue(report, row));
        value->setFlags(Qt::ItemIsEnabled);
        if (compareRowIsSection(row)) {
            QFont font = name->font();
            font.setBold(true);
            name->setFont(font);
            name->setBackground(QBrush(sectionColor));
            value->setBackground(QBrush(sectionColor));
        }
        m_table->setVerticalHeaderItem(row, name);
        m_table->setItem(row, 0, value);
    }
    m_status->setText(tr("Read only. The file was not modified."));
}

QString FirmwareRestoreDialog::askSavePath(const QString &suggested)
{
    const QFileInfo info(QDir::fromNativeSeparators(suggested));
    QFileDialog dialog(this, tr("Restore firmware"), info.absolutePath(),
                       tr("BIN Files (*.bin);;All Files (*)"));
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setDefaultSuffix(QStringLiteral("bin"));
    if (!info.fileName().isEmpty())
        dialog.selectFile(info.fileName());
    if (dialog.exec() != QDialog::Accepted)
        return QString();
    const QStringList files = dialog.selectedFiles();
    return files.isEmpty() ? QString() : QDir::toNativeSeparators(files.first());
}

void FirmwareRestoreDialog::writeUnpacked()
{
    if (m_file.isEmpty()) {
        m_status->setText(tr("Select a firmware file."));
        return;
    }
    refresh();
    const RestoreParts parts = splitRestore(m_file);
    if (!parts.fatalError.isEmpty()) {
        m_status->setText(parts.fatalError);
        QMessageBox::warning(this, tr("Restore firmware"), parts.fatalError);
        return;
    }
    if (!parts.packed) {
        const QString message = tr("Firmware is not packed.");
        m_status->setText(message);
        QMessageBox::warning(this, tr("Restore firmware"), message);
        return;
    }
    const QString chosen = m_unpackedOut->text().trimmed();
    if (chosen.isEmpty()) {
        const QString message = tr("Select an output path.");
        m_status->setText(message);
        QMessageBox::warning(this, tr("Restore firmware"), message);
        return;
    }
    QString written;
    QString error;
    if (!writeBinTo(m_file, chosen, parts.unpacked, &written, &error)) {
        m_status->setText(error);
        QMessageBox::warning(this, tr("Restore firmware"), error);
        return;
    }
    const QString message = tr("Wrote %1").arg(written);
    m_status->setText(message);
    QMessageBox::information(this, tr("Restore firmware"), message);
}

void FirmwareRestoreDialog::writeUnsigned()
{
    if (m_file.isEmpty()) {
        m_status->setText(tr("Select a firmware file."));
        return;
    }
    refresh();
    const RestoreParts parts = splitRestore(m_file);
    if (!parts.fatalError.isEmpty()) {
        m_status->setText(parts.fatalError);
        QMessageBox::warning(this, tr("Restore firmware"), parts.fatalError);
        return;
    }
    const QString unpackedPath = m_unpackedOut->text().trimmed();
    const QString unsignedPath = m_unsignedOut->text().trimmed();
    if (parts.packed && unpackedPath.isEmpty()) {
        const QString message = tr("Select an output path.");
        m_status->setText(message);
        QMessageBox::warning(this, tr("Restore firmware"), message);
        return;
    }
    if (parts.haveRaw && unsignedPath.isEmpty()) {
        const QString message = tr("Select an output path.");
        m_status->setText(message);
        QMessageBox::warning(this, tr("Restore firmware"), message);
        return;
    }
    if (parts.packed && parts.haveRaw
        && QFileInfo(unpackedPath).absoluteFilePath().compare(QFileInfo(unsignedPath).absoluteFilePath(), Qt::CaseInsensitive) == 0) {
        const QString message = tr("Cannot write BIN: %1").arg(unsignedPath);
        m_status->setText(message);
        QMessageBox::warning(this, tr("Restore firmware"), message);
        return;
    }
    QString unpackedWritten;
    if (parts.packed) {
        QString error;
        if (!writeBinTo(m_file, unpackedPath, parts.unpacked, &unpackedWritten, &error)) {
            m_status->setText(error);
            QMessageBox::warning(this, tr("Restore firmware"), error);
            return;
        }
    }
    if (!parts.haveRaw) {
        const QString reason = parts.rawError.isEmpty() ? tr("Not signed") : parts.rawError;
        QString message = reason;
        if (!unpackedWritten.isEmpty())
            message = tr("Wrote %1").arg(unpackedWritten) + QLatin1Char('\n') + reason;
        m_status->setText(message);
        QMessageBox::warning(this, tr("Restore firmware"), message);
        return;
    }
    const QString chosen = unsignedPath;
    if (!unpackedWritten.isEmpty()
        && QFileInfo(chosen).absoluteFilePath().compare(QFileInfo(unpackedWritten).absoluteFilePath(), Qt::CaseInsensitive) == 0) {
        const QString message = tr("Cannot write BIN: %1").arg(chosen);
        m_status->setText(tr("Wrote %1").arg(unpackedWritten) + QLatin1Char('\n') + message);
        QMessageBox::warning(this, tr("Restore firmware"), message);
        return;
    }
    QString written;
    QString error;
    if (!writeBinTo(m_file, chosen, parts.raw, &written, &error)) {
        QString message = error;
        if (!unpackedWritten.isEmpty())
            message = tr("Wrote %1").arg(unpackedWritten) + QLatin1Char('\n') + error;
        m_status->setText(message);
        QMessageBox::warning(this, tr("Restore firmware"), message);
        return;
    }
    QString message = tr("Wrote %1").arg(written);
    if (!unpackedWritten.isEmpty())
        message = tr("Wrote %1").arg(unpackedWritten) + QLatin1Char('\n') + message;
    m_status->setText(message);
    QMessageBox::information(this, tr("Restore firmware"), message);
}

namespace {

struct SignProduct {
    const char *name;
    bool h573;
    const char *devProfile;
    const char *prodProfile;
};

const SignProduct kSignProducts[] = {
    {"G0", true, "DEV-CCU", "G0-V2G"},
    {"RAT", true, "DEV-CCU", "RAT-V2G"},
    {"RD0047", true, "DEV-CCU", "RD0047-V2G"},
    {"RD0050", true, "DEV-CCU", "RD0047-V2G"},
    {"EAW", true, "DEV-CCU", "RD0047-V2G"},
    {"SECC", false, "DEV-SECC", "SECC-AC-1"},
};

QString signAppDir()
{
    return QDir(QCoreApplication::applicationDirPath()).absolutePath();
}

QString signAcsfParent()
{
    const QString inside = QStringLiteral("G0/G0-test/windows-tfm-tools/flash_stm32h573.bat");
    const QString marker = QStringLiteral("ACSF/") + inside;
    if (QFileInfo::exists(QDir(signAppDir()).absoluteFilePath(marker)))
        return signAppDir();
#ifdef ACSF_DIR
    const QString compiled = QDir::cleanPath(QString::fromUtf8(ACSF_DIR));
    if (QFileInfo::exists(QDir(compiled).filePath(inside)))
        return QFileInfo(compiled).dir().absolutePath();
#endif
    return QString();
}

QString kitWithSignBat(const QString &root)
{
    if (root.isEmpty())
        return QString();
    const QString bat = QDir(root).filePath(QStringLiteral("sign_kit/sign.bat"));
    return QFileInfo::exists(bat) ? QFileInfo(bat).absolutePath() : QString();
}

QString signConfigValue(const QString &filePath, const QString &key)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        if (line.left(eq).trimmed().compare(key, Qt::CaseInsensitive) != 0)
            continue;
        QString value = line.mid(eq + 1).trimmed();
        const int hash = value.indexOf(QLatin1Char('#'));
        if (hash >= 0)
            value = value.left(hash).trimmed();
        return value;
    }
    return QString();
}

bool signVersionOk(const QString &version)
{
    return QRegularExpression(QStringLiteral("^\\d+\\.\\d+\\.\\d+(?:\\+\\d+)?$")).match(version).hasMatch();
}

QString signKitDirectory()
{
    const QString beside = kitWithSignBat(signAppDir());
    if (!beside.isEmpty())
        return beside;
    const QString packaged = kitWithSignBat(signAcsfParent());
    if (!packaged.isEmpty())
        return packaged;
    const QString fallback = QStringLiteral("C:/Users/13584/Desktop/stm32h5xx/tool/qt_tool/sign_kit/sign.bat");
    return QFileInfo::exists(fallback) ? QFileInfo(fallback).absolutePath() : QString();
}

enum class SignPackKind {
    Unreadable,
    BadHex,
    InvalidPack,
    Packed,
    Signed,
    Unsigned
};

struct SignPackImage {
    SignPackKind kind = SignPackKind::Unreadable;
    QByteArray bytes;
    bool fileIsBin = false;
};

SignPackImage classifySignPack(const QString &path)
{
    SignPackImage image;
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly))
        return image;
    const QByteArray file = input.readAll();
    input.close();
    const bool hexName = path.endsWith(QLatin1String(".hex"), Qt::CaseInsensitive);
    const QByteArray trimmed = file.trimmed();
    const bool looksHex = !trimmed.isEmpty() && trimmed.at(0) == ':';
    const QByteArray bytes = loadImageBytes(path, file);
    if ((hexName || looksHex) && bytes.isEmpty()) {
        image.kind = SignPackKind::BadHex;
        return image;
    }
    image.fileIsBin = !hexName && !looksHex;
    if (bytes.size() >= 4 && bytes.startsWith("FWMP")) {
        quint8 version = 0;
        quint16 headerSize = 0;
        quint32 payloadSize = 0;
        const bool headerRead = bytes.size() >= 64
            && readU8(bytes, 4, &version)
            && readU16(bytes, 5, &headerSize)
            && readU32(bytes, 20, &payloadSize);
        const QByteArray storedHash = bytes.mid(24, 32);
        const bool lengthOk = headerRead && version == 1 && headerSize == 64 && storedHash.size() == 32
            && quint64(headerSize) + payloadSize == quint64(bytes.size());
        if (!lengthOk) {
            image.kind = SignPackKind::InvalidPack;
            return image;
        }
        const QByteArray payload = bytes.mid(int(headerSize), int(payloadSize));
        if (QCryptographicHash::hash(payload, QCryptographicHash::Sha256) != storedHash) {
            image.kind = SignPackKind::InvalidPack;
            return image;
        }
        image.kind = SignPackKind::Packed;
        image.bytes = payload;
        return image;
    }
    image.bytes = bytes;
    image.kind = parseMcuBoot(bytes).magicOk ? SignPackKind::Signed : SignPackKind::Unsigned;
    return image;
}

bool sameFile(const QString &left, const QString &right)
{
    return QFileInfo(left).absoluteFilePath().compare(QFileInfo(right).absoluteFilePath(), Qt::CaseInsensitive) == 0;
}

bool moveToPath(const QString &from, const QString &to, QString *error)
{
    if (sameFile(from, to))
        return true;
    if (QFile::exists(to))
        QFile::remove(to);
    if (QFile::rename(from, to))
        return true;
    if (!QFile::copy(from, to)) {
        if (error)
            *error = QObject::tr("Cannot write %1: %2").arg(QDir::toNativeSeparators(to), QFile(to).errorString());
        return false;
    }
    QFile::remove(from);
    return true;
}

} // namespace

SignPackDialog::SignPackDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowFlags(Qt::Window
                   | Qt::WindowTitleHint
                   | Qt::WindowSystemMenuHint
                   | Qt::WindowMinimizeButtonHint
                   | Qt::WindowMaximizeButtonHint
                   | Qt::WindowCloseButtonHint);
    resize(860, 720);
    auto *layout = new QVBoxLayout(this);

    auto *productRow = new QHBoxLayout();
    auto *productLabel = new QLabel(this);
    productLabel->setObjectName(QStringLiteral("productLabel"));
    m_product = new QComboBox(this);
    for (const SignProduct &product : kSignProducts)
        m_product->addItem(QString::fromLatin1(product.name));
    productRow->addWidget(productLabel);
    productRow->addWidget(m_product);
    productRow->addStretch();
    layout->addLayout(productRow);

    m_profile = new QLabel(this);
    m_profile->setWordWrap(true);
    layout->addWidget(m_profile);

    auto *versionRow = new QHBoxLayout();
    m_versionSLabel = new QLabel(this);
    m_versionS = new QLineEdit(this);
    m_versionS->setMaximumWidth(120);
    m_versionNLabel = new QLabel(this);
    m_versionN = new QLineEdit(this);
    m_versionN->setMaximumWidth(120);
    versionRow->addWidget(m_versionSLabel);
    versionRow->addWidget(m_versionS);
    versionRow->addWidget(m_versionNLabel);
    versionRow->addWidget(m_versionN);
    versionRow->addStretch();
    layout->addLayout(versionRow);

    auto *optionRow = new QHBoxLayout();
    auto *sideLabel = new QLabel(this);
    sideLabel->setObjectName(QStringLiteral("sideLabel"));
    m_sideS = new QRadioButton(QStringLiteral("S"), this);
    m_sideN = new QRadioButton(QStringLiteral("NS"), this);
    auto *sideGroup = new QButtonGroup(this);
    sideGroup->addButton(m_sideS);
    sideGroup->addButton(m_sideN);
    m_sideS->setChecked(true);
    m_dev = new QRadioButton(this);
    m_remote = new QRadioButton(this);
    auto *modeGroup = new QButtonGroup(this);
    modeGroup->addButton(m_dev);
    modeGroup->addButton(m_remote);
    m_dev->setChecked(true);
    optionRow->addWidget(sideLabel);
    optionRow->addWidget(m_sideS);
    optionRow->addWidget(m_sideN);
    optionRow->addSpacing(16);
    optionRow->addWidget(m_dev);
    optionRow->addWidget(m_remote);
    optionRow->addStretch();
    layout->addLayout(optionRow);

    auto *fileRow = new QHBoxLayout();
    m_choose = new QPushButton(this);
    m_path = new QLabel(this);
    m_path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_path->setWordWrap(true);
    fileRow->addWidget(m_choose);
    fileRow->addWidget(m_path, 1);
    layout->addLayout(fileRow);

    m_table = new QTableWidget(this);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setWordWrap(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    layout->addWidget(m_table, 1);

    auto addOutRow = [this, layout](QLabel **label, QLineEdit **edit, QPushButton **browse) {
        auto *row = new QHBoxLayout();
        *label = new QLabel(this);
        *edit = new QLineEdit(this);
        *browse = new QPushButton(this);
        row->addWidget(*label);
        row->addWidget(*edit, 1);
        row->addWidget(*browse);
        layout->addLayout(row);
    };
    addOutRow(&m_signedOutLabel, &m_signedOut, &m_signedBrowse);
    addOutRow(&m_fwmpOutLabel, &m_fwmpEdit, &m_fwmpBrowse);

    auto *actions = new QHBoxLayout();
    m_signed = new QPushButton(this);
    m_packed = new QPushButton(this);
    actions->addWidget(m_signed);
    actions->addWidget(m_packed);
    actions->addStretch();
    layout->addLayout(actions);

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(2000);
    layout->addWidget(m_log, 1);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    connect(m_product, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        updateProfileText();
        loadStoredVersions();
    });
    connect(m_choose, &QPushButton::clicked, this, &SignPackDialog::chooseFile);
    connect(m_signedBrowse, &QPushButton::clicked, this, &SignPackDialog::browseSigned);
    connect(m_fwmpBrowse, &QPushButton::clicked, this, &SignPackDialog::browsePackage);
    connect(m_signed, &QPushButton::clicked, this, &SignPackDialog::outputSigned);
    connect(m_packed, &QPushButton::clicked, this, &SignPackDialog::outputPacked);
    connect(m_signedOut, &QLineEdit::editingFinished, this, &SignPackDialog::savePaths);
    connect(m_fwmpEdit, &QLineEdit::editingFinished, this, &SignPackDialog::savePaths);
    loadPaths();
    retranslateUi();
    loadStoredVersions();
}

void SignPackDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QDialog::changeEvent(event);
}

void SignPackDialog::setDownloadBusy(bool busy)
{
    m_downloadBusy = busy;
    updateButtons();
}

void SignPackDialog::updateButtons()
{
    const bool enable = !m_downloadBusy && !m_signing;
    m_signed->setEnabled(enable);
    m_packed->setEnabled(enable);
}

void SignPackDialog::retranslateUi()
{
    setWindowTitle(tr("Sign and pack"));
    findChild<QLabel *>(QStringLiteral("productLabel"))->setText(tr("Product selection:"));
    findChild<QLabel *>(QStringLiteral("sideLabel"))->setText(tr("Side:"));
    m_versionSLabel->setText(tr("S version:"));
    m_versionNLabel->setText(tr("NS version:"));
    m_dev->setText(tr("Development sign"));
    m_remote->setText(tr("Remote sign"));
    m_choose->setText(tr("Select firmware"));
    m_signedOutLabel->setText(tr("Signed output:"));
    m_fwmpOutLabel->setText(tr("Package output:"));
    m_signedBrowse->setText(tr("Select path"));
    m_fwmpBrowse->setText(tr("Select path"));
    m_signed->setText(tr("Output signed firmware"));
    m_packed->setText(tr("Output signed package"));
    updateProfileText();
    if (m_file.isEmpty())
        m_status->setText(tr("Select a firmware file."));
    else
        refresh();
}

void SignPackDialog::updateProfileText()
{
    const int index = m_product->currentIndex();
    if (index < 0 || index >= int(sizeof(kSignProducts) / sizeof(kSignProducts[0])))
        return;
    const SignProduct &product = kSignProducts[index];
    const QString slotText = product.h573
        ? tr("H573 slots S 0x0C044000, NS 0x0C100000")
        : tr("H5F4 slots S 0x0C078000, NS 0x0C0F8000");
    m_profile->setText(tr("Development profile %1, remote profile %2. %3")
                           .arg(QString::fromLatin1(product.devProfile),
                                QString::fromLatin1(product.prodProfile),
                                slotText));
}

void SignPackDialog::loadStoredVersions()
{
    const int index = m_product->currentIndex();
    if (index < 0 || index >= int(sizeof(kSignProducts) / sizeof(kSignProducts[0])))
        return;
    const QString kit = signKitDirectory();
    const QString chip = kSignProducts[index].h573 ? QStringLiteral("h573") : QStringLiteral("h5f4");
    const QString configPath = QDir(kit).filePath(QStringLiteral("config.") + chip);
    const QString secure = signConfigValue(configPath, QStringLiteral("MCUBOOT_IMAGE_VERSION_S"));
    const QString app = signConfigValue(configPath, QStringLiteral("MCUBOOT_IMAGE_VERSION_NS"));
    m_versionS->setText(secure);
    m_versionN->setText(app);
    if (secure.isEmpty() || app.isEmpty())
        m_status->setText(tr("Cannot read signing version from %1").arg(QDir::toNativeSeparators(configPath)));
    else if (!m_signing && m_file.isEmpty())
        m_status->setText(tr("Select a firmware file."));
}

void SignPackDialog::chooseFile()
{
    const QString start = m_file.isEmpty()
        ? QFileInfo(SettingsWidget::firmwareFilePath(QStringLiteral("firmware.bin"))).absolutePath()
        : QFileInfo(m_file).absolutePath();
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Sign and pack"), start,
        tr("Firmware (*.bin *.hex);;All files (*)"));
    if (file.isEmpty())
        return;
    m_file = QDir::toNativeSeparators(file);
    m_path->setText(m_file);
    fillOutputPaths();
    savePaths();
    refresh();
}

void SignPackDialog::loadPaths()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("ProgramDownload"));
    const QString file = settings.value(QStringLiteral("signPackFile")).toString().trimmed();
    const QString signedOut = settings.value(QStringLiteral("signPackSigned")).toString();
    const QString packageOut = settings.value(QStringLiteral("signPackFwmp")).toString();
    settings.endGroup();
    if (file.isEmpty())
        return;
    m_file = QDir::toNativeSeparators(file);
    m_path->setText(m_file);
    m_signedOut->setText(signedOut);
    m_fwmpEdit->setText(packageOut);
    if (m_signedOut->text().trimmed().isEmpty())
        m_signedOut->setText(fileBeside(m_file, QStringLiteral("_signed.bin")));
    if (m_fwmpEdit->text().trimmed().isEmpty())
        m_fwmpEdit->setText(fileBeside(m_file, QStringLiteral(".fwmp")));
}

void SignPackDialog::savePaths() const
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("ProgramDownload"));
    settings.setValue(QStringLiteral("signPackFile"), m_file);
    settings.setValue(QStringLiteral("signPackSigned"), m_signedOut->text().trimmed());
    settings.setValue(QStringLiteral("signPackFwmp"), m_fwmpEdit->text().trimmed());
    settings.endGroup();
}

void SignPackDialog::fillOutputPaths()
{
    if (m_file.isEmpty())
        return;
    m_signedOut->setText(fileBeside(m_file, QStringLiteral("_signed.bin")));
    m_fwmpEdit->setText(fileBeside(m_file, QStringLiteral(".fwmp")));
}

void SignPackDialog::browseSigned()
{
    QString suggested = m_signedOut->text().trimmed();
    if (suggested.isEmpty() && !m_file.isEmpty())
        suggested = fileBeside(m_file, QStringLiteral("_signed.bin"));
    const QString chosen = askSavePath(suggested, tr("BIN Files (*.bin);;All Files (*)"), QStringLiteral("bin"));
    if (chosen.isEmpty())
        return;
    m_signedOut->setText(chosen);
    savePaths();
}

void SignPackDialog::browsePackage()
{
    QString suggested = m_fwmpEdit->text().trimmed();
    if (suggested.isEmpty() && !m_file.isEmpty())
        suggested = fileBeside(m_file, QStringLiteral(".fwmp"));
    const QString chosen = askSavePath(suggested, tr("FWMP Files (*.fwmp);;All Files (*)"), QStringLiteral("fwmp"));
    if (chosen.isEmpty())
        return;
    m_fwmpEdit->setText(chosen);
    savePaths();
}

void SignPackDialog::refresh()
{
    if (m_file.isEmpty()) {
        m_table->clear();
        m_table->setRowCount(0);
        m_table->setColumnCount(0);
        m_status->setText(tr("Select a firmware file."));
        return;
    }
    const PackageReport report = inspectOne(m_file);
    m_table->clear();
    m_table->setColumnCount(1);
    m_table->setRowCount(RowCount);
    m_table->setHorizontalHeaderLabels(QStringList() << report.title);
    const QColor sectionColor(236, 239, 241);
    for (int row = 0; row < RowCount; ++row) {
        auto *name = new QTableWidgetItem(compareRowName(row));
        name->setFlags(Qt::ItemIsEnabled);
        auto *value = new QTableWidgetItem(compareRowIsSection(row) ? QString() : compareRowValue(report, row));
        value->setFlags(Qt::ItemIsEnabled);
        if (compareRowIsSection(row)) {
            QFont font = name->font();
            font.setBold(true);
            name->setFont(font);
            name->setBackground(QBrush(sectionColor));
            value->setBackground(QBrush(sectionColor));
        }
        m_table->setVerticalHeaderItem(row, name);
        m_table->setItem(row, 0, value);
    }
    m_status->setText(tr("Read only. The file was not modified."));
}

void SignPackDialog::outputSigned()
{
    begin(false);
}

void SignPackDialog::outputPacked()
{
    begin(true);
}

void SignPackDialog::begin(bool packAfter)
{
    if (m_signing || m_downloadBusy)
        return;
    if (m_file.isEmpty()) {
        m_status->setText(tr("Select a firmware file."));
        return;
    }
    m_imageType = m_sideS->isChecked() ? 1 : 0;
    refresh();
    const SignPackImage image = classifySignPack(m_file);
    if (image.kind == SignPackKind::Unreadable) {
        showResult(false, tr("Cannot read"));
        return;
    }
    if (image.kind == SignPackKind::BadHex) {
        showResult(false, tr("Hex could not be decoded."));
        return;
    }
    if (image.kind == SignPackKind::InvalidPack) {
        showResult(false, tr("Invalid header"));
        return;
    }
    if (image.kind == SignPackKind::Packed) {
        showResult(false, tr("Packed"));
        return;
    }
    if (image.kind == SignPackKind::Signed) {
        if (!packAfter) {
            showResult(false, tr("Signed"));
            return;
        }
        const QString chosen = m_fwmpEdit->text().trimmed();
        if (chosen.isEmpty()) {
            showResult(false, tr("Select an output path."));
            return;
        }
        QString message;
        if (!writePackedFile(image.bytes, chosen, &message))
            showResult(false, message);
        else
            showResult(true, message);
        return;
    }
    startSign(image.bytes, image.fileIsBin, packAfter);
}

void SignPackDialog::startSign(const QByteArray &bytes, bool fileIsBin, bool packAfter)
{
    const int index = m_product->currentIndex();
    if (index < 0 || index >= int(sizeof(kSignProducts) / sizeof(kSignProducts[0])))
        return;
    const SignProduct &product = kSignProducts[index];
    const QString version = (m_sideS->isChecked() ? m_versionS : m_versionN)->text().trimmed();
    if (!version.isEmpty() && !signVersionOk(version)) {
        showResult(false, tr("Version must look like 2.3.1."));
        return;
    }
    m_kit = signKitDirectory();
    if (m_kit.isEmpty()) {
        const QString message = tr("Cannot find sign kit: %1").arg(QStringLiteral("sign_kit"));
        appendLog(message + QLatin1Char('\n'));
        showResult(false, message);
        return;
    }

    m_finalSigned = m_signedOut->text().trimmed();
    if (m_finalSigned.isEmpty()) {
        showResult(false, tr("Select an output path."));
        return;
    }
    if (sameFile(m_finalSigned, m_file)) {
        showResult(false, tr("Cannot write BIN: %1").arg(m_finalSigned));
        return;
    }
    m_fwmpOut.clear();
    if (packAfter) {
        m_fwmpOut = m_fwmpEdit->text().trimmed();
        if (m_fwmpOut.isEmpty()) {
            showResult(false, tr("Select an output path."));
            return;
        }
        if (sameFile(m_fwmpOut, m_file) || sameFile(m_fwmpOut, m_finalSigned)) {
            showResult(false, tr("Cannot write FWMP: %1").arg(m_fwmpOut));
            return;
        }
    }

    m_tempInput.clear();
    m_signInput = m_file;
    if (!fileIsBin) {
        const QString temp = fileBeside(m_file, QStringLiteral(".signpack-in.bin"));
        QFile out(temp);
        if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size()) {
            showResult(false, tr("Cannot write %1: %2").arg(QDir::toNativeSeparators(temp), out.errorString()));
            return;
        }
        out.close();
        m_tempInput = temp;
        m_signInput = temp;
    }
    m_expectedSigned = fileBeside(m_signInput, QStringLiteral("_signed.bin"));
    m_imageType = m_sideS->isChecked() ? 1 : 0;
    m_packAfter = packAfter;
    m_production = m_remote->isChecked();
    m_chip = product.h573 ? QStringLiteral("h573") : QStringLiteral("h5f4");
    m_profileName = QString::fromLatin1(m_production ? product.prodProfile : product.devProfile);

    const QString bat = QDir(m_kit).filePath(QStringLiteral("sign.bat"));
    const QString kind = m_sideS->isChecked() ? QStringLiteral("s") : QStringLiteral("ns");
    const QString input = QDir::toNativeSeparators(m_signInput);
    m_process = new QProcess(this);
    m_process->setWorkingDirectory(m_kit);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        if (!m_process)
            return;
        const QString text = QString::fromLocal8Bit(m_process->readAllStandardOutput());
        if (!text.isEmpty())
            appendLog(text);
    });
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus status) {
                if (!m_process || !m_signing)
                    return;
                const QString tail = QString::fromLocal8Bit(m_process->readAllStandardOutput());
                if (!tail.isEmpty())
                    appendLog(tail);
                QProcess *process = m_process;
                m_process = nullptr;
                process->deleteLater();
                const bool ok = status != QProcess::CrashExit && code == 0;
                if (!ok) {
                    if (!sameFile(m_expectedSigned, m_finalSigned))
                        QFile::remove(m_expectedSigned);
                    finishSign(false, tr("Signing failed, exit code %1").arg(code));
                    return;
                }
                if (!QFileInfo::exists(m_expectedSigned)) {
                    finishSign(false, tr("Cannot find file: %1").arg(m_expectedSigned));
                    return;
                }
                QString moveError;
                if (!moveToPath(m_expectedSigned, m_finalSigned, &moveError)) {
                    finishSign(false, moveError);
                    return;
                }
                const QString signedPath = QDir::toNativeSeparators(QFileInfo(m_finalSigned).absoluteFilePath());
                if (!m_packAfter) {
                    finishSign(true, tr("Wrote %1").arg(signedPath));
                    return;
                }
                QFile signedFile(signedPath);
                if (!signedFile.open(QIODevice::ReadOnly)) {
                    finishSign(false, tr("Wrote %1").arg(signedPath) + QLatin1Char('\n') + tr("Cannot read"));
                    return;
                }
                const QByteArray payload = signedFile.readAll();
                signedFile.close();
                QString packMessage;
                if (!writePackedFile(payload, m_fwmpOut, &packMessage)) {
                    finishSign(false, tr("Wrote %1").arg(signedPath) + QLatin1Char('\n') + packMessage);
                    return;
                }
                finishSign(true, tr("Wrote %1").arg(signedPath) + QLatin1Char('\n') + packMessage);
            });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || !m_process)
            return;
        QProcess *process = m_process;
        m_process = nullptr;
        process->disconnect(this);
        process->deleteLater();
        if (!sameFile(m_expectedSigned, m_finalSigned))
            QFile::remove(m_expectedSigned);
        finishSign(false, tr("Cannot start script: %1").arg(QDir(m_kit).filePath(QStringLiteral("sign.bat"))));
    });

    QStringList args;
    args << QStringLiteral("/c")
         << QDir::toNativeSeparators(bat)
         << QStringLiteral("--chip")
         << m_chip
         << (m_production ? QStringLiteral("--prod") : QStringLiteral("--dev"))
         << QStringLiteral("--profile")
         << m_profileName;
    if (!version.isEmpty())
        args << QStringLiteral("--version") << version;
    args << kind << input;
    QStringList shown;
    for (int i = 1; i < args.size(); ++i) {
        const QString arg = args.at(i);
        if (arg.contains(QLatin1Char(' ')) || arg.contains(QLatin1Char('\t')))
            shown << QLatin1Char('"') + arg + QLatin1Char('"');
        else
            shown << arg;
    }
    appendLog(tr("Sign kit: %1").arg(QDir::toNativeSeparators(m_kit)) + QLatin1Char('\n'));
    appendLog(tr("Signing %1 %2").arg(kind.toUpper(), input) + QLatin1Char('\n'));
    appendLog(shown.join(QLatin1Char(' ')) + QLatin1Char('\n'));
    m_signing = true;
    updateButtons();
    m_status->setText(tr("Signing %1").arg(QString::fromLatin1(product.name)));
    emit signingChanged(true);
    m_process->start(QStringLiteral("cmd.exe"), args);
}

void SignPackDialog::finishSign(bool ok, const QString &message)
{
    dropTemp();
    m_signing = false;
    updateButtons();
    emit signingChanged(false);
    showResult(ok, message);
}

QString SignPackDialog::askSavePath(const QString &suggested, const QString &filter, const QString &suffix)
{
    const QFileInfo info(QDir::fromNativeSeparators(suggested));
    QFileDialog dialog(this, tr("Sign and pack"), info.absolutePath(), filter);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setDefaultSuffix(suffix);
    if (!info.fileName().isEmpty())
        dialog.selectFile(info.fileName());
    if (dialog.exec() != QDialog::Accepted)
        return QString();
    const QStringList files = dialog.selectedFiles();
    return files.isEmpty() ? QString() : QDir::toNativeSeparators(files.first());
}

bool SignPackDialog::writePackedFile(const QByteArray &signedBytes, const QString &out, QString *message)
{
    if (out.isEmpty() || sameFile(out, m_file)) {
        if (message)
            *message = tr("Cannot write FWMP: %1").arg(out);
        return false;
    }
    const QByteArray header = fwmpHeader(signedBytes, m_imageType, m_imageType);
    if (header.size() != 64) {
        if (message)
            *message = tr("Invalid header");
        return false;
    }
    QFile output(out);
    if (!output.open(QIODevice::WriteOnly) || output.write(header) != header.size()
        || output.write(signedBytes) != signedBytes.size()) {
        if (message)
            *message = tr("Cannot write %1: %2").arg(QDir::toNativeSeparators(out), output.errorString());
        return false;
    }
    output.close();
    const QString written = QDir::toNativeSeparators(QFileInfo(out).absoluteFilePath());
    QString text = tr("Wrote %1").arg(written);
    const QString sizeWarning = firmwareSizeWarning(m_imageType, signedBytes.size());
    if (!sizeWarning.isEmpty())
        text = sizeWarning + QLatin1Char('\n') + text;
    if (message)
        *message = text;
    return true;
}

void SignPackDialog::showResult(bool ok, const QString &message)
{
    refresh();
    m_status->setText(message);
    appendLog(message + QLatin1Char('\n'));
    if (ok)
        QMessageBox::information(this, tr("Sign and pack"), message);
    else
        QMessageBox::warning(this, tr("Sign and pack"), message);
}

void SignPackDialog::appendLog(const QString &text)
{
    m_log->moveCursor(QTextCursor::End);
    m_log->insertPlainText(text);
    m_log->moveCursor(QTextCursor::End);
}

void SignPackDialog::dropTemp()
{
    if (m_tempInput.isEmpty())
        return;
    QFile::remove(m_tempInput);
    m_tempInput.clear();
}
