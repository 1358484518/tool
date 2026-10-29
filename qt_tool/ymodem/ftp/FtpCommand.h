#ifndef FTPCOMMAND_H
#define FTPCOMMAND_H

#include <QObject>

class QTcpSocket;

/**
 * 需要数据连接的 FTP 命令基类。LIST、RETR、STOR 都从这里派生。
 * 套接字由 DataConnection 交过来，父对象就是这条命令，断开后自行销毁。
 * 基于 QFtpServer（MIT，Copyright (c) 2020 Alexander Almaleh），改为普通 TCP。
 */
class FtpCommand : public QObject
{
    Q_OBJECT
public:
    explicit FtpCommand(QObject *parent = nullptr);  // 只记下还没开始
    void start(QTcpSocket *socket);                  // 接管数据套接字并开始真正传输

signals:
    void reply(const QString &details);  // 控制连接上要回给客户端的一句话

protected:
    virtual void startImplementation() = 0;  // 子类在套接字就绪后做自己的事
    QTcpSocket *m_socket = nullptr;          // 数据连接，断开时这条命令会被删掉
    bool m_started = false;                  // start 之后为真，析构时才补结束应答
};

#endif // FTPCOMMAND_H
