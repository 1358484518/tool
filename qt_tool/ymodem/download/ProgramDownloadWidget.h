#ifndef PROGRAMDOWNLOADWIDGET_H
#define PROGRAMDOWNLOADWIDGET_H

#include <QStringList>
#include <QWidget>

#include <functional>

class QButtonGroup;
class QEvent;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProcess;
class QPushButton;
class QRadioButton;
class QTimer;
class QVBoxLayout;

/**
 * 程序下载页。编程器路径、生成 DA OBK，以及 G0、RAT、RD0047、SECC 四个抽屉。
 * 路径框可以手输，也可以点按钮选。文件在程序目录或 ACSF 里就存相对路径。
 * G0 的下载会启动对应 bat。页面上填了 STM32_Programmer_CLI 时，把它的目录加到 PATH。
 * 脚本仍先找自己写的三个目录；那些目录没有时，才用到这条 PATH。
 * 下面的日志框跟着脚本自己写的 log 文件走，不显示 PowerShell 管道里的 CLIXML。最多留 2000 行。
 */
class ProgramDownloadWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ProgramDownloadWidget(QWidget *parent = nullptr);  // 建编程器路径、生成 OBK 和四个产品抽屉
    ~ProgramDownloadWidget() override;                          // 记下编程器路径和各行路径

protected:
    void changeEvent(QEvent *event) override;  // 语言切换时刷新本页文字

