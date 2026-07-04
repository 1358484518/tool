#ifndef TMX_TOOL_H
#define TMX_TOOL_H

#include <QWidget>
#include "QTabWidget"
#include "QHBoxLayout"
#include "SerialManager.h"
#include "SerialAssistant.h"

QT_BEGIN_NAMESPACE
namespace Ui { class TMX_TOOL; }
QT_END_NAMESPACE

class TMX_TOOL : public QWidget
{
    Q_OBJECT

public:
    TMX_TOOL(QWidget *parent = nullptr);
    ~TMX_TOOL();
private:
    void initConnections();
private:
    Ui::TMX_TOOL *ui;
    QTabWidget *m_tool_tab;
    SerialManager *m_serial_operate;
    SerialAssistant *m_serial_ui;
//    SerialHelperWidget *m_shw;
};
#endif // TMX_TOOL_H
