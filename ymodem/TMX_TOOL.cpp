#include "TMX_TOOL.h"
#include "ui_tmx_tool.h"

TMX_TOOL::TMX_TOOL(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::TMX_TOOL)
{
    ui->setupUi(this);

    m_tool_tab = new QTabWidget;
    QHBoxLayout *layout = new QHBoxLayout;
    layout->addWidget(m_tool_tab);

    m_shw = new SerialHelperWidget;
    m_tool_tab->addTab(m_shw,"串口工具");

    setLayout(layout);
}

TMX_TOOL::~TMX_TOOL()
{
    delete ui;
}

