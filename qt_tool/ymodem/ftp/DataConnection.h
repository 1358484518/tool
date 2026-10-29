#ifndef DATACONNECTION_H
#define DATACONNECTION_H

#include <QObject>
#include <QPointer>

class QTcpServer;
class QTcpSocket;
class FtpCommand;

/**
 * 一条控制连接对应的数据连接。PORT 是服务器去连客户端，PASV 是客户端来连服务器。
 * 同一时间只保留一条。新的 PORT/PASV 会丢掉还没跑完的上一条。
 * 套接字只在 FTP 工作线程里创建。
 */
class DataConnection : public QObject
{
    Q_OBJECT
public:
    explicit DataConnection(QObject *parent = nullptr);  // 建好被动监听用的 QTcpServer，先不 listen

    void scheduleConnectToHost(const QString &hostName, int port);  // PORT：记下客户端地址，等命令到了再连
    int listen();                                                   // PASV：随便挑一个端口听，返回端口号，失败返回 0
    bool setFtpCommand(FtpCommand *command);                        // 挂上 LIST/RETR/STOR，没有先行的 PORT/PASV 则失败
    FtpCommand *ftpCommand();                                       // 正在跑的数据命令，没有则返回空

private slots:
    void onNewConnection();  // 被动模式接受客户端的数据连接
    void onConnected();      // 主动模式已经连上客户端

private:
    void startFtpCommand();  // 套接字和命令都齐了才真正开始传

    QTcpServer *m_server = nullptr;       // 被动模式临时监听，连上一个就关掉
    QTcpSocket *m_socket = nullptr;       // 当前数据套接字，交给命令后清空
    QPointer<FtpCommand> m_command;       // 等待或正在使用这条数据连接的命令
    bool m_socketReady = false;           // 数据套接字已经连上
    bool m_waitingForCommand = false;     // 已经 PORT/PASV，还在等 LIST/RETR/STOR
    bool m_active = false;                // 真是 PORT，假是 PASV
    QString m_hostName;                   // PORT 里的客户端地址
    int m_port = 0;                       // PORT 里的客户端端口
};

#endif // DATACONNECTION_H
