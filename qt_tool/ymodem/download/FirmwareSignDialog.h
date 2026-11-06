#ifndef FIRMWARESIGNDIALOG_H
#define FIRMWARESIGNDIALOG_H

#include <QDialog>
#include <QStringList>

class QComboBox;
class QEvent;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProcess;
class QPushButton;

/**
 * 固件签名窗口。六个产品共用 sign_kit，用 --chip 选择 H573 或 H5F4 布局。
 * 开发签名走本地开发私钥，远程签名强制走内网正式密钥。
 * 产物写在所选 bin 旁边，文件名加 _signed。
 */
class FirmwareSignDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FirmwareSignDialog(QWidget *parent = nullptr);
    void setDownloadBusy(bool busy);  // 下载进行中时停住签名按钮

signals:
    void signingChanged(bool running);  // 签名开始或结束。下载页据此停住烧录

protected:
    void changeEvent(QEvent *event) override;

private:
    void retranslateUi();
    void updateProfileText();
    void loadStoredVersions();  // 从 config.h573 或 config.h5f4 读出当前版本
    void browse(QLineEdit *edit);
    void startSign(bool production);
    void startNext();
    void finishSign(bool ok, const QString &message);
    void appendLog(const QString &text);
    void updateButtons();

    QComboBox *m_product = nullptr;
    QLabel *m_profile = nullptr;
    QLabel *m_versionSLabel = nullptr;
    QLineEdit *m_versionS = nullptr;
    QLabel *m_versionNLabel = nullptr;
    QLineEdit *m_versionN = nullptr;
    QLabel *m_secureLabel = nullptr;
    QLineEdit *m_secure = nullptr;
    QPushButton *m_secureBrowse = nullptr;
    QLabel *m_appLabel = nullptr;
    QLineEdit *m_app = nullptr;
    QPushButton *m_appBrowse = nullptr;
    QPushButton *m_dev = nullptr;
    QPushButton *m_remote = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QLabel *m_status = nullptr;
    QProcess *m_process = nullptr;
    QString m_kit;
    QString m_chip;
    QString m_profileName;
    bool m_production = false;
    QStringList m_kinds;
    QStringList m_paths;
    QStringList m_versions;
    int m_job = 0;
    bool m_downloadBusy = false;
    bool m_signing = false;
};

#endif // FIRMWARESIGNDIALOG_H
