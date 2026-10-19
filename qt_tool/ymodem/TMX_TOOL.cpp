#include "TMX_TOOL.h"
#include "ui_tmx_tool.h"
#include "network/NetAssistWidget.h"
#include "firmware/FirmwareWidget.h"
#include "download/ProgramDownloadWidget.h"
#include "SettingsWidget.h"

#include <QCoreApplication>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QIcon>
#include <QMessageBox>
#include <QMetaObject>
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
    initSerialBackend();
    initUi();
    initYmodemBridge();
}

TMX_TOOL::~TMX_TOOL()
{
    stopYmodemTransfer();
    if (m_serial_operate) {
        m_serial_operate->disconnect();
        if (m_serial_ui)
            disconnect(m_serial_ui, nullptr, m_serial_operate, nullptr);
        disconnect(this, nullptr, m_serial_operate, nullptr);

        SerialManager *worker = m_serial_operate;
        m_serial_operate = nullptr;
        // 后端一直待在工作线程：就地 close + delete，不要 move 回 UI。
        if (m_serialThread && m_serialThread->isRunning()
            && worker->thread() == m_serialThread) {
            QMetaObject::invokeMethod(worker, [worker]() {
                worker->close();
                delete worker;
            }, Qt::BlockingQueuedConnection);
            m_serialThread->quit();
            m_serialThread->wait(5000);
        } else {
            delete worker;
        }
    }
    delete ui;
}

void TMX_TOOL::initSerialBackend()
{
    m_serialThread = new QThread(this);
    // SerialManager 先不设 parent：有 parent 的 QObject 不能 moveToThread
    m_serial_operate = new SerialManager;
    m_serial_operate->moveToThread(m_serialThread);
    connect(m_serialThread, &QThread::started, m_serial_operate, &SerialManager::initWorker);
    m_serialThread->start();
}

void TMX_TOOL::initUi()
{
    m_serial_ui = new SerialAssistant(this);
    m_net_ui = new NetAssistWidget(this);
    m_settings_ui = new SettingsWidget(this);
    m_firmware_ui = new FirmwareWidget(this);
    m_program_ui = new ProgramDownloadWidget(this);

    m_tool_tab = new QTabWidget(this);
    // 页签条拉到整行宽，再按个数把空位均分。形状用系统默认的方形。
    m_tool_tab->setStyle(new FullWidthTabStyle(m_tool_tab));
    m_tool_tab->tabBar()->setExpanding(true);
    m_tool_tab->addTab(m_serial_ui, tr("Serial"));
    m_tool_tab->addTab(m_net_ui, tr("Network"));
    m_tool_tab->addTab(m_firmware_ui, tr("Firmware"));

    m_tool_tab->addTab(m_program_ui, tr("Program Download"));
    m_tool_tab->addTab(m_settings_ui, tr("Settings"));
    // 下拉 data 与页签序号一致：0 串口、1 网络、2 固件、3 程序下载、4 设置。
    // 启动页若是还没登录的两个页，先停在第一页，等登录成功再过去。
    const int startup = m_settings_ui->startupTab();
    if (isGuardedTab(startup))
        m_tool_tab->setCurrentIndex(0);
    else if (startup >= 0 && startup < m_tool_tab->count())
        m_tool_tab->setCurrentIndex(startup);
    m_returnTab = m_tool_tab->currentIndex();
    connect(m_tool_tab, &QTabWidget::currentChanged, this, &TMX_TOOL::onTabChanged);
    connect(m_firmware_ui, &FirmwareWidget::gateLoginFinished, this, &TMX_TOOL::onGateLoginFinished);


    auto *layout = new QHBoxLayout;
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addWidget(m_tool_tab);
    setLayout(layout);
    setMinimumSize(1000, 850);

    connect(m_serial_ui, &SerialAssistant::openPortRequested, this, [this]() {
        SerialManager::SerialConfig config;
        config.portName = m_serial_ui->selectedPortName();
        config.baudRate = m_serial_ui->selectedBaudRate();
        config.dataBits = m_serial_ui->selectedDataBits();
        config.parity = m_serial_ui->selectedParity();
        config.stopBits = m_serial_ui->selectedStopBits();
        config.flowControl = m_serial_ui->selectedFlowControl();
        config.autoReconnect = m_serial_ui->autoReconnectEnabled();
        config.reconnectIntervalMs = m_serial_ui->reconnectInterval();
        config.readBufferTimeoutMs = 16;
        emit serialOpenRequested(config);
    });
    connect(this, &TMX_TOOL::serialOpenRequested,
            m_serial_operate, &SerialManager::openWithConfig, Qt::QueuedConnection);

    connect(m_serial_ui, &SerialAssistant::closePortRequested,
            m_serial_operate, &SerialManager::close, Qt::QueuedConnection);
    connect(m_serial_ui, &SerialAssistant::sendDataRequested,
            m_serial_operate, &SerialManager::sendBinary, Qt::QueuedConnection);
    connect(m_serial_ui, &SerialAssistant::dtrToggled,
            m_serial_operate, &SerialManager::setDtr, Qt::QueuedConnection);
    connect(m_serial_ui, &SerialAssistant::rtsToggled,
            m_serial_operate, &SerialManager::setRts, Qt::QueuedConnection);

    connect(m_serial_ui, &SerialAssistant::saveLogRequested, this, [this]() {
        const QString fileName = QFileDialog::getSaveFileName(
            this, tr("Save Log"),
            SettingsWidget::logFilePath(QStringLiteral("seriallog.txt")),
            tr("Text Files (*.txt);;All Files (*)"));
        if (fileName.isEmpty())
            return;
        QFile file(fileName);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(m_serial_ui->getReceiveText());
            file.close();
            m_serial_ui->showStatusMessage(tr("Log saved to %1").arg(fileName));
        } else {
            QMessageBox::warning(this, tr("Error"),
                                 tr("Failed to save file"));
        }
    });

    connect(m_serial_operate, &IoSource::ioDataReceived,
            m_serial_ui, &SerialAssistant::onIoData, Qt::QueuedConnection);
    connect(m_serial_operate, &SerialManager::portConnected, this, [this]() {
        m_serial_ui->setConnectionState(true);
        m_serial_ui->showStatusMessage(tr("Connected to %1").arg(m_serial_ui->selectedPortName()));
    });
    connect(m_serial_operate, &SerialManager::portDisconnected, this, [this]() {
        m_serial_ui->setConnectionState(false);
        if (m_serial_ui->autoReconnectEnabled())
            m_serial_ui->showStatusMessage(tr("Port lost, reconnecting..."));
        else
            m_serial_ui->showStatusMessage(tr("Disconnected"));
        stopYmodemTransfer();
    });
    connect(m_serial_operate, &SerialManager::openResult, this,
            [this](bool ok, const QString &errorString) {
        if (ok)
            return;
        m_serial_ui->showStatusMessage(tr("Open failed: %1").arg(errorString));
        QMessageBox::warning(this, tr("Error"),
                             tr("Failed to open port: %1").arg(errorString));
    });
    connect(m_serial_operate, &SerialManager::errorOccurred, this,
            [this](int, const QString &str) {
        m_serial_ui->showStatusMessage(tr("Error: %1").arg(str));
    });
    m_firmware_ui->trySavedLogin();
}

