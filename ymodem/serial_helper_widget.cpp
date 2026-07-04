#include "serial_helper_widget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QMessageBox>
#include <QFileDialog>
#include <QFile>
#include <QDateTime>
#include <QFont>
#include <QIntValidator>
#include <QTextCursor>
#include <QHeaderView>
#include <QTableWidgetItem>
#include <QInputDialog>

SerialHelperWidget::SerialHelperWidget(QWidget *parent)
    : QWidget{parent}
{
    initUI();
    loadPortList();
    updateUIState(false);

    // Init core objects
    m_serial = new QSerialPort(this);
    m_autoSendTimer = new QTimer(this);
    m_autoSendTimer->setSingleShot(false);
    m_multiSendTimer = new QTimer(this);
    m_multiSendTimer->setSingleShot(false);
    m_timeTimer = new QTimer(this);
    m_timeTimer->setInterval(1000);

    // Connect all signals
    connect(m_autoSendTimer, &QTimer::timeout, this, &SerialHelperWidget::onSendClicked);
    connect(m_multiSendTimer, &QTimer::timeout, this, &SerialHelperWidget::onMultiSendTimer);
    connect(m_timeTimer, &QTimer::timeout, this, &SerialHelperWidget::onUpdateTime);
    connect(m_serial, &QSerialPort::readyRead, this, &SerialHelperWidget::onSerialReadyRead);
    connect(m_serial, &QSerialPort::errorOccurred, this, &SerialHelperWidget::onSerialError);

    m_timeTimer->start();
    onUpdateTime();
}

SerialHelperWidget::~SerialHelperWidget()
{
    // ========== 析构第一道防线：标记正在销毁，所有槽函数直接返回 ==========
    m_isDestroying = true;

    // ========== 第二道防线：切断所有信号槽连接，杜绝回调 ==========
    this->disconnect();

    // ========== 第三道防线：先停所有定时器，再释放 ==========
    if (m_autoSendTimer) {
        m_autoSendTimer->stop();
        m_autoSendTimer->disconnect();
    }
    if (m_multiSendTimer) {
        m_multiSendTimer->stop();
        m_multiSendTimer->disconnect();
    }
    if (m_timeTimer) {
        m_timeTimer->stop();
        m_timeTimer->disconnect();
    }

    // ========== 第四道防线：安全清空表格，断开所有cell按钮信号 ==========
    clearMultiTableSafe();

    // ========== 第五道防线：串口先断信号再关闭 ==========
    if (m_serial) {
        m_serial->disconnect();
        if (m_serial->isOpen()) {
            m_serial->clear();
            m_serial->close();
        }
    }
}

void SerialHelperWidget::closeEvent(QCloseEvent *event)
{
    m_isDestroying = true;

    // Stop all timers immediately
    if (m_autoSendTimer) m_autoSendTimer->stop();
    if (m_multiSendTimer) m_multiSendTimer->stop();
    if (m_timeTimer) m_timeTimer->stop();

    // Close serial port
    if (m_serial && m_serial->isOpen()) {
        m_serial->clear();
        m_serial->close();
    }

    event->accept();
}

