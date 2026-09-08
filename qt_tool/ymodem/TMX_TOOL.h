#ifndef TMX_TOOL_H
#define TMX_TOOL_H

#include <QWidget>
#include <QTabWidget>
#include <QHBoxLayout>
#include <QThread>
#include "SerialManager.h"
#include "SerialAssistant.h"
#include "qxymodem.h"

QT_BEGIN_NAMESPACE
namespace Ui { class TMX_TOOL; }
QT_END_NAMESPACE

class TMX_TOOL : public QWidget
{
    Q_OBJECT

public:
    TMX_TOOL(QWidget *parent = nullptr);
    ~TMX_TOOL() override;

private:
    void initUi();
    void initSerialBackend();
    void initYmodemBridge();
    void stopYmodemTransfer();

signals:
    void serialOpenRequested(const SerialManager::SerialConfig &config);

private:
    Ui::TMX_TOOL *ui;
    QTabWidget *m_tool_tab = nullptr;
    SerialManager *m_serial_operate = nullptr;
    SerialAssistant *m_serial_ui = nullptr;
    QThread *m_serialThread = nullptr;
    QYmodemFile *m_ymodem = nullptr;
};

#endif // TMX_TOOL_H
