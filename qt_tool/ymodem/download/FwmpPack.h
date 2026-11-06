#ifndef FWMPPACK_H
#define FWMPPACK_H

#include <QByteArray>
#include <QDialog>
#include <QString>

struct FwmpPayload {
    QByteArray bytes;
    quint8 imageType = 0;  // 0 是 NS，1 是 S
};

bool readFwmpPayload(const QString &path, FwmpPayload *payload, QString *error);  // 核对魔数、头、长度和 SHA-256

class QComboBox;
class QEvent;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProcess;
class QPushButton;
class QRadioButton;

/**
 * 把已签名的 bin 打成 FWMP。布局与 tool/qt_tool 下的 PACK_PWMP_S.py、PACK_PWMP_NS.py 相同。
 * 安全侧 image type 和 target component 都是 1，非安全侧都是 0。板型固定为 0（CCU）。
 */
class FwmpPackDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Side {
        Secure,     // tfm_s_signed.bin，类型 1
        NonSecure   // tfm_ns_signed.bin，类型 0
    };

    explicit FwmpPackDialog(Side side, const QString &defaultBin,
                            QWidget *parent = nullptr);  // defaultBin 只在没保存过路径时使用

signals:
    void logged(const QString &text);  // 打包结果，由下载页写进日志框

protected:
    void changeEvent(QEvent *event) override;  // 语言切换时刷新对话框文字

private:
    void retranslateUi();                                      // 按当前语言重写对话框文字
    void chooseBin();                                          // 选择要打包的 bin，并带出同名 fwmp
    void chooseOutput();                                       // 选目录，用 bin 文件名加 .fwmp
    void pack();                                               // 读 bin，写 64 字节头加原内容
    QString pickFile(QLineEdit *edit, const QString &title, const QString &filter,
                     bool save, bool directory = false);       // directory 为真时只选目录
    void suggestOutput(const QString &binPath);                // 输出框跟着 bin 改成同目录同名 .fwmp
    QString sideName() const;                                  // 日志里的 S 或 NS

    Side m_side = Side::NonSecure;
    QString m_binKey;                              // 设置里 bin 路径的键
    QString m_outKey;                              // 设置里 fwmp 路径的键
    QLabel *m_binLabel = nullptr;                  // 「BIN」
    QLineEdit *m_bin = nullptr;                    // 已签名 bin，可手输
    QPushButton *m_browseBin = nullptr;            // 选择 bin
    QLabel *m_outLabel = nullptr;                  // 「FWMP 输出」
    QLineEdit *m_out = nullptr;                    // 输出 fwmp，可手输
    QPushButton *m_browseOut = nullptr;            // 选择输出文件
    QPushButton *m_pack = nullptr;                 // 打包并写入输出路径
};

/**
 * 从 FWMP 里取出原来的 bin。安全侧和非安全侧各记一套路径。
 * 核对魔数、64 字节头、长度和 SHA-256，对不上就不写文件。
 */
class FwmpUnpackDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Side {
        Secure,     // tfm_s_signed.fwmp，类型应为 1
        NonSecure   // tfm_ns_signed.fwmp，类型应为 0
    };

    explicit FwmpUnpackDialog(Side side, const QString &defaultFwmp,
                              QWidget *parent = nullptr);  // defaultFwmp 只在没保存过路径时使用

signals:
    void logged(const QString &text);  // 解包结果，由下载页写进日志框

protected:
    void changeEvent(QEvent *event) override;  // 语言切换时刷新对话框文字

private:
    void retranslateUi();                         // 按当前语言重写对话框文字
    void chooseFwmp();                            // 选择要解开的 fwmp，并带出同名 bin
    void chooseOutput();                          // 选择 bin 输出路径
    void unpack();                                // 校验头和哈希，写出 payload
    void reportFailure(const QString &message);   // 失败写进日志并弹出提示
    QString pickFile(QLineEdit *edit, const QString &title, const QString &filter,
                     bool save, const QString &suffix);  // 从输入框当前路径打开 Qt 文件框
    void suggestOutput(const QString &fwmpPath);  // 输出框跟着 fwmp 改成同目录同名 .bin

    Side m_side = Side::NonSecure;
    QString m_inKey;                              // 设置里 fwmp 路径的键
    QString m_outKey;                             // 设置里 bin 路径的键
    QLabel *m_fwmpLabel = nullptr;                // 「FWMP」
    QLineEdit *m_fwmp = nullptr;                  // 要解开的 fwmp，可手输
    QPushButton *m_browseFwmp = nullptr;          // 选择 fwmp
    QLabel *m_outLabel = nullptr;                 // 「BIN 输出」
    QLineEdit *m_out = nullptr;                   // 输出 bin，可手输
    QPushButton *m_browseOut = nullptr;           // 选择输出文件
    QPushButton *m_unpack = nullptr;              // 解包并写入输出路径
};

class QListWidget;
class QTableWidget;

// 只读对比多个固件：FWMP 打包头，以及内存里的 MCUboot 签名信息。不写回原文件。
class PackageInspectDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PackageInspectDialog(QWidget *parent = nullptr);