void SerialHelperWidget::initUI()
{
    resize(1100, 750);
    setWindowTitle("Serial Helper");
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(8);
    mainLayout->setContentsMargins(8,8,8,8);

    QHBoxLayout* topLayout = new QHBoxLayout();
    topLayout->setSpacing(12);

    // Left receive area
    m_edtRecv = new QPlainTextEdit(this);
    m_edtRecv->setReadOnly(true);
    QFont termFont("Consolas", 10);
    m_edtRecv->setFont(termFont);
    topLayout->addWidget(m_edtRecv, 5);

    // Right config panel (fixed 240px)
    QWidget* configWidget = new QWidget(this);
    configWidget->setFixedWidth(240);
    QVBoxLayout* configLayout = new QVBoxLayout(configWidget);
    configLayout->setSpacing(8);
    configLayout->setContentsMargins(0,0,0,0);

    QLabel* lblPort = new QLabel("Port Select", this);
    lblPort->setStyleSheet("font-weight: bold;");
    m_cbPort = new QComboBox(this);
    m_btnRefreshPort = new QPushButton("Refresh", this);
    QHBoxLayout* portBtnLayout = new QHBoxLayout();
    portBtnLayout->addWidget(m_cbPort, 1);
    portBtnLayout->addWidget(m_btnRefreshPort);
    connect(m_btnRefreshPort, &QPushButton::clicked, this, &SerialHelperWidget::onRefreshPorts);

    QFormLayout* paramForm = new QFormLayout();
    paramForm->setLabelAlignment(Qt::AlignRight);
    paramForm->setSpacing(6);

    m_cbBaudrate = new QComboBox(this);
    m_cbBaudrate->addItems({"1200","2400","4800","9600","19200","38400","57600","115200","230400","460800","921600"});
    m_cbBaudrate->setCurrentText("115200");

    m_cbStopBits = new QComboBox(this);
    m_cbStopBits->addItems({"1","1.5","2"});
    m_cbStopBits->setCurrentText("1");

    m_cbDataBits = new QComboBox(this);
    m_cbDataBits->addItems({"5","6","7","8"});
    m_cbDataBits->setCurrentText("8");

    m_cbParity = new QComboBox(this);
    m_cbParity->addItems({"None","Odd","Even","Mark","Space"});

    paramForm->addRow("Baudrate", m_cbBaudrate);
    paramForm->addRow("Stop Bit", m_cbStopBits);
    paramForm->addRow("Data Bit", m_cbDataBits);
    paramForm->addRow("Parity", m_cbParity);

    m_btnOpenClose = new QPushButton("Open Port", this);
    m_btnOpenClose->setMinimumHeight(32);
    connect(m_btnOpenClose, &QPushButton::clicked, this, &SerialHelperWidget::onOpenCloseClicked);

    QHBoxLayout* opBtnLayout = new QHBoxLayout();
    m_btnSaveWindow = new QPushButton("Save Window", this);
    m_btnClearRecv = new QPushButton("Clear Receive", this);
    connect(m_btnClearRecv, &QPushButton::clicked, this, &SerialHelperWidget::onClearRecvClicked);
    connect(m_btnSaveWindow, &QPushButton::clicked, this, &SerialHelperWidget::onSaveLogClicked);
    opBtnLayout->addWidget(m_btnSaveWindow);
    opBtnLayout->addWidget(m_btnClearRecv);

    // Checkbox rows (each widget created once, no double parent)
    m_chkHexDisplay = new QCheckBox("Hex Display", this);
    QHBoxLayout* row1 = new QHBoxLayout();
    m_chkDtr = new QCheckBox("DTR", this);
    m_chkRts = new QCheckBox("RTS", this);
    connect(m_chkDtr, &QCheckBox::toggled, this, &SerialHelperWidget::onDtrToggled);
    connect(m_chkRts, &QCheckBox::toggled, this, &SerialHelperWidget::onRtsToggled);
    row1->addWidget(m_chkHexDisplay);
    row1->addWidget(m_chkDtr);
    row1->addWidget(m_chkRts);

    QHBoxLayout* row2 = new QHBoxLayout();
    m_chkAutoSave = new QCheckBox("Auto Save", this);
    row2->addWidget(m_chkAutoSave);
    row2->addStretch();

    QHBoxLayout* row3 = new QHBoxLayout();
    m_chkTimestamp = new QCheckBox("Timestamp", this);
    m_edtTimestampPeriod = new QLineEdit("1000", this);
    m_edtTimestampPeriod->setFixedWidth(60);
    row3->addWidget(m_chkTimestamp);
    row3->addWidget(m_edtTimestampPeriod);
    row3->addWidget(new QLabel("ms", this));
    row3->addStretch();

    // Assemble config panel
    configLayout->addWidget(lblPort);
    configLayout->addLayout(portBtnLayout);
    configLayout->addLayout(paramForm);
    configLayout->addWidget(m_btnOpenClose);
    configLayout->addSpacing(6);
    configLayout->addLayout(opBtnLayout);
    configLayout->addLayout(row1);
    configLayout->addLayout(row2);
    configLayout->addLayout(row3);
    configLayout->addStretch();

    topLayout->addWidget(configWidget);
    mainLayout->addLayout(topLayout, 5);

    // Send area
    QGroupBox* sendGroup = new QGroupBox(this);
    QVBoxLayout* sendLayout = new QVBoxLayout(sendGroup);
    sendLayout->setSpacing(6);

    m_sendTab = new QTabWidget(this);

    // Single Send Tab
    QWidget* singleTab = new QWidget();
    QVBoxLayout* singleTabLayout = new QVBoxLayout(singleTab);
    singleTabLayout->setContentsMargins(4,4,4,4);

    QHBoxLayout* sendInputLayout = new QHBoxLayout();
    m_edtSend = new QPlainTextEdit(this);
    m_edtSend->setFont(termFont);
    m_edtSend->setMaximumHeight(100);

    QVBoxLayout* sendBtnCol = new QVBoxLayout();
    m_btnSend = new QPushButton("Send", this);
    m_btnSend->setFixedWidth(90);
    m_btnClearSend = new QPushButton("Clear Send", this);
    m_btnClearSend->setFixedWidth(90);
    connect(m_btnSend, &QPushButton::clicked, this, &SerialHelperWidget::onSendClicked);
    connect(m_btnClearSend, &QPushButton::clicked, this, &SerialHelperWidget::onClearSendClicked);
    sendBtnCol->addWidget(m_btnSend);
    sendBtnCol->addWidget(m_btnClearSend);
    sendBtnCol->addStretch();

    sendInputLayout->addWidget(m_edtSend);
    sendInputLayout->addLayout(sendBtnCol);
    singleTabLayout->addLayout(sendInputLayout);

    QHBoxLayout* sendOpt1 = new QHBoxLayout();
    m_chkAutoSend = new QCheckBox("Timed Send", this);
    connect(m_chkAutoSend, &QCheckBox::toggled, this, &SerialHelperWidget::onAutoSendToggled);
    m_edtAutoSendPeriod = new QLineEdit("1000", this);
    m_edtAutoSendPeriod->setFixedWidth(60);
    m_edtAutoSendPeriod->setValidator(new QIntValidator(10, 60000, this));

    m_btnCalcCrc = new QPushButton("Calc CRC16", this);
    m_btnCalcCrc->setFixedWidth(90);
    connect(m_btnCalcCrc, &QPushButton::clicked, this, &SerialHelperWidget::onCalcCrc16);
    m_edtCrcResult = new QLineEdit(this);
    m_edtCrcResult->setFixedWidth(80);
    m_edtCrcResult->setReadOnly(true);

    m_btnOpenFile = new QPushButton("Open File", this);
    m_btnSendFile = new QPushButton("Send File", this);
    m_btnStopSend = new QPushButton("Stop Send", this);
    connect(m_btnOpenFile, &QPushButton::clicked, this, &SerialHelperWidget::onOpenFileClicked);
    connect(m_btnSendFile, &QPushButton::clicked, this, &SerialHelperWidget::onSendFileClicked);

    sendOpt1->addWidget(m_chkAutoSend);
    sendOpt1->addWidget(new QLabel("Period:", this));
    sendOpt1->addWidget(m_edtAutoSendPeriod);
    sendOpt1->addWidget(new QLabel("ms", this));
    sendOpt1->addSpacing(20);
    sendOpt1->addWidget(m_btnCalcCrc);
    sendOpt1->addWidget(m_edtCrcResult);
    sendOpt1->addStretch();
    sendOpt1->addWidget(m_btnOpenFile);
    sendOpt1->addWidget(m_btnSendFile);
    sendOpt1->addWidget(m_btnStopSend);
    singleTabLayout->addLayout(sendOpt1);

    QHBoxLayout* sendOpt2 = new QHBoxLayout();
    m_chkHexSend = new QCheckBox("Hex Send", this);
    m_chkSendNewline = new QCheckBox("Send Newline", this);
    m_chkSendNewline->setChecked(true);
    m_sendProgress = new QProgressBar(this);
    m_sendProgress->setFixedWidth(180);
    m_sendProgress->setValue(0);
    m_sendProgress->setFormat("%p%");

    sendOpt2->addWidget(m_chkHexSend);
    sendOpt2->addWidget(m_chkSendNewline);
    sendOpt2->addStretch();
    sendOpt2->addWidget(m_sendProgress);
    singleTabLayout->addLayout(sendOpt2);

    m_sendTab->addTab(singleTab, "Single Send");

    // Multi Send Tab
    QWidget* multiTab = new QWidget();
    QVBoxLayout* multiLayout = new QVBoxLayout(multiTab);
    multiLayout->setContentsMargins(4,4,4,4);

    m_multiSendTable = new QTableWidget(this);
    m_multiSendTable->setColumnCount(4);
    m_multiSendTable->setHorizontalHeaderLabels({"", "Data (Hex)", "Comment", "Action"});
    m_multiSendTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_multiSendTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_multiSendTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_multiSendTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_multiSendTable->verticalHeader()->setVisible(false);
    multiLayout->addWidget(m_multiSendTable);

    QHBoxLayout* multiBtnLayout = new QHBoxLayout();
    m_btnAddItem = new QPushButton("Add", this);
    m_btnDeleteItem = new QPushButton("Delete", this);
    m_btnClearItems = new QPushButton("Clear All", this);
    m_btnImportList = new QPushButton("Import CSV", this);
    m_btnExportList = new QPushButton("Export CSV", this);
    m_chkMultiLoop = new QCheckBox("Loop Send", this);
    m_edtMultiPeriod = new QLineEdit("1000", this);
    m_edtMultiPeriod->setFixedWidth(60);
    m_edtMultiPeriod->setValidator(new QIntValidator(10, 60000, this));

    connect(m_btnAddItem, &QPushButton::clicked, this, &SerialHelperWidget::onAddMultiItem);
    connect(m_btnDeleteItem, &QPushButton::clicked, this, &SerialHelperWidget::onDeleteMultiItem);
    connect(m_btnClearItems, &QPushButton::clicked, this, &SerialHelperWidget::onClearMultiItems);
    connect(m_btnImportList, &QPushButton::clicked, this, &SerialHelperWidget::onImportMultiList);
    connect(m_btnExportList, &QPushButton::clicked, this, &SerialHelperWidget::onExportMultiList);
    connect(m_chkMultiLoop, &QCheckBox::toggled, this, &SerialHelperWidget::onMultiLoopToggled);

    multiBtnLayout->addWidget(m_btnAddItem);
    multiBtnLayout->addWidget(m_btnDeleteItem);
    multiBtnLayout->addWidget(m_btnClearItems);
    multiBtnLayout->addSpacing(20);
    multiBtnLayout->addWidget(m_btnImportList);
    multiBtnLayout->addWidget(m_btnExportList);
    multiBtnLayout->addStretch();
    multiBtnLayout->addWidget(m_chkMultiLoop);
    multiBtnLayout->addWidget(new QLabel("Period:", this));
    multiBtnLayout->addWidget(m_edtMultiPeriod);
    multiBtnLayout->addWidget(new QLabel("ms", this));
    multiLayout->addLayout(multiBtnLayout);

    m_sendTab->addTab(multiTab, "Multi Send");
    m_sendTab->addTab(new QWidget(), "Protocol Transfer");
    m_sendTab->addTab(new QWidget(), "Help");
    sendLayout->addWidget(m_sendTab);

    mainLayout->addWidget(sendGroup, 3);

    // Status bar
    QHBoxLayout* statusLayout = new QHBoxLayout();
    statusLayout->setContentsMargins(4,2,4,2);

    QLabel* lblSetting = new QLabel("⚙", this);
    m_lblSendCount = new QLabel("S:0", this);
    m_lblRecvCount = new QLabel("R:0", this);
    m_lblCurrentTime = new QLabel(this);
    m_lblCurrentTime->setAlignment(Qt::AlignRight);

    statusLayout->addWidget(lblSetting);
    statusLayout->addSpacing(20);
    statusLayout->addWidget(m_lblSendCount);
    statusLayout->addSpacing(30);
    statusLayout->addWidget(m_lblRecvCount);
    statusLayout->addStretch();
    statusLayout->addWidget(m_lblCurrentTime);
    mainLayout->addLayout(statusLayout);
}

