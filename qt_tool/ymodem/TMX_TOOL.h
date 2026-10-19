#ifndef TMX_TOOL_H
#define TMX_TOOL_H

#include <QWidget>
#include <QTabWidget>
#include <QHBoxLayout>
#include <QThread>
#include "serial/SerialManager.h"
#include "serial/SerialAssistant.h"
#include "serial/qxymodem.h"

QT_BEGIN_NAMESPACE
namespace Ui { class TMX_TOOL; }
QT_END_NAMESPACE

class FirmwareWidget;
class NetAssistWidget;
class ProgramDownloadWidget;
class SettingsWidget;
class IoSource;

/**
 * 主窗口：页签是串口、网络、固件、程序下载、设置，下面管串口工作线程和 YModem。
 * 自己不读写串口或套接字，只把 UI 信号转到后端。
 * 固件和程序下载要等 evlink 登录成功才能打开。失败时停在第一页，再点这两个页会弹出登录窗。
 */
class TMX_TOOL : public QWidget
{
    Q_OBJECT

public:
    TMX_TOOL(QWidget *parent = nullptr);  // 建页签、串口线程、YModem 桥接
    ~TMX_TOOL() override;                 // 先停 YModem，再在串口线程里关掉并 delete SerialManager

    IoSource *serialIoSource() const;   // 串口链路：收 ioDataReceived，发 sendIoData
    IoSource *networkIoSource() const;  // 网络链路，同上

protected:
    void changeEvent(QEvent *event) override;  // 语言切换时刷新标题和页签

private:
    void initUi();              // 建页签，把串口 UI 信号接到 SerialManager，并按登录结果拦两个页
    void onTabChanged(int index);  // 未登录时挡回固件和程序下载；登录中先记下要点的页
    void onGateLoginFinished(bool ok, const QString &message);  // 启动登录成功打开启动页；点过的页失败后再弹登录窗
    void showTab(int index);    // 切页，不再次进入拦截
    bool isGuardedTab(int index) const;  // 固件页和程序下载页
    void retranslateUi();       // 按当前语言重写窗口标题和页签
    void initSerialBackend();   // 创建 SerialManager 并移到工作线程
    void initYmodemBridge();    // 串口页「YModem 发送」接到传输线程
    void stopYmodemTransfer();  // 停传输：wait 后 delete，不用 deleteLater

signals:
    void serialOpenRequested(const SerialManager::SerialConfig &config);  // 把打开参数丢给串口线程

private:
    Ui::TMX_TOOL *ui;
    QTabWidget *m_tool_tab = nullptr;
    SerialManager *m_serial_operate = nullptr;  // 串口后端，无 parent，一直活在工作线程
    SerialAssistant *m_serial_ui = nullptr;
    NetAssistWidget *m_net_ui = nullptr;
    SettingsWidget *m_settings_ui = nullptr;  // 设置页：启动页签和日志目录
    FirmwareWidget *m_firmware_ui = nullptr;  // 固件页：拉网站上的固件列表并下载
    ProgramDownloadWidget *m_program_ui = nullptr;  // 程序下载页，独立于串口和固件页
    QThread *m_serialThread = nullptr;
    QYmodemFile *m_ymodem = nullptr;            // 有传输任务时才创建
    int m_returnTab = 0;         // 被挡住时留在这一页
    int m_heldTab = -1;          // 启动登录还没回来时用户点的页，-1 表示没点
    bool m_switching = false;    // 正在自己切页，避免再次进入拦截
    bool m_startupSettled = false;  // 第一次启动登录已经处理过，后面的登录不再改启动页
};

#endif // TMX_TOOL_H
