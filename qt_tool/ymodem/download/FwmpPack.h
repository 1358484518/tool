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
    void loadPaths();    // 打开时填回上次的文件列表
    void savePaths() const;

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
    void loadPaths();        // 打开时填回上次的固件和输出路径
    void savePaths() const;
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
    void loadPaths();           // 打开时填回上次的固件和输出路径
    void savePaths() const;
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