void SerialHelperWidget::clearMultiTableSafe()
{
    if (!m_multiSendTable) return;

    // Disconnect all cell widgets first, avoid signal trigger during delete
    for (int i = 0; i < m_multiSendTable->rowCount(); i++) {
        QWidget* w = m_multiSendTable->cellWidget(i, 3);
        if (w) w->disconnect();
    }
    m_multiSendTable->setRowCount(0);
}

void SerialHelperWidget::loadPortList()
{
    if (m_isDestroying || !m_cbPort) return;
    m_cbPort->clear();
    for (const QSerialPortInfo& info : QSerialPortInfo::availablePorts()) {
        QString text = QString("%1:%2").arg(info.portName(), info.description());
        m_cbPort->addItem(text, info.portName());
    }
}

void SerialHelperWidget::updateUIState(bool opened)
{
    if (m_isDestroying) return;

    if (m_cbPort) m_cbPort->setEnabled(!opened);
    if (m_btnRefreshPort) m_btnRefreshPort->setEnabled(!opened);
    if (m_cbBaudrate) m_cbBaudrate->setEnabled(!opened);
    if (m_cbDataBits) m_cbDataBits->setEnabled(!opened);
    if (m_cbParity) m_cbParity->setEnabled(!opened);
    if (m_cbStopBits) m_cbStopBits->setEnabled(!opened);
    if (m_btnSend) m_btnSend->setEnabled(opened);
    if (m_chkAutoSend) m_chkAutoSend->setEnabled(opened);
    if (m_edtAutoSendPeriod && m_chkAutoSend)
        m_edtAutoSendPeriod->setEnabled(opened && !m_chkAutoSend->isChecked());
    if (m_chkDtr) m_chkDtr->setEnabled(opened);
    if (m_chkRts) m_chkRts->setEnabled(opened);
    if (m_chkMultiLoop) m_chkMultiLoop->setEnabled(opened);

    if (opened) {
        if (m_btnOpenClose) {
            m_btnOpenClose->setText("Close Port");
            m_btnOpenClose->setStyleSheet("QPushButton {background: #ff4444; color: white; border-radius: 3px; padding: 4px;}");
        }
        m_recvBytes = 0;
        m_sendBytes = 0;
        if (m_lblRecvCount) m_lblRecvCount->setText("R:0");
        if (m_lblSendCount) m_lblSendCount->setText("S:0");
        if (m_serial && m_chkDtr)
            m_chkDtr->setChecked(m_serial->pinoutSignals() & QSerialPort::DataTerminalReadySignal);
        if (m_serial && m_chkRts)
            m_chkRts->setChecked(m_serial->pinoutSignals() & QSerialPort::RequestToSendSignal);
    } else {
        if (m_btnOpenClose) {
            m_btnOpenClose->setText("Open Port");
            m_btnOpenClose->setStyleSheet("QPushButton {background: #00aa00; color: white; border-radius: 3px; padding: 4px;}");
        }
        if (m_autoSendTimer) m_autoSendTimer->stop();
        if (m_multiSendTimer) m_multiSendTimer->stop();
        if (m_chkAutoSend) m_chkAutoSend->setChecked(false);
        if (m_chkMultiLoop) m_chkMultiLoop->setChecked(false);
    }
}

