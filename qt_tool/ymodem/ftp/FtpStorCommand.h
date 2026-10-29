#ifndef FTPSTORCOMMAND_H
#define FTPSTORCOMMAND_H

#include "FtpCommand.h"

class QFile;

/**
 * STOR / APPE。客户端上传文件。收到一块就写一块，不在工作线程里攒完整文件。
 */
class FtpStorCommand : public FtpCommand
{
    Q_OBJECT
public:
    explicit FtpStorCommand(QObject *parent, const QString &fileName, bool appendMode = false, qint64 seekTo = 0);  // appendMode 为真是 APPE
    ~FtpStorCommand() override;  // 写完回 226，写失败回 451

private:
    void startImplementation() override;  // 打开本地文件并等数据连接上的字节

private slots:
    void acceptNextBlock();  // 把刚到的一块写入文件

private:
    QString m_fileName;       // 本地目标路径
    QFile *m_file = nullptr;  // 正在写的文件
    bool m_appendMode = false;  // 真则追加，假则覆盖
    qint64 m_seekTo = 0;      // REST 指定的写入位置
    bool m_success = false;   // 文件已打开且目前没有写失败
};

#endif // FTPSTORCOMMAND_H
