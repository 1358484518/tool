#include "TMX_TOOL.h"
#include "ui_tmx_tool.h"
#include "QFile"
#include "QMessageBox"
#include "QFileDialog"

TMX_TOOL::TMX_TOOL(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::TMX_TOOL)
{
    ui->setupUi(this);

    initConnections();
    QHBoxLayout *layout = new QHBoxLayout;

    m_tool_tab = new QTabWidget;
//    m_shw = new SerialHelperWidget;
    m_tool_tab->addTab(m_serial_ui,"串口工具");

    layout->addWidget(m_tool_tab);
    setMinimumSize(1000, 850);
    setLayout(layout);
}

TMX_TOOL::~TMX_TOOL()
{
    delete ui;
}

void TMX_TOOL::initConnections()
{
    m_serial_ui = new SerialAssistant;
    m_serial_operate = new SerialManager;
    // ========== UI -> Backend ==========
    // Open port
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
        config.readBufferTimeoutMs = 0;
        m_serial_operate->setConfig(config);

        if (m_serial_operate->open()) {
            m_serial_ui->setConnectionState(true);
            m_serial_ui->showStatusMessage("Connected to " + config.portName);
        } else {
            m_serial_ui->showStatusMessage("Open failed: " + m_serial_operate->lastError());
            QMessageBox::warning(this, "Error", "Failed to open port: " + m_serial_operate->lastError());
        }
    });

    // Close port
    connect(m_serial_ui, &SerialAssistant::closePortRequested, this, [this]() {
        m_serial_operate->close();
        m_serial_ui->setConnectionState(false);
    });

    // Send data
    connect(m_serial_ui, &SerialAssistant::sendDataRequested, m_serial_operate, &SerialManager::sendBinary);

    // DTR/RTS control
    connect(m_serial_ui, &SerialAssistant::dtrToggled, m_serial_operate, &SerialManager::setDtr);
    connect(m_serial_ui, &SerialAssistant::rtsToggled, m_serial_operate, &SerialManager::setRts);

    // Save log
    connect(m_serial_ui, &SerialAssistant::saveLogRequested, this, [this]() {
        QString fileName = QFileDialog::getSaveFileName(this, "Save Log", "", "Text Files (*.txt);;All Files (*)");
        if (fileName.isEmpty()) return;
        QFile file(fileName);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(m_serial_ui->getReceiveText());
            file.close();
            m_serial_ui->showStatusMessage("Log saved to " + fileName);
        } else {
            QMessageBox::warning(this, "Error", "Failed to save file");
        }
    });

    // ========== Backend -> UI ==========
    connect(m_serial_operate, &SerialManager::dataReceived, m_serial_ui, &SerialAssistant::appendReceivedData);

    connect(m_serial_operate, &SerialManager::portConnected, this, [this]() {
        m_serial_ui->setConnectionState(true);
        m_serial_ui->showStatusMessage("Connected to " + m_serial_operate->getConfig().portName);
    });

    connect(m_serial_operate, &SerialManager::portDisconnected, this, [this]() {
        m_serial_ui->setConnectionState(false);
        m_serial_ui->showStatusMessage("Disconnected");
    });

    connect(m_serial_operate, &SerialManager::errorOccurred, this, [this](QSerialPort::SerialPortError err, const QString &str) {
        Q_UNUSED(err)
        m_serial_ui->showStatusMessage("Error: " + str);
    });

    // ========== ymodem ==========
    // YModem file send
    connect(m_serial_ui, &SerialAssistant::ymodemSendRequested, this, [this](const QString &filePath) {
        // 先停止上一次发送，安全清理，不wait避免死锁
        if (m_ymodem) {
            m_ymodem->requestStop();
            disconnect(m_serial_operate, &SerialManager::dataReceived, m_ymodem, &QYmodemFile::receive);
            disconnect(m_ymodem, &QYmodemFile::send, m_serial_operate, &SerialManager::sendBinary);
            m_ymodem->deleteLater();
            m_ymodem = nullptr;
        }
        qDebug()<<"执行ymodem";
        // 创建实例
        m_ymodem = new QYmodemFile(QStringList{filePath}, this);

        // 数据通路，指定跨线程队列连接，保证线程安全
        connect(m_serial_operate, &SerialManager::dataReceived, m_ymodem, &QYmodemFile::receive, Qt::QueuedConnection);
        connect(m_ymodem, &QYmodemFile::send, m_serial_operate, &SerialManager::sendBinary, Qt::QueuedConnection);

        // 状态提示
        connect(m_ymodem, &QYmodemFile::transferring, this, [this](const QString &name) {
            m_serial_ui->showStatusMessage("Sending file: " + name);
        }, Qt::QueuedConnection);
        connect(m_ymodem, &QYmodemFile::tick, this, [this](qint64 sent, qint64 total) {
            if (total > 0) {
                m_serial_ui->showStatusMessage(QString("Sending: %1% (%2/%3 bytes)").arg(sent*100/total).arg(sent).arg(total));
            }
        }, Qt::QueuedConnection);

        // 完成处理，所有操作都在主线程，安全
        connect(m_ymodem, &QYmodemFile::complete, this, [this](const QString &name, int result, size_t size) {
            if (result == 0) {
                m_serial_ui->showStatusMessage(QString("✅ Send success: %1, %2 bytes").arg(name).arg(size));
            } else {
                m_serial_ui->showStatusMessage(QString("❌ Send failed: %1, error: %2").arg(name).arg(result));
            }
            // 清理
            if (m_ymodem) {
                disconnect(m_serial_operate, &SerialManager::dataReceived, m_ymodem, &QYmodemFile::receive);
                disconnect(m_ymodem, &QYmodemFile::send, m_serial_operate, &SerialManager::sendBinary);
                m_ymodem->requestStop();
                m_ymodem->deleteLater();
                m_ymodem = nullptr;
            }
        }, Qt::QueuedConnection);

        // 开始发送
        m_ymodem->startSend();
        m_serial_ui->showStatusMessage("Start YModem send, waiting for device response...");
    });

}

