#include "TMX_TOOL.h"
#include "ui_tmx_tool.h"
#include "network/NetAssistWidget.h"

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
    setWindowTitle(QStringLiteral("TMX 调试助手"));
    setWindowIcon(QIcon(QStringLiteral(":/image/tmx.png")));
    initSerialBackend();
    initUi();
    initYmodemBridge();
}

TMX_TOOL::~TMX_TOOL()
{
    stopYmodemTransfer();
    if (m_serial_operate)
        disconnect(m_serial_operate, nullptr, nullptr, nullptr);
    if (m_serialThread && m_serialThread->isRunning()) {
        QMetaObject::invokeMethod(m_serial_operate, "close", Qt::BlockingQueuedConnection);
        m_serialThread->quit();
        m_serialThread->wait(3000);
    }
    delete m_serial_operate;
    m_serial_operate = nullptr;
    delete ui;
}

void TMX_TOOL::initSerialBackend()
{
    m_serial_operate = new SerialManager;
    m_serialThread = new QThread(this);
    m_serial_operate->moveToThread(m_serialThread);
    m_serialThread->start();
}

void TMX_TOOL::initUi()
{
    m_serial_ui = new SerialAssistant;
    auto *netAssistUi = new NetAssistWidget;

    m_tool_tab = new QTabWidget;
    m_tool_tab->addTab(netAssistUi, QStringLiteral("网络工具"));
    m_tool_tab->addTab(m_serial_ui, QStringLiteral("串口工具"));

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
            this, QStringLiteral("Save Log"), QString(),
            QStringLiteral("Text Files (*.txt);;All Files (*)"));
        if (fileName.isEmpty())
            return;
        QFile file(fileName);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(m_serial_ui->getReceiveText());
            file.close();
            m_serial_ui->showStatusMessage(QStringLiteral("Log saved to ") + fileName);
        } else {
            QMessageBox::warning(this, QStringLiteral("Error"),
                                 QStringLiteral("Failed to save file"));
        }
    });

    connect(m_serial_operate, &SerialManager::dataReceived,
            m_serial_ui, &SerialAssistant::appendReceivedData, Qt::QueuedConnection);
    connect(m_serial_operate, &SerialManager::portConnected, this, [this]() {
        m_serial_ui->setConnectionState(true);
        m_serial_ui->showStatusMessage(
            QStringLiteral("Connected to ") + m_serial_ui->selectedPortName());
    });
    connect(m_serial_operate, &SerialManager::portDisconnected, this, [this]() {
        m_serial_ui->setConnectionState(false);
        m_serial_ui->showStatusMessage(QStringLiteral("Disconnected"));
    });
    connect(m_serial_operate, &SerialManager::openResult, this,
            [this](bool ok, const QString &errorString) {
        if (ok)
            return;
        m_serial_ui->showStatusMessage(QStringLiteral("Open failed: ") + errorString);
        QMessageBox::warning(this, QStringLiteral("Error"),
                             QStringLiteral("Failed to open port: ") + errorString);
    });
    connect(m_serial_operate, &SerialManager::errorOccurred, this,
            [this](QSerialPort::SerialPortError, const QString &str) {
        m_serial_ui->showStatusMessage(QStringLiteral("Error: ") + str);
    });
}

void TMX_TOOL::stopYmodemTransfer()
{
    if (!m_ymodem)
        return;
    m_ymodem->requestStop();
    disconnect(m_serial_operate, &SerialManager::dataReceived, m_ymodem, &QYmodemFile::receive);
    disconnect(m_ymodem, &QYmodemFile::send, m_serial_operate, &SerialManager::sendBinary);
    m_ymodem->deleteLater();
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
            m_serial_ui->showStatusMessage(QStringLiteral("Sending file: ") + name);
        }, Qt::QueuedConnection);
        connect(m_ymodem, &QYmodemFile::tick, this, [this](qint64 sent, qint64 total) {
            if (total > 0) {
                m_serial_ui->showStatusMessage(
                    QStringLiteral("Sending: %1% (%2/%3 bytes)")
                        .arg(sent * 100 / total).arg(sent).arg(total));
            }
        }, Qt::QueuedConnection);
        connect(m_ymodem, &QYmodemFile::complete, this,
                [this](const QString &name, int result, size_t size) {
            if (result == 0) {
                m_serial_ui->showStatusMessage(
                    QStringLiteral("Send success: %1, %2 bytes").arg(name).arg(size));
            } else if (result == QXYmodem::XMODEM_ERROR_IDLETIMEOUT) {
                m_serial_ui->showStatusMessage(
                    QStringLiteral("发送失败：5秒无响应，已自动退出"));
            } else {
                m_serial_ui->showStatusMessage(
                    QStringLiteral("Send failed: %1, error: %2").arg(name).arg(result));
            }
            stopYmodemTransfer();
        }, Qt::QueuedConnection);

        m_ymodem->startSend();
        m_serial_ui->showStatusMessage(
            QStringLiteral("Start YModem send, waiting for device response..."));
    });
}