bool TMX_TOOL::isGuardedTab(int index) const
{
    return index == 2 || index == 3;
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
        if (next < 0 && !m_startupSettled)
            next = m_settings_ui->startupTab();
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
            showTab(0);
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
    m_tool_tab->setTabText(0, tr("Serial"));
    m_tool_tab->setTabText(1, tr("Network"));
    m_tool_tab->setTabText(2, tr("Firmware"));
    m_tool_tab->setTabText(3, tr("Program Download"));
    m_tool_tab->setTabText(4, tr("Settings"));
}

IoSource *TMX_TOOL::serialIoSource() const
{
    return m_serial_operate;
}

IoSource *TMX_TOOL::networkIoSource() const
{
    return m_net_ui ? m_net_ui->ioSource() : nullptr;
}

void TMX_TOOL::stopYmodemTransfer()
{
    if (!m_ymodem)
        return;
    m_ymodem->requestStop();
    disconnect(m_serial_operate, &SerialManager::dataReceived, m_ymodem, &QYmodemFile::receive);
    disconnect(m_ymodem, &QYmodemFile::send, m_serial_operate, &SerialManager::sendBinary);
    if (m_ymodem->isRunning())
        m_ymodem->wait(5000);
    delete m_ymodem;
    m_ymodem = nullptr;
}

void TMX_TOOL::initYmodemBridge()
{
    connect(m_serial_ui, &SerialAssistant::ymodemSendRequested, this,
            [this](const QString &filePath) {
        stopYmodemTransfer();
        m_ymodem = new QYmodemFile(QStringList{filePath}, this);

        connect(m_serial_operate, &SerialManager::dataReceived,
                m_ymodem, &QYmodemFile::receive, Qt::QueuedConnection);
        connect(m_ymodem, &QYmodemFile::send,
                m_serial_operate, &SerialManager::sendBinary, Qt::QueuedConnection);

        connect(m_ymodem, &QYmodemFile::transferring, this, [this](const QString &name) {
            m_serial_ui->showStatusMessage(tr("Sending file: %1").arg(name));
        }, Qt::QueuedConnection);
        connect(m_ymodem, &QYmodemFile::tick, this, [this](qint64 sent, qint64 total) {
            if (total > 0) {
                m_serial_ui->showStatusMessage(
                    tr("Sending: %1% (%2/%3 bytes)")
                        .arg(sent * 100 / total).arg(sent).arg(total));
            }
        }, Qt::QueuedConnection);
        connect(m_ymodem, &QYmodemFile::complete, this,
                [this](const QString &name, int result, size_t size) {
            if (result == 0) {
                m_serial_ui->showStatusMessage(
                    tr("Send success: %1, %2 bytes").arg(name).arg(size));
            } else if (result == QXYmodem::XMODEM_ERROR_IDLETIMEOUT) {
                m_serial_ui->showStatusMessage(
                    tr("Send timed out after 5 seconds"));
            } else {
                m_serial_ui->showStatusMessage(
                    tr("Send failed: %1, error: %2").arg(name).arg(result));
            }
            stopYmodemTransfer();
        }, Qt::QueuedConnection);

        m_ymodem->startSend();
        m_serial_ui->showStatusMessage(
            tr("Start YModem send, waiting for device response..."));
    });
}
