#ifndef SETTINGSWIDGET_H
#define SETTINGSWIDGET_H

#include <QWidget>
#include <QEvent>

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;

/**
 * 主窗口 Settings 页。只存应用级选项。
 * 串口参数和网络参数仍由各自页面读写。
 */
class SettingsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit SettingsWidget(QWidget *parent = nullptr);  // 建界面并恢复上次选项
    ~SettingsWidget() override;                          // 把当前选项写回 QSettings

    int startupTab() const;  // 下次启动要选中的页签，0 串口、1 网络、2 FTP、3 固件、4 程序下载、5 设置
    bool showSerial() const;   // 主窗口是否显示串口页
    bool showNetwork() const;  // 主窗口是否显示网络页
    bool showFtp() const;      // 主窗口是否显示 FTP / TFTP 页
    bool showFirmware() const; // 主窗口是否显示 evlink 固件页

signals:
    void tabVisibilityChanged();  // 串口、网络、FTP 页的显示勾选变了

public:
    static QString logFilePath(const QString &fileName);       // 保存日志对话框的起始路径
    static QString firmwareFilePath(const QString &fileName);  // 保存固件对话框的起始路径

protected:
    void changeEvent(QEvent *event) override;  // 语言切换时刷新本页文字

private:
    void setupUi();       // 拼 General 分组
    void retranslateUi(); // 按当前语言重写本页控件文字
    void loadSettings();       // 从 QSettings 的 App 组读启动页、语言、日志目录和固件目录
    void saveSettings();       // 把启动页、语言、日志目录和固件目录写入 QSettings
    void browseLogDir();       // 选一个目录填进日志路径
    void browseFirmwareDir();  // 选一个目录填进固件路径
    void applyLanguage();      // 按语言下拉安装或卸下翻译
    void syncStartupCombo();   // 藏起来的页不能再当启动页

    QGroupBox *m_group = nullptr;          // General 分组
    QLabel *m_startupLabel = nullptr;      // 启动页面
    QLabel *m_languageLabel = nullptr;     // 语言
    QLabel *m_logDirLabel = nullptr;           // 日志目录
    QLabel *m_firmwareDirLabel = nullptr;      // 固件目录
    QComboBox *m_startupCombo = nullptr;       // 启动时显示哪一页
    QComboBox *m_languageCombo = nullptr;      // en 或 zh_CN
    QLineEdit *m_logDirEdit = nullptr;         // 保存日志的默认目录，空表示当前目录
    QLineEdit *m_firmwareDirEdit = nullptr;    // 保存固件的默认目录，空表示系统下载目录
    QPushButton *m_browseBtn = nullptr;        // 选择日志目录
    QPushButton *m_firmwareBrowseBtn = nullptr;  // 选择固件目录
    QLabel *m_tabsLabel = nullptr;             // 显示页签
    QCheckBox *m_showSerial = nullptr;         // 显示串口页
    QCheckBox *m_showNetwork = nullptr;        // 显示网络页
    QCheckBox *m_showFtp = nullptr;            // 显示 FTP / TFTP 页
    QCheckBox *m_showFirmware = nullptr;       // 显示 evlink 固件页
};

#endif // SETTINGSWIDGET_H
