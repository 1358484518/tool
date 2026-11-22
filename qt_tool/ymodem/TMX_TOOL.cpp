#include "TMX_TOOL.h"
#include "ui_tmx_tool.h"
#include "network/NetAssistWidget.h"
#include "ftp/FtpWidget.h"
#include "firmware/FirmwareWidget.h"
#include "download/ProgramDownloadWidget.h"
#include "SettingsWidget.h"

#include <QCoreApplication>
#include <QEvent>
#include <QIcon>
#include <QProxyStyle>
#include <QStyleOption>
#include <QTimer>

namespace {

/**
 * 主窗口页签铺满上面这一整行。
 * 系统样式只按文字给页签条宽度，右边会空着。页签本身仍用系统的方形。
 */
class FullWidthTabStyle : public QProxyStyle
{
public:
    explicit FullWidthTabStyle(QWidget *parent)  // 跟着主页签一起销毁
        : QProxyStyle()
    {
        setParent(parent);
    }

    QRect subElementRect(QStyle::SubElement element, const QStyleOption *option,
                         const QWidget *widget) const override  // 页签条改成和页签控件一样宽
    {
        QRect rect = QProxyStyle::subElementRect(element, option, widget);
        if (element != QStyle::SE_TabWidgetTabBar || !option)
            return rect;
        const int top = rect.top();
        const int height = rect.height();
        rect.setLeft(option->rect.left());
        rect.setWidth(option->rect.width());
        rect.setTop(top);
        rect.setHeight(height);
        return rect;
    }
};

} // namespace

TMX_TOOL::TMX_TOOL(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::TMX_TOOL)
{
    ui->setupUi(this);
    const QString ver = QCoreApplication::applicationVersion();
    setWindowTitle(ver.isEmpty()
                       ? tr("TMX Debug Assistant")
                       : tr("TMX Debug Assistant %1").arg(ver));
    setWindowIcon(QIcon(QStringLiteral(":/image/tmx.png")));
    initUi();
}

TMX_TOOL::~TMX_TOOL()
{
    // 串口页是子对象，析构时自己停线程。这里只放掉界面生成的 ui。
    delete ui;
}

void TMX_TOOL::initUi()
{
    // 串口页自己创建工作线程。主窗口不再组装波特率，也不再写串口日志。
    m_serial_ui = new SerialAssistant(this);
    m_net_ui = new NetAssistWidget(this);
    m_ftp_ui = new FtpWidget(this);
    m_settings_ui = new SettingsWidget(this);
    m_firmware_ui = new FirmwareWidget(this);
    m_program_ui = new ProgramDownloadWidget(this);

    m_tool_tab = new QTabWidget(this);
    // 页签条拉到整行宽，再按个数把空位均分。形状用系统默认的方形。
    m_tool_tab->setStyle(new FullWidthTabStyle(m_tool_tab));
    m_tool_tab->tabBar()->setExpanding(true);
    m_tool_tab->addTab(m_serial_ui, tr("Serial"));
    m_tool_tab->addTab(m_net_ui, tr("Network"));
    m_tool_tab->addTab(m_ftp_ui, tr("FTP / TFTP"));
    m_tool_tab->addTab(m_firmware_ui, tr("Firmware"));

    m_tool_tab->addTab(m_program_ui, tr("Program Download"));
    m_tool_tab->addTab(m_settings_ui, tr("Settings"));
    connect(m_settings_ui, &SettingsWidget::tabVisibilityChanged, this, [this]() {
        QTimer::singleShot(0, this, &TMX_TOOL::applyTabVisibility);
    });
    applyTabVisibility();
    // 启动页序号是固定的：0 串口、1 网络、2 FTP、3 固件、4 程序下载、5 设置。
    // 藏起来的页不在页签栏里。固件页要等登录，未登录时先停在别的页。
    QWidget *startPage = pageForLogical(m_settings_ui->startupTab());
    int startIndex = m_tool_tab->indexOf(startPage);
    if (startIndex < 0 || m_tool_tab->widget(startIndex) == m_firmware_ui)
        startIndex = firstOpenTab();
    m_tool_tab->setCurrentIndex(startIndex);
    m_returnTab = m_tool_tab->currentIndex();
    connect(m_tool_tab, &QTabWidget::currentChanged, this, &TMX_TOOL::onTabChanged);
    connect(m_firmware_ui, &FirmwareWidget::gateLoginFinished, this, &TMX_TOOL::onGateLoginFinished);


    auto *layout = new QHBoxLayout;
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addWidget(m_tool_tab);
    setLayout(layout);
    setMinimumSize(1000, 850);
    m_firmware_ui->trySavedLogin();
}

bool TMX_TOOL::isGuardedTab(int index) const
{
    return m_tool_tab && m_tool_tab->widget(index) == m_firmware_ui;
}

