#ifndef FTPWIDGET_H
#define FTPWIDGET_H

#include <QThread>
#include <QWidget>

class QCheckBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class FtpClientWorker;
class FtpWorker;
class TftpClientWorker;
class TftpServerWorker;

/**
 * FTP / TFTP 页。上排是 FTP 服务和客户端，下排是 TFTP 服务和客户端。
 * 四个网络对象各有一条工作线程，界面只通过信号槽跟它们说话。
 */
class FtpWidget : public QWidget
{
    Q_OBJECT
public:
    explicit FtpWidget(QWidget *parent = nullptr);  // 建四格界面和四条工作线程。FTP 和 TFTP 都要点启动才监听
    ~FtpWidget() override;                          // 在各自线程里停掉四个网络对象

signals:
    void startRequested(quint16 port, QString root, QString user, QString password,
                        bool readOnly, bool onlyOneIp);  // 让服务端线程按表单重新监听
    void stopRequested();                                // 关掉服务。页销毁时用
    void clientConnectRequested(QString host, quint16 port, QString user, QString password);  // 客户端去连接
    void clientDisconnectRequested();                    // 客户端断开
    void clientRefreshRequested();                       // 重新列当前目录
    void clientEnterRequested(QString name);             // 进入子目录
    void clientUpRequested(QString path);                // 回到上一级，path 是目标绝对路径
    void clientDownloadRequested(QString remoteName, QString localDir);  // 下载选中的文件
    void clientUploadRequested(QString localPath);       // 上传本地文件
    void clientCancelRequested();                        // 取消当前传输
    void tftpStartRequested(quint16 port, QString root);  // 让 TFTP 服务线程开始监听
    void tftpStopRequested();                            // 关掉 TFTP 服务
    void tftpTransferRequested(QString host, quint16 port, QString remoteFile,
                               QString localFile, bool download);  // TFTP 下载或上传一个文件
    void tftpCancelRequested();                          // 取消当前 TFTP 传输

protected:
    void changeEvent(QEvent *event) override;  // 语言切换时刷新本页文字

private slots:
    void onRestart();                                           // 启动或停止 FTP 服务
    void onBrowseRoot();                                        // 选服务端根目录
    void onAnonymousToggled(bool checked);                      // 匿名时用户名和密码不可改
    void onServerState(bool listening, const QString &message); // 服务端底栏
    void onPeerConnected(const QString &ip);                    // 有客户端连上服务端时改底栏
    void onLogLine(const QString &line);                        // 追加到调试控制台
    void onClientConnect();                                     // 连接或断开远程 FTP
    void onClientUp();                                          // 回到上一级目录
    void onClientRefresh();                                     // 刷新当前目录
    void onClientDownload();                                    // 下载选中文件
    void onClientUpload();                                      // 选择本地文件上传
    void onClientCancel();                                      // 取消传输
    void onClientBrowseSave();                                  // 选择下载保存目录
    void onClientItemActivated(QTreeWidgetItem *item, int column);  // 双击目录就进去，双击文件就下载
    void onClientSelectionChanged();                            // 选中文件才允许下载
    void onClientStatus(const QString &text);                   // 客户端状态栏
    void onClientClosed();                                      // 连接没了，按钮恢复
    void onClientLoggedIn();                                    // 登录成功，可以浏览和上传
    void onListingStarted();                                    // 清空目录树，等新的条目
    void onListItem(const QString &name, qint64 size, const QString &owner,
                    const QString &group, const QString &time, bool isDir);  // 加一行
    void onListFinished();                                      // 列表结束，写条目数
    void onTransferActive(bool active);                         // 传输中锁住浏览和再次传输
    void onTransferProgress(qint64 done, qint64 total);         // 更新进度条
    void onClientDirectoryFailed();                             // 进入目录失败，保持原来的路径
    void onTftpToggle();                                        // 启动或停止 TFTP 服务
    void onTftpBrowseRoot();                                    // 选 TFTP 根目录
    void onTftpDownload();                                      // 按表单下载一个文件
    void onTftpUpload();                                        // 按表单上传一个文件
    void onTftpBrowseLocal();                                   // 选 TFTP 下载保存的文件夹
    void onTftpCancel();                                        // 取消 TFTP 传输
    void onTftpRunning(bool running, const QString &message);   // TFTP 服务状态
    void onTftpServerLog(const QString &line);                  // TFTP 服务日志
    void onTftpClientLog(const QString &line);                  // TFTP 客户端日志
    void onTftpProgress(qint64 done, qint64 total);             // TFTP 进度条
    void onTftpFinished(bool success, const QString &message);  // TFTP 这一次结束
    void onTftpActive(bool active);                             // TFTP 传输中锁住开始和取消以外的操作

private:
    void setupUi();             // 上排 FTP，下排 TFTP，每排左服务右客户端
    void retranslateUi();       // 按当前语言重写四格文字
    void loadSettings();        // 从 QSettings 的 Ftp 和 Tftp 组恢复
    void saveSettings();        // 把四格表单写回 QSettings
    void initWorker();          // FTP 服务线程
    void initClient();          // FTP 客户端线程
    void initTftpServer();      // TFTP 服务线程
    void initTftpClient();      // TFTP 客户端线程
    void shutdownWorker(QThread &thread, QObject *worker);  // 在工作线程里 delete，再等线程退出
    void setClientConnected(bool connected);  // 连接后才能浏览和上传
    void updateClientButtons();               // 按连接、传输和选中项刷新按钮
    void updateFtpButtons();                  // 按是否在监听刷新 FTP 的启动按钮和表单
    void updateTftpButtons();                 // 按监听和传输状态刷新 TFTP 按钮
    QString selectedClientName() const;       // 目录树里当前选中的名字
    bool selectedClientIsDir() const;         // 当前选中的是目录
    bool warnIfSaveDirServed(const QString &dir);  // 保存目录和服务端正在列出的目录相同就提醒并拒绝
    void beginClientPath(const QString &path);  // 记下即将列出的远程路径，列表成功后再显示
    static QString parentClientPath(const QString &path);  // 上一级。根目录还是 "/"

