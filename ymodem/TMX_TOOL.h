#ifndef TMX_TOOL_H
#define TMX_TOOL_H

#include <QWidget>
#include "QTabWidget"
#include "QHBoxLayout"
#include "serial_helper_widget.h"

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
    Ui::TMX_TOOL *ui;
    QTabWidget *m_tool_tab;
    SerialHelperWidget *m_shw;
};
#endif // TMX_TOOL_H
