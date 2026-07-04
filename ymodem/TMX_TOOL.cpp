#include "TMX_TOOL.h"
#include "ui_tmx_tool.h"

TMX_TOOL::TMX_TOOL(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::TMX_TOOL)
{
    ui->setupUi(this);

    m_tool_table = new QTableWidget;
    QHBoxLayout *layout = new QHBoxLayout;
    layout->addWidget(m_tool_table);
    setLayout(layout);
}

TMX_TOOL::~TMX_TOOL()
{
    delete ui;
}

