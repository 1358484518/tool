#ifndef PROGRAMDOWNLOADWIDGET_H
#define PROGRAMDOWNLOADWIDGET_H

#include <QByteArray>
#include <QWidget>

class QCheckBox;
class QEvent;
class QFile;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProcess;
class QPushButton;

/**
 * 程序下载页。上下两个分组，分别是 STM32H573 和 STM32H5F4。
 * 各显示自己的 da_tfm_close.bat、regression.bat，以及可选的应用程序 bin。
 * 应用程序空着时，下载脚本仍使用默认的 tfm_ns_signed.bin。
 * 执行后把脚本输出收进本组日志。
 * 不使用串口页、固件页或已有的 YModem 发送。
 */
class ProgramDownloadWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ProgramDownloadWidget(QWidget *parent = nullptr);  // 建上下两个芯片分组、日志和保存选项
    ~ProgramDownloadWidget() override;                          // 记下自动保存勾选、脚本路径和应用程序

protected:
    void changeEvent(QEvent *event) override;  // 语言切换时刷新本页文字

private:
    /**
     * 一个芯片分区。路径、执行、日志和自动保存各自一份，两片互不影响。
     */
    struct ChipPanel {
        QGroupBox *group = nullptr;        // 分区
        QLabel *pathLabel = nullptr;       // 「下载脚本路径」
        QLineEdit *path = nullptr;         // 下载脚本全路径，可改，缺省是 da_tfm_close.bat
        QPushButton *changePath = nullptr; // 改下载脚本路径，本组有脚本在跑时禁用
        QPushButton *download = nullptr;   // 执行下载，本组有脚本在跑时禁用
        QLabel *erasePathLabel = nullptr;  // 「擦除脚本路径」
        QLineEdit *erasePath = nullptr;    // 擦除脚本全路径，可改，缺省是 regression.bat
        QPushButton *changeErasePath = nullptr;  // 改擦除脚本路径，本组有脚本在跑时禁用
        QPushButton *erase = nullptr;      // 执行擦除，本组有脚本在跑时禁用
        QLabel *appLabel = nullptr;        // 「应用程序」
        QLineEdit *appPath = nullptr;      // 指定的 NS 镜像，空表示脚本自己找 tfm_ns_signed.bin
        QPushButton *changeApp = nullptr;  // 选择应用程序 bin，本组有脚本在跑时禁用
        QPushButton *defaultApp = nullptr; // 清空应用程序，恢复默认文件
        QLabel *logLabel = nullptr;        // 「日志」
        QPlainTextEdit *log = nullptr;     // 脚本标准输出和错误
        QPushButton *saveLog = nullptr;    // 把当前日志另存为文件
        QCheckBox *autoSave = nullptr;     // 勾上后新日志立刻追加落盘，默认勾选
        QProcess *process = nullptr;       // 正在跑的 cmd，空表示没在跑
        QFile *autoFile = nullptr;         // 自动保存文件，没勾选或打开失败时为空
        QByteArray pending;                // 还没凑成一整行的控制台字节
        QString autoFileName;              // 自动保存文件名，不含目录
        bool endSeen = false;              // 已看到脚本收尾，准备结束末尾的 cmd /k
    };

    void setupUi();        // 上 STM32H573，下 STM32H5F4
    void retranslateUi();  // 按当前语言重写本页控件文字
    void loadSettings();   // 读自动保存、脚本路径和应用程序，相对路径按工具目录还原
    void saveSettings() const;  // 工具目录内的路径存相对路径，其余存绝对路径
    QString scriptFile(const QString &chipDir, const QString &scriptRelative) const;  // 该芯片 windows-tfm-tools 下的脚本
    void buildChip(ChipPanel *panel, const QString &title, const QString &scriptPath,
                   const QString &erasePath, const QString &autoFileName);  // 下载、擦除、应用程序、日志和保存选项
    void chooseScriptPath(QLineEdit *edit);  // 选一个脚本文件并写回路路径
    void chooseAppPath(QLineEdit *edit);     // 选一个 bin 作为应用程序
    void useDefaultApp(QLineEdit *edit);     // 清空应用程序路径，下次下载用默认文件
    void setScriptRunning(ChipPanel *panel, bool running);  // 跑脚本时禁用执行、改路径和应用程序
    void runScript(ChipPanel *panel, const QString &path);  // 用 cmd 跑脚本，输出进该分区日志
    void takeScriptOutput(ChipPanel *panel);  // 读出已到的一整行并追加
    void finishScript(ChipPanel *panel, int exitCode, bool crashed);  // 脚本结束，恢复执行和改路径
    void appendLog(ChipPanel *panel, const QString &text);  // 追加到日志，勾了自动保存就落盘
    void saveLog(ChipPanel *panel);  // 把当前日志另存为
    void setAutoSave(ChipPanel *panel, bool enabled);  // 打开或关掉该分区的自动保存
    bool openAutoSave(ChipPanel *panel);  // 按设置里的日志目录追加打开

    ChipPanel m_h573;  // 上面的 STM32H573
    ChipPanel m_h5f4;  // 下面的 STM32H5F4
};

#endif // PROGRAMDOWNLOADWIDGET_H
