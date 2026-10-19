#ifndef PROGRAMDOWNLOADWIDGET_H
#define PROGRAMDOWNLOADWIDGET_H

#include <QByteArray>
#include <QWidget>

class QCheckBox;
class QComboBox;
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
 * 分组里先放应用程序和 boot 路径，下载脚本、擦除脚本两排在这些路径下面。
 * STM32H573 另有一条 boot 路径；空着时先找 bl2.bin，没有再找 bl2.hex。
 * Boot 路径前的下拉来自下载脚本目录里以 BOOT 结尾的文件夹，选中后切到该目录的 bl2。
 * 应用程序空着时，下载脚本仍使用默认的 tfm_ns_signed.bin。
 * 页顶一行 STM32CubeProgrammer 两个分区共用；空着时脚本仍按原来的位置查找。
 * 「生成 DA OBK」用根公钥写出和 gen_da_obk.py 相同的 108 字节 obk。
 * 路径框可以手输，也可以点按钮选。文件在程序目录里就存相对路径，整包搬走仍然对得上。
 * 执行后把脚本输出收进本组日志。
 * 不使用串口页、固件页或已有的 YModem 发送。
 */
class ProgramDownloadWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ProgramDownloadWidget(QWidget *parent = nullptr);  // 建上下两个芯片分组、日志和保存选项
    ~ProgramDownloadWidget() override;                          // 记下路径，并结束还在跑的 cmd 和 STM32_Programmer_CLI

protected:
    void changeEvent(QEvent *event) override;  // 语言切换时刷新本页文字

