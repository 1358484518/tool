#ifndef FTPSERVER_H
#define FTPSERVER_H

#include <QObject>
#include <QSet>

class QTcpServer;

/**
 * FTP 监听端。必须创建在 FTP 工作线程里，套接字不能挪到界面线程。
 * 每个客户端一条 FtpControlConnection，仍在这个线程的事件循环里处理。
 * 基于 QFtpServer（MIT，Copyright (c) 2020 Alexander Almaleh），去掉了 FTPS。
 */
class FtpServer : public QObject
{
    Q_OBJECT
public:
    explicit FtpServer(QObject *parent, const QString &rootPath, quint16 port,
                       const QString &userName, const QString &password, bool readOnly,
                       bool onlyOneIp);  // 立刻开始监听。onlyOneIp 为真时只接受第一个客户端地址
    bool isListening() const;     // 端口是否已经绑上
    QString errorString() const;  // 监听失败时的原因
    static QString lanIp();       // 本机一个局域网 IPv4，没有则空

signals:
    void logLine(const QString &line);              // 连接、命令和应答
    void peerConnected(const QString &ip);          // 新的客户端地址，界面用来改状态栏

private slots:
    void startNewControlConnection();  // 接受一条控制连接

private:
    QString m_userName;             // 期望的用户名
    QString m_password;             // 期望的密码，空则匿名
    QString m_rootPath;             // 客户端能看到的本地根目录
    QTcpServer *m_server = nullptr; // 控制端口
    QString m_error;                // listen 失败原因
    bool m_readOnly = false;        // 真则拒绝上传和删除
    bool m_onlyOneIp = false;       // 真则拒绝第一个地址以外的客户端
    QSet<QString> m_seenIps;        // 这次监听已经接受过的客户端地址
};

#endif // FTPSERVER_H
