#ifndef FTPRETRCOMMAND_H
#define FTPRETRCOMMAND_H

#include "FtpCommand.h"

class QFile;

/**
 * RETR。客户端下载服务器上的文件。按套接字写出的字节数继续读文件，不一次塞满内存。
 */
class FtpRetrCommand : public FtpCommand
{
    Q_OBJECT
public:
    explicit FtpRetrCommand(QObject *parent, const QString &fileName, qint64 seekTo = 0);  // seekTo 来自前面的 REST
    ~FtpRetrCommand() override;  // 传完回 226，没传完回 550

private:
    void startImplementation() override;  // 打开文件并写第一块

private slots:
    void refillSocketBuffer(qint64 bytes = 0);  // 发送缓冲空出一些后继续读文件

private:
    QString m_fileName;          // 本地文件路径
    QFile *m_file = nullptr;     // 正在读的文件
    qint64 m_seekTo = 0;         // 从文件中间接着下，0 表示从头
    bool m_failed = false;       // 文件打不开
};

#endif // FTPRETRCOMMAND_H
