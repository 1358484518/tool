#ifndef FTPWORKER_H
#define FTPWORKER_H

#include <QObject>

class FtpServer;

/**
 * FTP 后端。无 parent，由界面 move 到工作线程。
 * QTcpServer 和客户端套接字都在这个线程里创建和销毁，界面只收信号。
 */
class FtpWorker : public QObject
{
    Q_OBJECT
public:
    explicit FtpWorker(QObject *parent = nullptr);  // 先不监听
    ~FtpWorker() override;                          // 关掉正在听的服务

public slots:
    void startServer(quint16 port, QString root, QString user, QString password, bool readOnly, bool onlyOneIp);  // 按参数重新监听
    void stopServer();  // 关掉监听和已有客户端，并通知界面

signals:
    void serverState(bool listening, const QString &message);  // 听上了或失败、停止
    void logLine(const QString &line);                         // 转给调试控制台
    void peerConnected(const QString &ip);                     // 新客户端地址，界面改状态栏

private:
    void closeServer(bool notify);  // 删掉监听。notify 为真时才告诉界面已经停止

    FtpServer *m_server = nullptr;  // 当前这次监听，停掉就删掉
};

#endif // FTPWORKER_H
