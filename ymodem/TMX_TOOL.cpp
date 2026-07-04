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
        config.readBufferTimeoutMs = 50;
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
            QTextEdit *receiveText = m_serial_ui->findChild<QTextEdit*>("receiveText");
            if (receiveText) file.write(receiveText->toPlainText().toUtf8());
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

}

