#ifndef TFTPCLIENTWORKER_H
#define TFTPCLIENTWORKER_H

#include <QObject>

namespace TftpQt {
class TftpClient;
}

/**
 * TFTP 客户端后端。无 parent，由界面移到自己的工作线程，和服务端不是同一条。
 * TftpClient 和它的 UDP 套接字只在这个线程里创建。一次只传一个文件。
 */
class TftpClientWorker : public QObject
{
    Q_OBJECT
public:
    explicit TftpClientWorker(QObject *parent = nullptr);  // 先不建客户端
    ~TftpClientWorker() override;                          // 取消未完成的传输

public slots:
    void startTransfer(QString host, quint16 port, QString remoteFile, QString localFile, bool download);  // 开始下载或上传
    void cancelTransfer();  // 取消当前这一次

signals:
    void logLine(const QString &line);                          // 过程日志
    void progress(qint64 done, qint64 total);                   // 已传字节。total 小于 0 表示总大小未知
    void transferFinished(bool success, const QString &message);  // 这一次结束
    void transferActive(bool active);                           // 真则正在传，界面锁住再次开始

private:
    void ensureClient();  // 第一次传输时在本线程创建 TftpClient

    TftpQt::TftpClient *m_client = nullptr;  // 本线程里的客户端。空表示还没传过
};

#endif // TFTPCLIENTWORKER_H
