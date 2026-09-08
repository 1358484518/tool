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

class NetAssistWidget;
class IoSource;

/**
 * 主窗口：上面两个页签（网络 / 串口），下面管串口工作线程和 YModem。
 * 自己不读写串口或套接字，只把 UI 信号转到后端。
 */
class TMX_TOOL : public QWidget
{
    Q_OBJECT

public:
    TMX_TOOL(QWidget *parent = nullptr);  // 建页签、串口线程、YModem 桥接
    ~TMX_TOOL() override;                 // 先停 YModem，再 close 串口并归还线程后退出

    IoSource *serialIoSource() const;   // 串口链路：收 ioDataReceived，发 sendIoData
    IoSource *networkIoSource() const;  // 网络链路，同上

private:
    void initUi();              // 建页签，把串口 UI 信号接到 SerialManager
    void initSerialBackend();   // 创建 SerialManager 并移到工作线程
    void initYmodemBridge();    // 串口页「YModem 发送」接到传输线程
    void stopYmodemTransfer();  // 停传输：wait 后 delete，不用 deleteLater

signals:
    void serialOpenRequested(const SerialManager::SerialConfig &config);  // 把打开参数丢给串口线程

private:
    Ui::TMX_TOOL *ui;
    QTabWidget *m_tool_tab = nullptr;
    SerialManager *m_serial_operate = nullptr;  // 串口后端，运行时无 parent
    SerialAssistant *m_serial_ui = nullptr;
    NetAssistWidget *m_net_ui = nullptr;
    QThread *m_serialThread = nullptr;
    QYmodemFile *m_ymodem = nullptr;            // 有传输任务时才创建
};

#endif // TMX_TOOL_H