QWidget *TMX_TOOL::pageForLogical(int logical) const
{
    switch (logical) {
    case 0: return m_serial_ui;
    case 1: return m_net_ui;
    case 2: return m_ftp_ui;
    case 3: return m_firmware_ui;
    case 4: return m_program_ui;
    case 5: return m_settings_ui;
    default: return nullptr;
    }
}

int TMX_TOOL::firstOpenTab() const
{
    if (!m_tool_tab)
        return 0;
    for (int i = 0; i < m_tool_tab->count(); ++i) {
        if (m_tool_tab->widget(i) != m_firmware_ui)
            return i;
    }
    return 0;
}

void TMX_TOOL::applyTabVisibility()
{
    if (!m_tool_tab || !m_settings_ui)
        return;
    const struct {
        QWidget *page;
        bool visible;
    } pages[] = {
        {m_serial_ui, m_settings_ui->showSerial()},
        {m_net_ui, m_settings_ui->showNetwork()},
        {m_ftp_ui, m_settings_ui->showFtp()},
        {m_firmware_ui, m_settings_ui->showFirmware()},
        {m_program_ui, true},
        {m_settings_ui, true},
    };
    QWidget *current = m_tool_tab->currentWidget();
    m_switching = true;
    while (m_tool_tab->count() > 0)
        m_tool_tab->removeTab(0);
    for (const auto &page : pages) {
        if (page.visible && page.page)
            m_tool_tab->addTab(page.page, QString());
    }
    retranslateUi();
    int index = m_tool_tab->indexOf(current);
    if (index < 0)
        index = firstOpenTab();
    m_tool_tab->setCurrentIndex(index);
    m_returnTab = index;
    m_switching = false;
}

void TMX_TOOL::showTab(int index)
{
    if (!m_tool_tab || index < 0 || index >= m_tool_tab->count())
        return;
    m_switching = true;
    m_tool_tab->setCurrentIndex(index);
    m_switching = false;
    m_returnTab = index;
}

void TMX_TOOL::onTabChanged(int index)
{
    if (m_switching)
        return;
    if (!isGuardedTab(index) || m_firmware_ui->isSessionOpen()) {
        m_returnTab = index;
        return;
    }
    const int back = m_returnTab;
    showTab(back);
    if (m_firmware_ui->isGateLoginPending()) {
        m_heldTab = index;
        return;
    }
    if (m_firmware_ui->promptLogin(this))
        showTab(index);
}

void TMX_TOOL::onGateLoginFinished(bool ok, const QString &message)
{
    Q_UNUSED(message);
    if (ok) {
        int next = m_heldTab;
        m_heldTab = -1;
        if (next < 0 && !m_startupSettled) {
            m_startupSettled = true;
            const int index = m_tool_tab ? m_tool_tab->indexOf(pageForLogical(m_settings_ui->startupTab())) : -1;
            if (isGuardedTab(index))
                showTab(index);
            return;
        }
        m_startupSettled = true;
        if (isGuardedTab(next))
            showTab(next);
        return;
    }
    const int wanted = m_heldTab;
    m_heldTab = -1;
    if (!m_startupSettled) {
        m_startupSettled = true;
        if (m_tool_tab && isGuardedTab(m_tool_tab->currentIndex()))
            showTab(firstOpenTab());
    }
    if (wanted < 0)
        return;
    // 等这次网络回调结束再弹窗，避免在回复槽里再套一层事件循环。
    QTimer::singleShot(0, this, [this, wanted]() {
        if (m_firmware_ui->isSessionOpen()) {
            showTab(wanted);
            return;
        }
        if (m_firmware_ui->promptLogin(this))
            showTab(wanted);
    });
}

void TMX_TOOL::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QWidget::changeEvent(event);
}

void TMX_TOOL::retranslateUi()
{
    const QString ver = QCoreApplication::applicationVersion();
    setWindowTitle(ver.isEmpty()
                       ? tr("TMX Debug Assistant")
                       : tr("TMX Debug Assistant %1").arg(ver));
    if (!m_tool_tab)
        return;
    auto setText = [this](QWidget *page, const QString &text) {
        const int index = m_tool_tab->indexOf(page);
        if (index >= 0)
            m_tool_tab->setTabText(index, text);
    };
    setText(m_serial_ui, tr("Serial"));
    setText(m_net_ui, tr("Network"));
    setText(m_ftp_ui, tr("FTP / TFTP"));
    setText(m_firmware_ui, tr("Firmware"));
    setText(m_program_ui, tr("Program Download"));
    setText(m_settings_ui, tr("Settings"));
}

IoSource *TMX_TOOL::serialIoSource() const
{
    return m_serial_ui ? m_serial_ui->ioSource() : nullptr;
}

IoSource *TMX_TOOL::networkIoSource() const
{
    return m_net_ui ? m_net_ui->ioSource() : nullptr;
}