protected:
    void changeEvent(QEvent *event) override;

private:
    void retranslateUi();
    void addFiles();     // 多选加入列表，不立刻解析
    void removeFiles();  // 从列表去掉选中的文件
    void compare();      // 重新只读打开列表里的文件，刷新对比

    QListWidget *m_files = nullptr;
    QPushButton *m_add = nullptr;
    QPushButton *m_remove = nullptr;
    QPushButton *m_compare = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_note = nullptr;
};

class QTableWidget;

// 选择一份已打包或已签名的固件，显示打包和签名信息，并可写出解包或去掉签名的 bin。不改原文件。
class FirmwareRestoreDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FirmwareRestoreDialog(QWidget *parent = nullptr);

protected:
    void changeEvent(QEvent *event) override;

private:
    void retranslateUi();
    void chooseFile();     // 选择 bin、fwmp 或 hex，选中后刷新信息
    void refresh();        // 只读打开当前文件，填打包和签名信息
    void writeUnpacked();  // 有 FWMP 时去掉包头，写到窗口里的路径
    void writeUnsigned();  // 去掉 MCUboot 头和签名。打包的先写出解包文件
    void fillOutputPaths();  // 按当前固件填默认输出路径
    void browseUnpacked();
    void browseUnsigned();
    QString askSavePath(const QString &suggested);  // 选择写出路径。取消返回空

    QPushButton *m_choose = nullptr;
    QLabel *m_path = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_unpackedOutLabel = nullptr;
    QLineEdit *m_unpackedOut = nullptr;
    QPushButton *m_unpackedBrowse = nullptr;
    QLabel *m_unsignedOutLabel = nullptr;
    QLineEdit *m_unsignedOut = nullptr;
    QPushButton *m_unsignedBrowse = nullptr;
    QPushButton *m_unpacked = nullptr;
    QPushButton *m_unsigned = nullptr;
    QLabel *m_status = nullptr;
    QString m_file;
};

// 给未签名镜像签名，或给已签名镜像加上 FWMP。和固件还原相反。不改原文件。
class SignPackDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SignPackDialog(QWidget *parent = nullptr);
    void setDownloadBusy(bool busy);  // 下载进行中时停住两个输出按钮

signals:
    void signingChanged(bool running);  // 签名开始或结束。下载页据此停住烧录

protected:
    void changeEvent(QEvent *event) override;

private:
    void retranslateUi();
    void updateProfileText();
    void loadStoredVersions();  // 从 config.h573 或 config.h5f4 读出当前版本，不写回
    void updateButtons();
    void chooseFile();
    void fillOutputPaths();     // 按当前固件填默认输出路径
    void browseSigned();
    void browsePackage();
    void refresh();             // 只读打开当前文件，填打包和签名信息
    void outputSigned();        // 未签名则签名；已有 MCUboot 魔数则不写
    void outputPacked();        // 已签名只打包；未签名先签名再打包；合法 FWMP 不写
    void begin(bool packAfter);
    void startSign(const QByteArray &bytes, bool fileIsBin, bool packAfter);
    void finishSign(bool ok, const QString &message);
    QString askSavePath(const QString &suggested, const QString &filter, const QString &suffix);  // 选择写出路径。取消返回空
    bool writePackedFile(const QByteArray &signedBytes, const QString &out, QString *message);  // 写到选好的路径
    void showResult(bool ok, const QString &message);
    void appendLog(const QString &text);
    void dropTemp();

    QComboBox *m_product = nullptr;
    QLabel *m_profile = nullptr;
    QLabel *m_versionSLabel = nullptr;
    QLineEdit *m_versionS = nullptr;
    QLabel *m_versionNLabel = nullptr;
    QLineEdit *m_versionN = nullptr;
    QRadioButton *m_sideS = nullptr;
    QRadioButton *m_sideN = nullptr;
    QRadioButton *m_dev = nullptr;
    QRadioButton *m_remote = nullptr;
    QPushButton *m_choose = nullptr;
    QLabel *m_path = nullptr;
    QLabel *m_signedOutLabel = nullptr;
    QLineEdit *m_signedOut = nullptr;
    QPushButton *m_signedBrowse = nullptr;
    QLabel *m_fwmpOutLabel = nullptr;
    QLineEdit *m_fwmpEdit = nullptr;
    QPushButton *m_fwmpBrowse = nullptr;
    QTableWidget *m_table = nullptr;
    QPushButton *m_signed = nullptr;
    QPushButton *m_packed = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QLabel *m_status = nullptr;
    QProcess *m_process = nullptr;
    QString m_file;
    QString m_kit;
    QString m_chip;
    QString m_profileName;
    QString m_signInput;
    QString m_expectedSigned;
    QString m_finalSigned;
    QString m_fwmpOut;
    QString m_tempInput;
    quint8 m_imageType = 1;
    bool m_production = false;
    bool m_packAfter = false;
    bool m_downloadBusy = false;
    bool m_signing = false;
};

#endif // FWMPPACK_H
