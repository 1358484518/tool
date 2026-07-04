#ifndef TMX_TOOL_H
#define TMX_TOOL_H

#include <QWidget>
#include "QTableWidget"
#include "QHBoxLayout"

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
    QTableWidget *m_tool_table;
};
#endif // TMX_TOOL_H
