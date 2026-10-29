#ifndef FTPCLIENTWORKER_H
#define FTPCLIENTWORKER_H

#include <QObject>

#include "qurlinfo.h"

class QFile;
class QFtp;
class QTimer;

/**
 * FTP 客户端后端。无 parent，由界面移到自己的工作线程，和服务器线程分开。
 * QFtp 和传输用的 QFile 只在这个线程里创建。界面通过信号发命令、收状态。
 */
class FtpClientWorker : public QObject
{
    Q_OBJECT
public:
    explicit FtpClientWorker(QObject *parent = nullptr);  // 只建超时定时器，先不连
    ~FtpClientWorker() override;                          // 断开并丢掉 QFtp

public slots:
    void connectToHost(QString host, quint16 port, QString user, QString password);  // 连接并登录
    void disconnectFromHost();                          // 中止传输并关掉连接
    void refreshList();                                 // 重新列出当前目录
    void enterDirectory(QString name);                  // 进入子目录并列出
    void cdToParent(QString path);                      // 回到 path。根目录传 "/"
    void download(QString remoteName, QString localDir);  // 把远程文件写到本地目录
    void upload(QString localPath);                     // 把本地文件上传到当前目录
    void cancelTransfer();                              // 取消正在进行的上传或下载

signals:
    void statusMessage(const QString &text);            // 给人看的一行状态
    void connectionClosed();                            // 已经断开，界面恢复成未连接
    void loggedIn();                                    // 登录成功，可以列目录和上传
    void listingStarted();                              // 新的一次列表开始，界面先清空
    void listItem(const QString &name, qint64 size, const QString &owner,
                  const QString &group, const QString &time, bool isDir);  // 一条目录项
    void listFinished();                                // 这次列表结束
    void directoryFailed();                             // 进入目录失败，界面不要改当前路径
    void transferActive(bool active);                   // 真则正在传文件，界面锁住其它操作
    void transferProgress(qint64 done, qint64 total);   // 进度。total 为 0 表示总大小未知

private slots:
    void onCommandFinished(int id, bool error);         // QFtp 一条命令结束。不能在这里直接 delete QFtp
    void onListInfo(const QUrlInfo &info);              // 把一条目录项转成界面能跨线程收的参数
    void onTransferProgress(qint64 done, qint64 total); // 原样转给界面
    void onConnectTimeout();                            // 连上或登录超时

private:
    void dropConnection();                  // 丢掉 QFtp。不要从 commandFinished 里直接调用
    void scheduleDrop();                    // 等当前回调返回后再 dropConnection
    void resetTransfer(bool removePartial); // 关掉传输文件。下载失败或取消时删掉半截文件
    void finishTransfer(bool error);        // 传输命令结束，写状态并放开界面

    QFtp *m_ftp = nullptr;          // 只在本线程 new。空表示没连着
    QFile *m_file = nullptr;        // 当前上传或下载的本地文件
    QTimer *m_timer = nullptr;      // 连接超时
    bool m_download = true;         // 真是下载，假是上传
    QString m_host;                 // 最近一次要连的主机，超时提示用
};

#endif // FTPCLIENTWORKER_H
