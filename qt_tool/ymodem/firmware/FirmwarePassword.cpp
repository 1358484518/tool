#include "FirmwarePassword.h"

#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QtNetwork/qpassworddigestor.h>
#include <QRandomGenerator>
#include <QSysInfo>

namespace {

const int kKeyBytes = 32;          // PBKDF2 输出长度，给 HMAC 当密钥
const int kIvBytes = 16;           // 每次保存换一个随机前缀，避免同样的密码得到同样的密文
const int kIterations = 10000;     // 把邮箱和机器码拉成密钥的迭代次数
const char kSalt[] = "tmx-firmware-password-v1";  // 固定盐，和邮箱、机器码一起决定密钥

QByteArray machineCode()  // 这台电脑的唯一码；没有时退回主机名
{
    const QByteArray id = QSysInfo::machineUniqueId();
    if (!id.isEmpty())
        return id;
    return QSysInfo::machineHostName().toUtf8();
}

} // namespace

QByteArray FirmwarePassword::keyFor(const QString &email)
{
    // 邮箱和机器码都进密钥材料。只改其中一个，旧密文就解不开。
    const QByteArray material = email.trimmed().toUtf8() + '\n' + machineCode();
    return QPasswordDigestor::deriveKeyPbkdf2(
        QCryptographicHash::Sha256, material, QByteArray(kSalt), kIterations, kKeyBytes);
}

QByteArray FirmwarePassword::keystream(const QByteArray &key, const QByteArray &iv, int size)
{
    QByteArray out;
    out.reserve(size);
    quint32 counter = 0;
    while (out.size() < size) {
        QByteArray block = iv;
        block.append(char((counter >> 24) & 0xff));
        block.append(char((counter >> 16) & 0xff));
        block.append(char((counter >> 8) & 0xff));
        block.append(char(counter & 0xff));
        out.append(QMessageAuthenticationCode::hash(block, key, QCryptographicHash::Sha256));
        ++counter;
    }
    out.truncate(size);
    return out;
}

QString FirmwarePassword::encrypt(const QString &email, const QString &password)
{
    if (email.trimmed().isEmpty() || password.isEmpty())
        return QString();
    const QByteArray key = keyFor(email);
    if (key.size() != kKeyBytes)
        return QString();
    quint32 words[kIvBytes / 4];
    QRandomGenerator::system()->generate(words, words + (kIvBytes / 4));
    const QByteArray iv(reinterpret_cast<const char *>(words), kIvBytes);
    const QByteArray plain = password.toUtf8();
    const QByteArray stream = keystream(key, iv, plain.size());
    QByteArray cipher(plain.size(), Qt::Uninitialized);
    for (int i = 0; i < plain.size(); ++i)
        cipher[i] = plain.at(i) ^ stream.at(i);
    return QString::fromLatin1((iv + cipher).toBase64());
}

QString FirmwarePassword::decrypt(const QString &email, const QString &stored)
{
    if (email.trimmed().isEmpty() || stored.isEmpty())
        return QString();
    const QByteArray blob = QByteArray::fromBase64(stored.toLatin1());
    if (blob.size() <= kIvBytes)
        return QString();
    const QByteArray key = keyFor(email);
    if (key.size() != kKeyBytes)
        return QString();
    const QByteArray iv = blob.left(kIvBytes);
    const QByteArray cipher = blob.mid(kIvBytes);
    const QByteArray stream = keystream(key, iv, cipher.size());
    QByteArray plain(cipher.size(), Qt::Uninitialized);
    for (int i = 0; i < cipher.size(); ++i)
        plain[i] = cipher.at(i) ^ stream.at(i);
    return QString::fromUtf8(plain);
}