private:
    /**
     * 一个产品里的一行：说明，以及 Boot、安全程序、应用程序三条路径。
     */
    struct ImageRow {
        QLabel *title = nullptr;              // 「下载G0测试程序」这一类说明
        QLabel *bootLabel = nullptr;          // 「Boot程序」
        QLineEdit *boot = nullptr;            // Boot，缺省是该目录里的 bl2.hex
        QPushButton *chooseBoot = nullptr;    // 改 Boot 路径
        QLabel *secureLabel = nullptr;        // 「安全程序」
        QLineEdit *secure = nullptr;          // 安全侧，缺省是 tfm_s_signed.bin
        QPushButton *chooseSecure = nullptr;  // 改安全程序路径
        QLabel *appLabel = nullptr;           // 「应用程序」
        QLineEdit *app = nullptr;             // 应用程序，缺省是 tfm_ns_signed.bin
        QPushButton *chooseApp = nullptr;     // 改应用程序路径
        QPushButton *download = nullptr;      // 启动这一块的 bat
        QString bootKey;                      // 设置里 Boot 路径的键
        QString secureKey;                    // 设置里安全程序路径的键
        QString appKey;                       // 设置里应用程序路径的键
        QString defaultScript;                // 相对 ACSF 的烧录脚本，界面不显示
        QString scriptArg;                    // 传给 bat 的镜像参数。发布脚本不用
        QString defaultBoot;                  // 相对 ACSF 的缺省 Boot
        QString defaultSecure;                // 相对 ACSF 的缺省安全程序
        QString defaultApp;                   // 相对 ACSF 的缺省应用程序
    };

    struct ProductPane {
        QString name;                         // 抽屉标题：G0、RAT、RD0047、SECC
        QPushButton *header = nullptr;       // 点击展开或收起
        QWidget *body = nullptr;             // 展开后的三行
        ImageRow test;
        ImageRow debug;
        ImageRow release;
    };

    void setupUi();             // 编程器、生成 OBK，再是四个产品抽屉和 DA 抽屉
    void retranslateUi();       // 按当前语言重写本页控件文字
    void loadSettings();        // 读路径；没存过的行用 ACSF 缺省文件
    void saveSettings() const;  // 程序目录、工具目录、ACSF 内的路径存相对路径
    void chooseCubeProg();      // 选择 STM32_Programmer_CLI.exe
    void useDefaultCube();      // 清空编程器路径，恢复脚本原来的查找
    void generateDaObk();       // 打开对话框，由根公钥生成 DA obk
    void packSecureFirmware();     // 打开对话框，把安全侧 bin 打成 FWMP
    void packNonSecureFirmware();  // 打开对话框，把非安全侧 bin 打成 FWMP
    void unpackSecureFirmware();      // 打开对话框，解开安全侧 FWMP
    void unpackNonSecureFirmware();   // 打开对话框，解开非安全侧 FWMP
    void configureProduct(ProductPane *pane, const QString &name, const QString &prefix,
                          const QString &folder, const QString &script);  // 填缺省路径和设置键
    void buildImageRow(ImageRow *row, QWidget *parent);  // 往抽屉里加一行
    QWidget *addDrawer(QVBoxLayout *host, QPushButton **headerOut, const QString &title, bool open);  // 抽屉头和内容，各自展开
    void eachRow(const std::function<void(ImageRow *)> &fn);  // 四个产品的测试、调试、发布
    void refreshDrawers();        // 按展开状态重写抽屉标题
    void buildDaRow(QWidget *parent);  // 公钥和 DA 认证一行，私钥、证书链和回归一行
    void showDaMaterial(bool leaf);  // 按叶子或中间填入缺省私钥和证书链
    void runDaClose();            // 用页面上的公钥做 DA 认证并设为 CLOSED
    void runDaRegression();       // 用页面上的私钥和证书链回归到 Open
    void runDaScript(const QString &scriptName, const QStringList &scriptArgs, const QString &title);

    void chooseImage(QLineEdit *edit, const QString &title, const QString &filter,
                     const QStringList &suffixes);  // 只接受这些扩展名
    QString pickExistingFile(QLineEdit *edit, const QString &title, const QString &filter);  // 从输入框当前路径打开文件框
    void showStoredOrDefault(QLineEdit *edit, const QString &saved, const QString &relativeToAcsf);  // 有保存用保存的，否则用 ACSF 缺省
    void runDownload(const ImageRow *row);  // 启动这一行的 bat，并把页面上的编程器目录放进 PATH
    void setDownloadRunning(bool running);  // 有脚本在跑时停用三个下载按钮
    void appendLog(const QString &text);    // 追加到日志框末尾。框自己丢掉超过 2000 行的旧内容
    void showRunStatus(const QString &result, bool ok);  // 页底状态：这次下载的结果和用时
    QString formatElapsed(qint64 ms) const;  // 不到一分钟用秒，否则用分和秒
    void pollScriptLog();                   // 把脚本 log 目录里这次新文件的新内容追加到日志框
    QLabel *m_cubeLabel = nullptr;         // 「STM32CubeProgrammer」
    QLineEdit *m_cubeProg = nullptr;       // STM32_Programmer_CLI.exe，可手输；空表示脚本自己找
    QPushButton *m_changeCube = nullptr;   // 选择编程器程序
    QPushButton *m_defaultCube = nullptr;  // 清空编程器路径
    QPushButton *m_genObk = nullptr;       // 打开生成 DA obk 的对话框
    QPushButton *m_packS = nullptr;        // 安全侧升级固件打包
    QPushButton *m_packNs = nullptr;       // 非安全侧升级固件打包
    QPushButton *m_unpackS = nullptr;      // 安全侧升级固件解包
    QPushButton *m_unpackNs = nullptr;     // 非安全侧升级固件解包
    ProductPane m_products[4];             // G0、RAT、RD0047、SECC
    QLabel *m_daTitle = nullptr;           // DA 标题，一直显示
    QWidget *m_daBody = nullptr;           // DA 内容，一直显示
    QLabel *m_keyLabel = nullptr;          // 「公钥」
    QLineEdit *m_key = nullptr;            // PEM 公钥，用来生成 DA OBK
    QPushButton *m_chooseKey = nullptr;    // 选择公钥
    QPushButton *m_daClose = nullptr;      // 生成 OBK，写入，设为 CLOSED
    QButtonGroup *m_levelGroup = nullptr;  // 叶子或中间，二选一
    QRadioButton *m_leaf = nullptr;        // 叶子私钥和叶子证书链
    QRadioButton *m_intermediate = nullptr;  // 中间私钥和中间证书链
    QLabel *m_privLabel = nullptr;         // 「私钥」
    QLineEdit *m_priv = nullptr;           // 回归用的 PEM 私钥
    QPushButton *m_choosePriv = nullptr;   // 选择私钥
    QLabel *m_certLabel = nullptr;         // 「证书链」
    QLineEdit *m_cert = nullptr;           // 回归用的 b64 证书链
    QPushButton *m_chooseCert = nullptr;   // 选择证书链
    QPushButton *m_regress = nullptr;      // 全回归到 Open
    QPushButton *m_clearLog = nullptr;     // 清空日志框里已经显示的内容
    QPlainTextEdit *m_log = nullptr;       // 脚本 log 文件的内容。只读，最多 2000 行
    QLabel *m_statusLabel = nullptr;       // 页底状态，显示这次下载的结果和用时
    QString m_runTitle;                    // 这次下载的行标题，写进状态栏
    QTimer *m_logPoll = nullptr;           // 烧录过程中定期读脚本 log 文件
    QProcess *m_process = nullptr;         // 正在跑的烧录脚本
    QString m_scriptLogDir;                // 这次 bat 旁边的 log 目录
    QString m_logGlob = QStringLiteral("flash_*.log");  // 这次要跟着读的日志名
    QString m_scriptLogFile;               // 这次新出现的日志
    QString m_boxTitle;                    // 失败对话框的标题
    bool m_sendEnter = false;              // 烧录脚本结尾有 pause，启动后送一个回车
    qint64 m_scriptLogOffset = 0;          // 已经显示过的文件长度
    qint64 m_runStartedMs = 0;             // 这次点击下载的时间，用来认出新日志
    bool m_scriptFailed = false;           // log 里出现 Failed 或 [FAIL]。bat 结尾的 pause 会把退出码清成 0
    QString m_failStep;                    // log 里 Failed: 后面的步骤名
};

#endif // PROGRAMDOWNLOADWIDGET_H
