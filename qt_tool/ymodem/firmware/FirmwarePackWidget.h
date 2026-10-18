#ifndef FIRMWAREPACKWIDGET_H
#define FIRMWAREPACKWIDGET_H

#include <QGroupBox>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

/**
 * 固件组包控件，做法和 FWGenerator 一致。
 * 校验文件是 10 字节小端头；升级文件把项目号和大端头加在固件前面。
 * 结果写到源文件旁边，打包信息显示在文本框里。项目、路径、后缀和打包信息记在 QSettings 的 Firmware 组。
 */
class FirmwarePackWidget : public QGroupBox
{
    Q_OBJECT

public:
    explicit FirmwarePackWidget(QWidget *parent = nullptr);  // 建界面并恢复上次的项目、路径、后缀和打包信息
    void saveSettings() const;  // 把当前组包内容写回 Firmware 组

signals:
    void message(const QString &text);  // 成功路径或失败原因，写到固件页状态栏

protected:
    void changeEvent(QEvent *event) override;  // 语言切换时刷新文字

private:
    void retranslateUi();          // 按当前语言重写本控件文字
    void loadSettings();           // 从 Firmware 组恢复项目、路径、后缀和打包信息
    void browseFile();             // 弹出选择固件框
    void writeCheckFile();         // 按校验后缀写出头文件，原名末尾的 .bin 去掉
    void writeUpdateFile();        // 按固件后缀写出升级文件，原名末尾的 .bin 去掉
    void clearInfo();              // 清掉打包信息文本
    QString suffixText(bool update) const;  // 取对应后缀，空了就回到默认
    bool packFile(bool update);    // 读源文件、算 CRC16-MODBUS 并写目标，失败返回 false

    QLabel *m_projectLabel = nullptr;       // 选择项目
    QComboBox *m_projectCombo = nullptr;    // 项目，当前项下标就是升级头里的项目号
    QLineEdit *m_pathEdit = nullptr;        // 源固件路径
    QLabel *m_checkSuffixLabel = nullptr;   // 校验后缀
    QLineEdit *m_checkSuffixEdit = nullptr; // 接在去掉末尾 .bin 的原名后面，默认 -CHECK.bin
    QLabel *m_firmwareSuffixLabel = nullptr;  // 固件后缀
    QLineEdit *m_firmwareSuffixEdit = nullptr;  // 接在去掉末尾 .bin 的原名后面，默认 -UPDATE.bin
    QPushButton *m_browseBtn = nullptr;     // 选择文件
    QPushButton *m_checkBtn = nullptr;      // 生成校验文件
    QPushButton *m_updateBtn = nullptr;     // 生成升级文件
    QPushButton *m_clearBtn = nullptr;      // 清除打包信息
    QPlainTextEdit *m_info = nullptr;       // 分组顶部的打包信息，和左侧校验文本对齐，超过 1MB 丢掉旧内容
};

#endif // FIRMWAREPACKWIDGET_H
