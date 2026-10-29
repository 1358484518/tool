#ifndef FTPCONTROLCONNECTION_H
#define FTPCONTROLCONNECTION_H

#include <QObject>

class QTcpSocket;
class FtpCommand;
class DataConnection;

/**
 * 一条 FTP 控制连接。读客户端命令，在工作线程里解析并执行。
 * 除了要数据连接的命令，其余都在这里完成。活动通过 activity 交给界面，不直接碰控件。
 */
class FtpControlConnection : public QObject
{
    Q_OBJECT
public:
    explicit FtpControlConnection(QObject *parent, QTcpSocket *socket, const QString &rootPath,
                                  const QString &userName, const QString &password, bool readOnly);  // 接管控制套接字，先不发欢迎语
    ~FtpControlConnection() override;  // 拆掉套接字信号，避免析构过程中再 deleteLater
    void opened();                     // 界面已经接上 activity 之后再回 220

signals:
    void activity(const QString &line);  // 给界面日志的一行，已经去掉密码明文

public slots:
    void reply(const QString &replyCode);  // 写一条应答，并通知界面

private slots:
    void acceptNewData();       // 读入一行命令。一次只处理一行，剩下的交给下次事件
    void disconnectFromHost();  // QUIT 时关掉控制连接
    void onDisconnected();      // 控制连接断开，记一笔日志再销毁自己

private:
    bool verifyAuthentication(const QString &command);   // 没登录时只放行 USER/PASS 等
    bool verifyWritePermission(const QString &command);  // 只读模式下拒绝上传和删除
    QString stripFlagL(const QString &fileName);         // 丢掉部分客户端 LIST 后面的 -L
    void parseCommand(const QString &entireCommand, QString *command, QString *commandParameters);  // 命令大写，参数原样
    QString toLocalPath(const QString &fileName) const;  // 虚拟路径收进根目录，不让 .. 逃出去
    void processCommand(const QString &entireCommand);   // 执行一行
    void startOrScheduleCommand(FtpCommand *ftpCommand); // 挂到数据连接上，没有数据连接就回 425
    void port(const QString &addressAndPort);            // 主动模式
    void pasv();                                         // 被动模式
    void list(const QString &dir, bool nameListOnly);    // LIST 或 NLST
    void retr(const QString &fileName);                  // 下载
    void stor(const QString &fileName, bool appendMode = false);  // 上传或追加
    void cwd(const QString &dir);                        // 切换虚拟目录
    void mkd(const QString &dir);                        // 建目录
    void rmd(const QString &dir);                        // 删空目录
    void dele(const QString &fileName);                  // 删文件
    void rnto(const QString &fileName);                  // 跟上一条 RNFR 完成改名
    void quit();                                         // 等传输结束再关控制连接
    void size(const QString &fileName);                  // 文件大小
    void pass(const QString &password);                  // 核对密码。用户名和密码都空则匿名
    void cdup();                                         // 回到上一级
    void feat();                                         // 告诉客户端支持 UTF8 文件名
    qint64 seekTo();                                     // 上一条若是 REST，返回偏移

    QTcpSocket *m_socket = nullptr;     // 控制连接
    QString m_peerIp;                   // 客户端地址，断开日志要用，套接字销毁后就没了
    QString m_currentDirectory;         // 虚拟当前目录，以 / 开头
    QString m_lastProcessedCommand;     // 上一条原文，RNFR/REST 要看它
    bool m_loggedIn = false;            // 已经 PASS 成功
    QString m_userName;                 // 期望的用户名
    QString m_password;                 // 期望的密码，空表示不校验
    QString m_rootPath;                 // 本地根目录，客户端看不到外面
    DataConnection *m_dataConnection = nullptr;  // 这条控制连接唯一的数据连接
    bool m_readOnly = false;            // 真则拒绝写操作
};

#endif // FTPCONTROLCONNECTION_H
