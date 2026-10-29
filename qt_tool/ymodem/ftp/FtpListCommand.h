#ifndef FTPLISTCOMMAND_H
#define FTPLISTCOMMAND_H

#include "FtpCommand.h"

#include <QFileInfo>

class QTimer;

/**
 * LIST / NLST。按批把目录项写到数据连接，避免一次列完堵住工作线程的事件循环。
 */
class FtpListCommand : public FtpCommand
{
    Q_OBJECT
public:
    explicit FtpListCommand(QObject *parent, const QString &listDirectory, bool nameListOnly = false);  // 记下要列的本地目录
    ~FtpListCommand() override;  // 正常结束时补 226

private:
    void startImplementation() override;             // 读目录并开始分批发送
    QString fileListingString(const QFileInfo &fi);  // 一条 unix ls -l 风格的列表

private slots:
    void listNextBatch();  // 每次最多写 10 条

private:
    QString m_listDirectory;             // 要列出的本地路径
    bool m_nameListOnly = false;         // 真是 NLST，只写文件名
    bool m_failed = false;               // 目录打不开，析构时不要再回 226
    QTimer *m_timer = nullptr;           // 零间隔分批，让事件循环还能处理别的连接
    QFileInfoList m_list;                // 这一次要发送的条目
    int m_index = 0;                     // 已经写到第几条
};

#endif // FTPLISTCOMMAND_H
