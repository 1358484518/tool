#include "TMX_TOOL.h"
#include "ui_tmx_tool.h"
#include "network/NetAssistWidget.h"
#include "firmware/FirmwareWidget.h"
#include "SettingsWidget.h"

#include <QCoreApplication>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QIcon>
#include <QMessageBox>
#include <QMetaObject>

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

    m_tool_tab = new QTabWidget(this);
    m_tool_tab->addTab(m_serial_ui, tr("Serial"));
    m_tool_tab->addTab(m_net_ui, tr("Network"));
    m_tool_tab->addTab(m_settings_ui, tr("Settings"));
    m_tool_tab->addTab(m_firmware_ui, tr("Firmware"));
    // 下拉里的 data 与上面 addTab 的顺序一致：0 串口、1 网络、2 设置、3 固件。
    const int startup = m_settings_ui->startupTab();
    if (startup >= 0 && startup < m_tool_tab->count())
        m_tool_tab->setCurrentIndex(startup);


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
    m_tool_tab->setTabText(2, tr("Settings"));
    m_tool_tab->setTabText(3, tr("Firmware"));
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
