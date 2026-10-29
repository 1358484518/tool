#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QString>
#include <QVector>

namespace TftpQt {

constexpr quint16 DefaultPort = 69;
constexpr int DefaultBlockSize = 512;
constexpr int MinBlockSize = 8;
constexpr int MaxBlockSize = 65464;
constexpr int EthernetBlockSize = 1468;

enum class Opcode : quint16 {
    Rrq = 1,
    Wrq = 2,
    Data = 3,
    Ack = 4,
    Error = 5,
    Oack = 6
};

enum class ErrorCode : quint16 {
    NotDefined = 0,
    FileNotFound = 1,
    AccessViolation = 2,
    DiskFull = 3,
    IllegalOperation = 4,
    UnknownTransferId = 5,
    FileExists = 6,
    NoSuchUser = 7,
    OptionRefused = 8
};

struct Option {
    QByteArray name;
    QByteArray value;
};

struct Packet {
    Opcode opcode = Opcode::Error;
    QByteArray filename;
    QByteArray mode;
    QVector<Option> options;
    quint16 block = 0;
    QByteArray data;
    quint16 errorCode = 0;
    QByteArray errorMessage;
    bool valid = false;
};

Packet decodePacket(const QByteArray &raw);
QByteArray encodeRequest(Opcode opcode, const QByteArray &filename, const QByteArray &mode,
                         const QVector<Option> &options);
QByteArray encodeData(quint16 block, const QByteArray &data);
QByteArray encodeAck(quint16 block);
QByteArray encodeError(quint16 code, const QByteArray &message);
QByteArray encodeOack(const QVector<Option> &options);

bool optionNameIs(const QByteArray &name, const char *expected);
QByteArray optionValue(const QVector<Option> &options, const char *name);

QString opcodeName(Opcode opcode);
QString errorCodeName(quint16 code);
bool samePeer(const QHostAddress &left, const QHostAddress &right);

QString safeChildPath(const QString &root, const QByteArray &relativeName, QString *errorMessage);

} // namespace TftpQt
