#ifndef DAOBK_H
#define DAOBK_H

#include <QByteArray>
#include <QDialog>
#include <QString>

class QEvent;
class QLabel;
class QLineEdit;
class QPushButton;

/**
 * 按 gen_da_obk.py 的布局生成 STM32H5 DA obk。
 * 目标地址 0x0FFD0100，加密标记默认 1，权限默认 0x00005077。
 * H573 和当前这份 H5F4 的 DA_Config.obk 都是这个格式。
 */
class DaObk
{
public:
    enum class Error {
        Ok = 0,
        Read,    // 打不开 PEM
        Pem,     // PEM 里没有 Base64
        Point    // DER 里没有 P-256 未压缩公钥点
    };

    static QByteArray fromPublicKey(const QString &pemPath, quint32 permission, quint32 encrypt,
                                    Error *error);  // 由 PEM 公钥或私钥生成 108 字节 obk
};

/**
 * 生成 DA obk 的对话框。公钥、输出路径、权限和加密标记都可以改。
 */
class DaObkDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DaObkDialog(const QString &defaultKey, const QString &defaultOut,
                         QWidget *parent = nullptr);  // 没有保存过路径时用这两个缺省文件

protected:
    void changeEvent(QEvent *event) override;  // 语言切换时刷新对话框文字

private:
    void retranslateUi();   // 按当前语言重写对话框文字
    void chooseKey();       // 选择 PEM 公钥或私钥
    void chooseOutput();    // 选择要写出的 obk
    void generate();        // 按框里的参数生成并覆盖写出

    QLabel *m_keyLabel = nullptr;       // 「根公钥」
    QLineEdit *m_key = nullptr;         // PEM 路径，可手输
    QPushButton *m_browseKey = nullptr; // 选择 PEM
    QLabel *m_outLabel = nullptr;       // 「OBK 输出」
    QLineEdit *m_out = nullptr;         // 输出 obk 路径，可手输
    QPushButton *m_browseOut = nullptr; // 选择输出文件
    QLabel *m_permLabel = nullptr;      // 「权限」
    QLineEdit *m_perm = nullptr;        // SOC 权限，默认 0x00005077
    QLabel *m_encLabel = nullptr;       // 「加密标记」
    QLineEdit *m_enc = nullptr;         // 烧录时是否由芯片加密，H573 用 1
    QPushButton *m_generate = nullptr;  // 生成并写入输出路径
};

#endif // DAOBK_H