void SerialHelperWidget::appendRecvData(const QByteArray &data)
{
    if (m_isDestroying || !m_edtRecv) return;

    QString log;
    if (m_chkTimestamp && m_chkTimestamp->isChecked()) {
        log += QString("[%1] ").arg(QDateTime::currentDateTime().toString("hh:mm:ss.zzz"));
    }
    if (m_chkHexDisplay && m_chkHexDisplay->isChecked()) {
        log += bytesToHexString(data);
    } else {
        log += QString::fromUtf8(data);
    }
    m_edtRecv->appendPlainText(log);
    QTextCursor cursor = m_edtRecv->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_edtRecv->setTextCursor(cursor);
}

void SerialHelperWidget::sendData(const QByteArray &data)
{
    if (m_isDestroying || !m_serial || !m_serial->isOpen() || data.isEmpty()) return;
    qint64 sent = m_serial->write(data);
    if (sent > 0) {
        m_sendBytes += sent;
        if (m_lblSendCount) m_lblSendCount->setText(QString("S:%1").arg(m_sendBytes));
        m_serial->flush();
    }
}

QByteArray SerialHelperWidget::hexStringToBytes(const QString &hex)
{
    QString hexStr = hex.simplified().remove(' ').remove('\n').remove('\r');
    QByteArray bytes;
    for (int i = 0; i < hexStr.size(); i += 2) {
        bool ok;
        bytes.append(static_cast<char>(hexStr.mid(i, 2).toUInt(&ok, 16)));
        if (!ok) return QByteArray();
    }
    return bytes;
}

