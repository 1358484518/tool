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

class QEvent;
class QLabel;
class QLineEdit;
class QPushButton;

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

#endif // FWMPPACK_H
