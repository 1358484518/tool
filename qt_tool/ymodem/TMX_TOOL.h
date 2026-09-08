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
    TMX_TOOL(QWidget *parent = nullptr);
    ~TMX_TOOL() override;

    IoSource *serialIoSource() const;   // 收 ioDataReceived，发 sendIoData
    IoSource *networkIoSource() const;

private:
    void initUi();
    void initSerialBackend();   // 创建 SerialManager 并移到工作线程
    void initYmodemBridge();    // 串口页「YModem 发送」接到传输线程
    void stopYmodemTransfer();  // 停传输：wait 后 delete，不用 deleteLater

signals:
    void serialOpenRequested(const SerialManager::SerialConfig &config);

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