    QGroupBox *m_serverGroup = nullptr;    // 左上 FTP 服务
    QLabel *m_portLabel = nullptr;         // 端口
    QLabel *m_userLabel = nullptr;         // 用户
    QLabel *m_passwordLabel = nullptr;     // 密码
    QLabel *m_rootLabel = nullptr;         // 根目录
    QLineEdit *m_portEdit = nullptr;       // 监听端口
    QLineEdit *m_userEdit = nullptr;       // 用户名。匿名勾上后这次监听不用它
    QLineEdit *m_passwordEdit = nullptr;   // 密码。本地配置里明文保存，只给本机服务用
    QLineEdit *m_rootEdit = nullptr;       // 根目录，只能通过浏览按钮改
    QToolButton *m_browseBtn = nullptr;    // 选择根目录
    QCheckBox *m_anonymousCheck = nullptr; // 允许任意账号登录
    QCheckBox *m_readOnlyCheck = nullptr;  // 拒绝上传和删除
    QCheckBox *m_oneIpCheck = nullptr;     // 只接受第一个连上来的地址
    QPushButton *m_restartBtn = nullptr;   // 启动或停止监听
    QLabel *m_statusLabel = nullptr;       // 服务端底栏
    bool m_ftpListening = false;           // 正在监听。打开程序时是停着的
    QPlainTextEdit *m_log = nullptr;       // 服务端日志，直接显示在这一格里
    FtpWorker *m_worker = nullptr;         // 无 parent，一直活在 m_thread
    QThread m_thread;                      // 服务端套接字所在线程