QString SerialHelperWidget::bytesToHexString(const QByteArray &bytes)
{
    QString hex;
    for (unsigned char c : bytes) {
        hex += QString("%1 ").arg(c, 2, 16, QChar('0')).toUpper();
    }
    return hex.trimmed();
}

uint16_t SerialHelperWidget::crc16Modbus(const uint8_t *data, int len)
{
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

// ==================== Slot Implementations ====================
void SerialHelperWidget::onRefreshPorts()
{
    if (m_isDestroying) return;
    loadPortList();
}

void SerialHelperWidget::onOpenCloseClicked()
{
    if (m_isDestroying || !m_serial) return;

    if (m_serial->isOpen()) {
        m_serial->close();
        updateUIState(false);
        return;
    }

    QString portName = m_cbPort ? m_cbPort->currentData().toString() : "";
    if (portName.isEmpty()) {
        QMessageBox::warning(this, "Error", "No serial port selected");
        return;
    }

    m_serial->setPortName(portName);
    m_serial->setBaudRate(m_cbBaudrate->currentText().toInt());

    QSerialPort::DataBits dataBits = QSerialPort::Data8;
    if (m_cbDataBits) {
        switch (m_cbDataBits->currentText().toInt()) {
            case 5: dataBits = QSerialPort::Data5; break;
            case 6: dataBits = QSerialPort::Data6; break;
            case 7: dataBits = QSerialPort::Data7; break;
            case 8: dataBits = QSerialPort::Data8; break;
        }
    }
    m_serial->setDataBits(dataBits);

    QSerialPort::Parity parity = QSerialPort::NoParity;
    if (m_cbParity) {
        switch (m_cbParity->currentIndex()) {
            case 0: parity = QSerialPort::NoParity; break;
            case 1: parity = QSerialPort::OddParity; break;
            case 2: parity = QSerialPort::EvenParity; break;
            case 3: parity = QSerialPort::MarkParity; break;
            case 4: parity = QSerialPort::SpaceParity; break;
        }
    }
    m_serial->setParity(parity);

    QSerialPort::StopBits stopBits = QSerialPort::OneStop;
    if (m_cbStopBits) {
        if (m_cbStopBits->currentText() == "1") stopBits = QSerialPort::OneStop;
        else if (m_cbStopBits->currentText() == "1.5") stopBits = QSerialPort::OneAndHalfStop;
        else if (m_cbStopBits->currentText() == "2") stopBits = QSerialPort::TwoStop;
    }
    m_serial->setStopBits(stopBits);
    m_serial->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_serial->open(QIODevice::ReadWrite)) {
        QMessageBox::critical(this, "Open Failed", QString("Error: %1").arg(m_serial->errorString()));
        return;
    }
    updateUIState(true);
}

