#ifndef TMX_TOOL_H
#define TMX_TOOL_H

#include <QWidget>
#include <QTabWidget>
#include <QHBoxLayout>
#include "serial/SerialAssistant.h"

QT_BEGIN_NAMESPACE
namespace Ui { class TMX_TOOL; }
QT_END_NAMESPACE

class FirmwareWidget;
class FtpWidget;
class NetAssistWidget;
class ProgramDownloadWidget;
class SettingsWidget;
class IoSource;

/**
 * 主窗口：只放页签，并按登录结果决定能不能进固件页。
 * 串口线程、网络套接字、YModem 都留在各自页面里。这里要收发字节时用 serialIoSource / networkIoSource。
 * 固件页要等 evlink 登录成功才能打开。失败时停在第一页，再点固件页会弹出登录窗。程序下载页不拦。
 */
class TMX_TOOL : public QWidget
{
    Q_OBJECT

public:
    TMX_TOOL(QWidget *parent = nullptr);  // 建页签。各页自己拉起后端
    ~TMX_TOOL() override;

    IoSource *serialIoSource() const;   // 串口链路：收 ioDataReceived，发 sendIoData
    IoSource *networkIoSource() const;  // 网络链路，同上

protected:
    void changeEvent(QEvent *event) override;  // 语言切换时刷新标题和页签

private:
    void initUi();              // 建页签，并按登录结果拦固件页
    void onTabChanged(int index);  // 未登录时挡回固件页；登录中先记下要点的页
    void onGateLoginFinished(bool ok, const QString &message);  // 启动登录成功打开启动页；点过的页失败后再弹登录窗
    void showTab(int index);    // 切页，不再次进入拦截
    bool isGuardedTab(int index) const;  // 只有固件页要登录。FTP 插在网络后面，固件是页签 3
    void retranslateUi();       // 按当前语言重写窗口标题和页签
    void applyTabVisibility();  // 按设置显示或藏起串口、网络、FTP 页
    QWidget *pageForLogical(int logical) const;  // 启动页序号对应的页面
    int firstOpenTab() const;   // 固件页以外的第一页，未登录时停在这里

private:
    Ui::TMX_TOOL *ui;
    QTabWidget *m_tool_tab = nullptr;
    SerialAssistant *m_serial_ui = nullptr;  // 串口页，线程和 YModem 在页面内部
    NetAssistWidget *m_net_ui = nullptr;
    FtpWidget *m_ftp_ui = nullptr;        // FTP 页，套接字在它自己的工作线程
    SettingsWidget *m_settings_ui = nullptr;  // 设置页：启动页签和日志目录
    FirmwareWidget *m_firmware_ui = nullptr;  // 固件页：拉网站上的固件列表并下载
    ProgramDownloadWidget *m_program_ui = nullptr;  // 程序下载页，独立于串口和固件页
    int m_returnTab = 0;         // 被挡住时留在这一页
    int m_heldTab = -1;          // 启动登录还没回来时用户点的页，-1 表示没点
    bool m_switching = false;    // 正在自己切页，避免再次进入拦截
    bool m_startupSettled = false;  // 第一次启动登录已经处理过，后面的登录不再改启动页
};

#endif // TMX_TOOL_H