    QGroupBox *m_clientGroup = nullptr;    // 右上 FTP 客户端
    QLabel *m_clientHostLabel = nullptr;   // 服务器地址
    QLabel *m_clientPortLabel = nullptr;   // 客户端端口
    QLabel *m_clientUserLabel = nullptr;   // 客户端用户
    QLabel *m_clientPasswordLabel = nullptr;  // 客户端密码
    QLabel *m_saveLabel = nullptr;         // 下载保存到
    QLineEdit *m_clientHostEdit = nullptr; // 要连的主机，默认本机
    QLineEdit *m_clientPortEdit = nullptr; // 要连的端口，默认同服务端
    QLineEdit *m_clientUserEdit = nullptr; // 登录用户。空则匿名
    QLineEdit *m_clientPasswordEdit = nullptr;  // 登录密码
    QLineEdit *m_saveDirEdit = nullptr;    // 下载文件保存目录
    QLabel *m_pathLabel = nullptr;         // 远程当前目录
    QTreeWidget *m_fileList = nullptr;     // 远程目录
    QPushButton *m_connectBtn = nullptr;   // 连接或断开
    QPushButton *m_upBtn = nullptr;        // 上一级
    QPushButton *m_refreshBtn = nullptr;   // 刷新列表
    QPushButton *m_saveBrowseBtn = nullptr;  // 选择保存目录
    QPushButton *m_downloadBtn = nullptr;  // 下载选中文件
    QPushButton *m_uploadBtn = nullptr;    // 上传本地文件
    QPushButton *m_cancelBtn = nullptr;    // 取消传输
    QProgressBar *m_progress = nullptr;    // 传输进度
    QLabel *m_clientStatus = nullptr;      // 客户端状态
    FtpClientWorker *m_client = nullptr;   // 无 parent，一直活在 m_clientThread
    QThread m_clientThread;                // 客户端套接字所在线程
    QString m_clientPath;                  // 远程当前路径，以 / 开头
    bool m_clientConnected = false;        // 已经登录
    bool m_transferring = false;           // 正在上传或下载
    QString m_pendingPath;                 // 这次列表对应的路径。成功后才写进 m_clientPath
    bool m_hasPendingPath = false;         // 有一次还没结束的列表
    bool m_navOk = true;                   // 进入目录失败时不要采用 m_pendingPath

    QGroupBox *m_tftpServerGroup = nullptr;   // 左下 TFTP 服务
    QLabel *m_tftpPortLabel = nullptr;        // TFTP 端口
    QLabel *m_tftpRootLabel = nullptr;        // TFTP 根目录
    QLineEdit *m_tftpPortEdit = nullptr;      // 监听端口，默认 6969
    QLineEdit *m_tftpRootEdit = nullptr;      // 根目录，只能通过浏览按钮改
    QToolButton *m_tftpBrowseBtn = nullptr;   // 选择 TFTP 根目录
    QPushButton *m_tftpToggleBtn = nullptr;   // 启动或停止
    QLabel *m_tftpStatus = nullptr;           // TFTP 服务状态
    QPlainTextEdit *m_tftpServerLog = nullptr;  // TFTP 服务日志
    TftpServerWorker *m_tftpServer = nullptr; // 无 parent，一直活在 m_tftpServerThread
    QThread m_tftpServerThread;               // TFTP 服务套接字所在线程
    bool m_tftpRunning = false;               // 正在监听

    QGroupBox *m_tftpClientGroup = nullptr;   // 右下 TFTP 客户端
    QLabel *m_tftpHostLabel = nullptr;        // 服务器地址
    QLabel *m_tftpClientPortLabel = nullptr;  // 客户端端口
    QLabel *m_tftpRemoteLabel = nullptr;      // 远端文件名
    QLabel *m_tftpLocalLabel = nullptr;       // 本地文件
    QLineEdit *m_tftpHostEdit = nullptr;      // 要连的主机，默认本机
    QLineEdit *m_tftpClientPortEdit = nullptr;  // 要连的端口，默认同服务端
    QLineEdit *m_tftpRemoteEdit = nullptr;    // 远端文件名，没有目录列表
    QLineEdit *m_tftpLocalEdit = nullptr;     // 本地文件或保存目录。浏览选的是目录
    QPushButton *m_tftpLocalBrowseBtn = nullptr;  // 选择本地文件
    QPushButton *m_tftpDownloadBtn = nullptr; // 下载
    QPushButton *m_tftpUploadBtn = nullptr;   // 上传
    QPushButton *m_tftpCancelBtn = nullptr;   // 取消
    QProgressBar *m_tftpProgress = nullptr;   // 传输进度
    QLabel *m_tftpClientStatus = nullptr;     // 客户端状态
    QPlainTextEdit *m_tftpClientLog = nullptr;  // TFTP 客户端日志
    TftpClientWorker *m_tftpClient = nullptr; // 无 parent，一直活在 m_tftpClientThread
    QThread m_tftpClientThread;               // TFTP 客户端套接字所在线程
    bool m_tftpBusy = false;                  // 正在传一个文件
};

#endif // FTPWIDGET_H