void SerialHelperWidget::onDtrToggled(bool checked)
{
    if (m_isDestroying || !m_serial || !m_serial->isOpen()) return;
    m_serial->setDataTerminalReady(checked);
}

void SerialHelperWidget::onRtsToggled(bool checked)
{
    if (m_isDestroying || !m_serial || !m_serial->isOpen()) return;
    m_serial->setRequestToSend(checked);
}

void SerialHelperWidget::onClearRecvClicked()
{
    if (m_isDestroying || !m_edtRecv) return;
    m_edtRecv->clear();
}

void SerialHelperWidget::onSaveLogClicked()
{
    if (m_isDestroying || !m_edtRecv) return;
    QString path = QFileDialog::getSaveFileName(this, "Save Log", "", "Text File (*.txt)");
    if (path.isEmpty()) return;
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(m_edtRecv->toPlainText().toUtf8());
        file.close();
    }
}

void SerialHelperWidget::onSendClicked()
{
    if (m_isDestroying || !m_serial || !m_serial->isOpen()) return;
    QString input = m_edtSend ? m_edtSend->toPlainText() : "";
    if (input.isEmpty() && (!m_chkSendNewline || !m_chkSendNewline->isChecked())) return;

    QByteArray data;
    if (m_chkHexSend && m_chkHexSend->isChecked()) {
        data = hexStringToBytes(input);
        if (data.isEmpty()) {
            QMessageBox::warning(this, "Error", "Invalid hex format");
            if (m_autoSendTimer) m_autoSendTimer->stop();
            if (m_chkAutoSend) m_chkAutoSend->setChecked(false);
            return;
        }
    } else {
        data = input.toUtf8();
        if (m_chkSendNewline && m_chkSendNewline->isChecked()) {
            data.append("\r\n");
        }
    }
    sendData(data);
}

void SerialHelperWidget::onClearSendClicked()
{
    if (m_isDestroying || !m_edtSend) return;
    m_edtSend->clear();
}

void SerialHelperWidget::onAutoSendToggled(bool checked)
{
    if (m_isDestroying || !m_edtAutoSendPeriod || !m_autoSendTimer) return;
    m_edtAutoSendPeriod->setEnabled(!checked);
    if (checked) {
        bool ok;
        int period = m_edtAutoSendPeriod->text().toInt(&ok);
        if (!ok || period < 10) {
            QMessageBox::warning(this, "Error", "Invalid period");
            m_chkAutoSend->setChecked(false);
            return;
        }
        m_autoSendTimer->start(period);
    } else {
        m_autoSendTimer->stop();
    }
}

