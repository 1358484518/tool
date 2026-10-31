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
 * 目标地址 0x0FFD0100。H573 和 H5F4 都是加密型号，加密标记固定写 1。
 * 权限是 AN6008 4.1.4 的低 16 位，默认 0x00005077。
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
 * 生成 DA obk 的对话框。公钥和输出路径可以改。
 * 权限是一张和 Trusted Package Creator 一样的表，按位点选。加密标记固定为 1。
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
    void retranslateUi();                  // 按当前语言重写对话框文字
    void chooseKey();                      // 选择 PEM 公钥或私钥
    void chooseOutput();                   // 选择要写出的 obk
    void generate();                       // 按框里的参数生成并覆盖写出
    void setPermission(quint32 value);     // 点亮有效位并刷新十六进制，保留位保持 0
    void refreshPermissionText();          // 把当前点选写成 0x........
    quint32 permission() const;            // 读出点选得到的权限
    void onBitToggled(bool checked);       // 有效位在 0 和 1 之间切换后刷新十六进制

    QLabel *m_keyLabel = nullptr;          // 「根公钥」
    QLineEdit *m_key = nullptr;            // PEM 路径，可手输
    QPushButton *m_browseKey = nullptr;    // 选择 PEM
    QLabel *m_outLabel = nullptr;          // 「OBK 输出」
    QLineEdit *m_out = nullptr;            // 输出 obk 路径，可手输
    QPushButton *m_browseOut = nullptr;    // 选择输出文件
    QLabel *m_permLabel = nullptr;         // 「权限」
    QLabel *m_permValue = nullptr;         // 点选合成的权限，形如 0x00005077
    QLabel *m_encLabel = nullptr;          // 固定显示加密标记 1，不能改
    QLabel *m_descHeader = nullptr;        // 表左上角「说明」
    QLabel *m_maskHeader = nullptr;        // 表头「权限掩码」，横跨 16 位
    QLabel *m_coreLabel = nullptr;         // 左侧竖排 AP - M33，从位号跨到按钮
    QLabel *m_groupLabel[4] = {};          // 回退、非侵入调试、安全调试、非安全调试
    QLabel *m_bitName[16] = {};            // 下标是 bit 号，格子里竖排的名称
    QPushButton *m_bit[16] = {};           // 下标是 bit 号。保留位灰色禁用，有效位可切换
    QPushButton *m_generate = nullptr;     // 生成并写入输出路径
};

#endif // DAOBK_H