private:
    /**
     * 一个芯片分区。路径、执行、日志和自动保存各自一份，两片互不影响。
     */
    struct ChipPanel {
        QGroupBox *group = nullptr;        // 分区
        QLabel *pathLabel = nullptr;       // 「下载脚本路径」
        QLineEdit *path = nullptr;         // 下载脚本，可手输；在程序目录里时显示相对路径，缺省是 da_tfm_close.bat
        QPushButton *changePath = nullptr; // 改下载脚本路径，本组有脚本在跑时禁用
        QPushButton *download = nullptr;   // 执行下载，本组有脚本在跑时禁用
        QLabel *erasePathLabel = nullptr;  // 「擦除脚本路径」
        QLineEdit *erasePath = nullptr;    // 擦除脚本，可手输；在程序目录里时显示相对路径，缺省是 regression.bat
        QPushButton *changeErasePath = nullptr;  // 改擦除脚本路径，本组有脚本在跑时禁用
        QPushButton *erase = nullptr;      // 执行擦除，本组有脚本在跑时禁用
        QLabel *appLabel = nullptr;        // 「应用程序」
        QLineEdit *appPath = nullptr;      // 指定的 NS 镜像，可手输；空表示脚本自己找 tfm_ns_signed.bin
        QPushButton *changeApp = nullptr;  // 选择应用程序 bin，本组有脚本在跑时禁用
        QPushButton *defaultApp = nullptr; // 清空应用程序，恢复默认文件
        QLabel *bootLabel = nullptr;       // 「Boot路径」，只有 STM32H573 有
        QComboBox *bootChoice = nullptr;   // H573 的 boot 目录，项是文件夹名里 BOOT 前面的一段
        QLineEdit *bootPath = nullptr;     // H573 的 BL2，可手输；空表示先找 bl2.bin，没有再找 bl2.hex
        QPushButton *changeBoot = nullptr; // 选择 boot 文件，仅 H573，跑脚本时禁用
        QPushButton *defaultBoot = nullptr; // 清空 boot 路径，仅 H573
        QLabel *logLabel = nullptr;        // 「日志」
        QPlainTextEdit *log = nullptr;     // 脚本标准输出和错误
        QPushButton *saveLog = nullptr;    // 把当前日志另存为文件
        QPushButton *clearLog = nullptr;   // 清空本分区日志窗口，不删自动保存文件
        QCheckBox *autoSave = nullptr;     // 勾上后新日志立刻追加落盘，默认勾选
        QProcess *process = nullptr;       // 正在跑的 cmd，空表示没在跑
        QFile *autoFile = nullptr;         // 自动保存文件，没勾选或打开失败时为空
        QByteArray pending;                // 还没凑成一整行的控制台字节
        QString autoFileName;              // 自动保存文件名，不含目录
        bool endSeen = false;              // 已看到脚本收尾，准备结束末尾的 cmd /k
    };

    void setupUi();        // 上 STM32H573，下 STM32H5F4
    void retranslateUi();  // 按当前语言重写本页控件文字
    void loadSettings();   // 读自动保存和各路径；相对路径先按程序目录找，再按工具目录找
    void saveSettings() const;  // 程序目录内的路径存相对路径，其余在工具目录内也存相对路径，目录外存绝对路径
    QString scriptFile(const QString &chipDir, const QString &scriptRelative) const;  // 该芯片 windows-tfm-tools 下的脚本
    void buildChip(ChipPanel *panel, const QString &title, const QString &scriptPath,
                   const QString &erasePath, const QString &autoFileName, bool withBoot);  // 先应用程序和 boot，再下载、擦除两排；withBoot 时加 boot 路径
    void chooseScriptPath(QLineEdit *edit);  // 选一个脚本文件并写回路路径
    void chooseAppPath(QLineEdit *edit);     // 选一个 bin 作为应用程序
    void chooseBootPath(QLineEdit *edit);    // 选 H573 的 boot，hex 或 bin
    void fillBootChoices();                  // 按下载脚本目录里的 *BOOT 重填下拉，并按当前路径选中
    void applyBootChoice(int index);         // 把 boot 路径改到所选目录的 bl2.bin，没有则用 bl2.hex
    void chooseCubeProg();                   // 选两个分区共用的 STM32_Programmer_CLI.exe
    void generateDaObk();                    // 打开对话框，由根公钥生成 DA obk
    void useDefaultApp(QLineEdit *edit);     // 清空这条路径并保存，空着表示用脚本默认
    void setScriptRunning(ChipPanel *panel, bool running);  // 跑脚本时禁用本组按钮；任一组在跑就锁共用编程器路径
    void runScript(ChipPanel *panel, const QString &path);  // 用 cmd 跑脚本，输出进该分区日志
    bool confirmEraseReset();               // 擦除前用红字提醒接上 ST-LINK 和单片机复位线，确定才继续
    void stopProcessTree(ChipPanel *panel);  // 结束 cmd 以及它启动的 STM32_Programmer_CLI
    void takeScriptOutput(ChipPanel *panel);  // 读出已到的一整行并追加
    void finishScript(ChipPanel *panel, int exitCode, bool crashed);  // 脚本结束，恢复执行和改路径
    void appendLog(ChipPanel *panel, const QString &text);  // 追加到日志，勾了自动保存就落盘
    void saveLog(ChipPanel *panel);   // 把当前日志另存为
    void clearLog(ChipPanel *panel);  // 清空该分区日志窗口
    void setAutoSave(ChipPanel *panel, bool enabled);  // 打开或关掉该分区的自动保存
    bool openAutoSave(ChipPanel *panel);  // 按设置里的日志目录追加打开

    ChipPanel m_h573;  // 上面的 STM32H573
    ChipPanel m_h5f4;  // 下面的 STM32H5F4
    QLabel *m_cubeLabel = nullptr;         // 页顶「STM32CubeProgrammer」，两个分区共用
    QLineEdit *m_cubeProg = nullptr;       // STM32_Programmer_CLI.exe，可手输；空表示脚本自己找
    QPushButton *m_changeCube = nullptr;   // 选择编程器程序
    QPushButton *m_defaultCube = nullptr;  // 清空编程器路径，恢复脚本原来的查找
    QPushButton *m_genObk = nullptr;       // 打开生成 DA obk 的对话框，两个分区共用
};

#endif // PROGRAMDOWNLOADWIDGET_H