void SerialHelperWidget::onCalcCrc16()
{
    if (m_isDestroying || !m_edtSend || !m_edtCrcResult) return;
    QString input = m_edtSend->toPlainText();
    QByteArray data = hexStringToBytes(input);
    if (data.isEmpty()) {
        QMessageBox::warning(this, "Error", "Invalid hex data");
        return;
    }
    uint16_t crc = crc16Modbus(reinterpret_cast<const uint8_t*>(data.constData()), data.size());
    QByteArray crcBytes;
    crcBytes.append(static_cast<char>(crc & 0xFF));
    crcBytes.append(static_cast<char>((crc >> 8) & 0xFF));
    QString crcHex = bytesToHexString(crcBytes);
    m_edtCrcResult->setText(crcHex);

    if (QMessageBox::question(this, "Append CRC", "Append CRC to send data?") == QMessageBox::Yes) {
        m_edtSend->setPlainText(bytesToHexString(data) + " " + crcHex);
    }
}

void SerialHelperWidget::onOpenFileClicked()
{
    if (m_isDestroying) return;
    m_sendFilePath = QFileDialog::getOpenFileName(this, "Select File", "", "All Files (*.*)");
    if (!m_sendFilePath.isEmpty() && m_edtSend) {
        m_edtSend->setPlainText(m_sendFilePath);
    }
}

void SerialHelperWidget::onSendFileClicked()
{
    if (m_isDestroying) return;
    if (m_sendFilePath.isEmpty()) {
        onOpenFileClicked();
        if (m_sendFilePath.isEmpty()) return;
    }
    QMessageBox::information(this, "Tip", "File send can be extended with chunked transfer logic");
}

// ==================== Multi Send Slots ====================
void SerialHelperWidget::onAddMultiItem()
{
    if (m_isDestroying || !m_multiSendTable) return;

    bool ok;
    QString data = QInputDialog::getText(this, "Add Item", "Hex Data:", QLineEdit::Normal, "", &ok);
    if (!ok || data.isEmpty()) return;

    QString comment = QInputDialog::getText(this, "Add Item", "Comment:");
    int row = m_multiSendTable->rowCount();
    m_multiSendTable->insertRow(row);

    QTableWidgetItem* checkItem = new QTableWidgetItem();
    checkItem->setCheckState(Qt::Unchecked);
    checkItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
    m_multiSendTable->setItem(row, 0, checkItem);
    m_multiSendTable->setItem(row, 1, new QTableWidgetItem(data.toUpper()));
    m_multiSendTable->setItem(row, 2, new QTableWidgetItem(comment));

    QPushButton* sendBtn = new QPushButton("Send", this);
    sendBtn->setFixedWidth(60);
    connect(sendBtn, &QPushButton::clicked, this, &SerialHelperWidget::onSendMultiRowClicked);
    m_multiSendTable->setCellWidget(row, 3, sendBtn);
}

void SerialHelperWidget::onDeleteMultiItem()
{
    if (m_isDestroying || !m_multiSendTable) return;
    int row = m_multiSendTable->currentRow();
    if (row >= 0 && row < m_multiSendTable->rowCount()) {
        // Disconnect button signal before remove row
        QWidget* w = m_multiSendTable->cellWidget(row, 3);
        if (w) w->disconnect();
        m_multiSendTable->removeRow(row);
    }
}

void SerialHelperWidget::onClearMultiItems()
{
    if (m_isDestroying || !m_multiSendTable) return;
    if (QMessageBox::question(this, "Confirm", "Clear all items?") != QMessageBox::Yes) return;
    clearMultiTableSafe();
    m_multiLoopIndex = 0;
}

