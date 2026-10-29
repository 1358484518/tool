#ifndef TFTPSERVERWORKER_H
#define TFTPSERVERWORKER_H

#include <QObject>

namespace TftpQt {
class TftpServer;
}

/**
 * TFTP 服务后端。无 parent，由界面移到自己的工作线程。
 * 监听套接字只在 startServer 里创建，所以不会生在界面线程。
 */
class TftpServerWorker : public QObject
{
    Q_OBJECT
public:
    explicit TftpServerWorker(QObject *parent = nullptr);  // 先不监听
    ~TftpServerWorker() override;                          // 关掉监听

public slots:
    void startServer(quint16 port, QString root);  // 按端口和根目录重新监听
    void stopServer();                             // 停掉监听和正在传的会话

signals:
    void logLine(const QString &line);                              // 协议日志
    void runningChanged(bool running, const QString &message);      // 听上了、失败或停止

private:
    void ensureServer();  // 第一次启动时在本线程创建 TftpServer

    TftpQt::TftpServer *m_server = nullptr;  // 本线程里的服务。空表示还没启动过
};

#endif // TFTPSERVERWORKER_H
