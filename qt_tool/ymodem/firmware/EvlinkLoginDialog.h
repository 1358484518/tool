#ifndef EVLINKLOGINDIALOG_H
#define EVLINKLOGINDIALOG_H

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;

/**
 * evlink 登录窗。固件页和程序下载页还没登录成功时，点页签弹出这一层。
 * 只收集邮箱和密码，登录请求由固件页发出。取消则不改已保存的账号。
 */
class EvlinkLoginDialog : public QDialog
{
    Q_OBJECT

public:
    explicit EvlinkLoginDialog(QWidget *parent = nullptr);  // 建邮箱、密码和登录按钮
    void setAccount(const QString &email, const QString &password);  // 用固件页当前账号填好，方便改成别的
    QString email() const;     // 去掉首尾空白的邮箱
    QString password() const;  // 密码原样，空格也算
    void showFailure(const QString &message);  // 登录失败后恢复输入，并把原因写在窗里

signals:
    void signInRequested(const QString &email, const QString &password);  // 点登录后交给固件页去请求

protected:
    void changeEvent(QEvent *event) override;  // 语言切换时刷新本窗文字

private:
    void retranslateUi();  // 按当前语言重写本窗文字
    void submit();         // 邮箱和密码都有才发出登录请求
    void setBusy(bool busy);  // 请求进行中时先不能改账号，也不能再点登录

    QLabel *m_emailLabel = nullptr;      // 邮箱
    QLabel *m_passwordLabel = nullptr;   // 密码
    QLineEdit *m_emailEdit = nullptr;    // 登录邮箱
    QLineEdit *m_passwordEdit = nullptr; // 登录密码，不回显
    QLabel *m_status = nullptr;          // 正在登录或失败原因
    QPushButton *m_signIn = nullptr;     // 发出登录请求
    QPushButton *m_cancel = nullptr;     // 关掉窗口，不保存这次输入
    bool m_busy = false;                 // 这次登录还没返回
};

#endif // EVLINKLOGINDIALOG_H
