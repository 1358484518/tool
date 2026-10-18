#ifndef FIRMWAREPASSWORD_H
#define FIRMWAREPASSWORD_H

#include <QString>

/**
 * 固件登录密码的本地密文。
 * Qt 5.14 没有 AES。密钥用 QPasswordDigestor 从邮箱和本机唯一码推出，
 * 再用 HMAC-SHA256 密钥流异或。密文以 Base64 放进 QSettings，不是明文。
 * 换电脑或换邮箱就解不开。
 */
class FirmwarePassword
{
public:
    static QString encrypt(const QString &email, const QString &password);  // 明文换成可写入 QSettings 的密文
    static QString decrypt(const QString &email, const QString &stored);    // 启动时把密文还原成密码，失败返回空

private:
    static QByteArray keyFor(const QString &email);  // 邮箱加本机唯一码推出的 32 字节密钥
    static QByteArray keystream(const QByteArray &key, const QByteArray &iv, int size);  // HMAC-SHA256 计数器密钥流
};

#endif // FIRMWAREPASSWORD_H