void SerialHelperWidget::onImportMultiList()
{
    if (m_isDestroying || !m_multiSendTable) return;
    QString path = QFileDialog::getOpenFileName(this, "Import CSV", "", "CSV File (*.csv)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    clearMultiTableSafe();
    while (!file.atEnd()) {
        QString line = file.readLine().trimmed();
        if (line.isEmpty()) continue;
        QStringList parts = line.split(',');
        if (parts.size() < 1) continue;

        int row = m_multiSendTable->rowCount();
        m_multiSendTable->insertRow(row);

        QTableWidgetItem* checkItem = new QTableWidgetItem();
        checkItem->setCheckState(Qt::Unchecked);
        checkItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        m_multiSendTable->setItem(row, 0, checkItem);
        m_multiSendTable->setItem(row, 1, new QTableWidgetItem(parts[0].trimmed()));
        m_multiSendTable->setItem(row, 2, new QTableWidgetItem(parts.size() > 1 ? parts[1].trimmed() : ""));

        QPushButton* sendBtn = new QPushButton("Send", this);
        sendBtn->setFixedWidth(60);
        connect(sendBtn, &QPushButton::clicked, this, &SerialHelperWidget::onSendMultiRowClicked);
        m_multiSendTable->setCellWidget(row, 3, sendBtn);
    }
    file.close();
}

void SerialHelperWidget::onExportMultiList()
{
    if (m_isDestroying || !m_multiSendTable) return;
    QString path = QFileDialog::getSaveFileName(this, "Export CSV", "", "CSV File (*.csv)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;

    for (int i = 0; i < m_multiSendTable->rowCount(); i++) {
        QString data = m_multiSendTable->item(i, 1) ? m_multiSendTable->item(i, 1)->text() : "";
        QString comment = m_multiSendTable->item(i, 2) ? m_multiSendTable->item(i, 2)->text() : "";
        file.write(QString("%1,%2\n").arg(data, comment).toUtf8());
    }
    file.close();
}

void SerialHelperWidget::onMultiLoopToggled(bool checked)
{
    if (m_isDestroying || !m_edtMultiPeriod || !m_multiSendTimer || !m_multiSendTable) return;
    m_edtMultiPeriod->setEnabled(!checked);
    if (checked) {
        if (m_multiSendTable->rowCount() == 0) {
            QMessageBox::warning(this, "Error", "Please add items first");
            m_chkMultiLoop->setChecked(false);
            return;
        }
        bool ok;
        int period = m_edtMultiPeriod->text().toInt(&ok);
        if (!ok || period < 10) {
            QMessageBox::warning(this, "Error", "Invalid period");
            m_chkMultiLoop->setChecked(false);
            return;
        }
        m_multiLoopIndex = 0;
        m_multiSendTimer->start(period);
    } else {
        m_multiSendTimer->stop();
    }
}

void SerialHelperWidget::onMultiSendTimer()
{
    if (m_isDestroying || !m_serial || !m_serial->isOpen() || !m_multiSendTable) {
        if (m_multiSendTimer) m_multiSendTimer->stop();
        if (m_chkMultiLoop) m_chkMultiLoop->setChecked(false);
        return;
    }

    int total = m_multiSendTable->rowCount();
    if (total == 0) {
        m_multiSendTimer->stop();
        m_chkMultiLoop->setChecked(false);
        return;
    }

    for (int i = 0; i < total; i++) {
        int idx = (m_multiLoopIndex + i) % total;
        QTableWidgetItem* checkItem = m_multiSendTable->item(idx, 0);
        if (checkItem && checkItem->checkState() == Qt::Checked) {
            QTableWidgetItem* dataItem = m_multiSendTable->item(idx, 1);
            if (dataItem) {
                QByteArray data = hexStringToBytes(dataItem->text());
                if (!data.isEmpty()) sendData(data);
            }
            m_multiLoopIndex = (idx + 1) % total;
            return;
        }
    }
}

void SerialHelperWidget::onSendMultiRowClicked()
{
    if (m_isDestroying || !m_multiSendTable) return;
    QPushButton* btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;
    int row = m_multiSendTable->indexAt(btn->pos()).row();
    if (row < 0 || row >= m_multiSendTable->rowCount()) return;
    onSendMultiRow(row);
}

void SerialHelperWidget::onSendMultiRow(int row)
{
    if (m_isDestroying || !m_serial || !m_serial->isOpen() || !m_multiSendTable) return;
    QTableWidgetItem* dataItem = m_multiSendTable->item(row, 1);
    if (!dataItem) return;

    QByteArray data = hexStringToBytes(dataItem->text());
    if (data.isEmpty()) {
        QMessageBox::warning(this, "Error", "Invalid hex data");
        return;
    }
    sendData(data);
}

// ==================== Serial Slots ====================
void SerialHelperWidget::onSerialReadyRead()
{
    if (m_isDestroying || !m_serial) return;
    QByteArray data = m_serial->readAll();
    if (data.isEmpty()) return;
    m_recvBytes += data.size();
    if (m_lblRecvCount) m_lblRecvCount->setText(QString("R:%1").arg(m_recvBytes));
    appendRecvData(data);
}

void SerialHelperWidget::onSerialError(QSerialPort::SerialPortError err)
{
    if (m_isDestroying || !m_serial) return;
    if (err == QSerialPort::NoError) return;
    if (err == QSerialPort::ResourceError || err == QSerialPort::DeviceNotFoundError) {
        m_serial->close();
        updateUIState(false);
        QMessageBox::warning(this, "Serial Error", m_serial->errorString());
    }
}

void SerialHelperWidget::onUpdateTime()
{
    if (m_isDestroying || !m_lblCurrentTime) return;
    m_lblCurrentTime->setText(QDateTime::currentDateTime().toString("Current Time hh:mm:ss"));
}
