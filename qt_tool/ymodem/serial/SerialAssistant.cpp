#include "SerialAssistant.h"
#include "common/ProtocolUtils.h"

#include <QAbstractItemView>
#include <QDateTime>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMessageBox>
#include <QScrollBar>
#include <QSettings>
#include <QTextCodec>
#include <QTextCursor>
#include <QTextStream>
#include <QTimer>
#include <algorithm>

SerialAssistant::SerialAssistant(QWidget *parent)
    : QWidget(parent)
    , m_isConnected(false)
    , m_paused(false)
    , m_rxBytes(0)
    , m_txBytes(0)
{
    setupUi();
    populateBaudRates();
    populateDataBits();
    populateParity();
    populateStopBits();
    populateFlowControl();
    populateLineEndings();

    m_autoSendTimer = new QTimer(this);
    m_autoSendTimer->setSingleShot(false);
    connect(m_autoSendTimer, &QTimer::timeout, this, &SerialAssistant::onAutoSendTimer);
    connect(m_multiSendTable,&QTableWidget::cellDoubleClicked,this,&SerialAssistant::onDoubleSendSelected);
    updatePortList(QSerialPortInfo::availablePorts());
    setWindowTitle(QStringLiteral("Serial Debug Assistant"));
    loadSettings();
}

SerialAssistant::~SerialAssistant()
{
    if (m_autoSendTimer->isActive())
        m_autoSendTimer->stop();
    exportCsvFile(QStringLiteral("./system_control.csv"), false);
    saveSettings();
}

void SerialAssistant::setupUi()
{
    m_mainLayout = new QHBoxLayout(this);
    m_mainLayout->setSpacing(6);
    m_mainLayout->setContentsMargins(6, 6, 6, 6);

    // ========== Left Panel (pure text area) ==========
    m_leftPanel = new QWidget(this);
    auto *leftLayout = new QVBoxLayout(m_leftPanel);
    leftLayout->setSpacing(4);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    // Receive area (2/3 height)
    m_receiveText = new QPlainTextEdit(m_leftPanel);
    m_receiveText->setReadOnly(true);
    m_receiveText->setFont(QFont("Consolas", 10));
    m_receiveText->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_receiveText->setMaximumBlockCount(50000);
    m_receiveText->setUndoRedoEnabled(false);
    m_receiveText->setStyleSheet(
        QStringLiteral("QPlainTextEdit { background-color: #ffffff; color: #000000; border: 1px solid #c0c0c0; }"));
    leftLayout->addWidget(m_receiveText, 2);

    // Send Tab Widget (1/3 height)
    m_sendTab = new QTabWidget(m_leftPanel);
    m_sendTab->setMinimumHeight(220);
    m_sendTab->setStyleSheet("QTabWidget::pane { border: 1px solid #c0c0c0; }");

    // Tab 1: Single Send
    m_singleSendPage = new QWidget(m_sendTab);
    auto *singleLayout = new QHBoxLayout(m_singleSendPage);
    singleLayout->setSpacing(4);
    singleLayout->setContentsMargins(4, 4, 4, 4);

    m_sendText = new QPlainTextEdit(m_singleSendPage);
    m_sendText->setFont(QFont("Consolas", 10));
    m_sendText->setPlaceholderText("Enter command to send... (Ctrl+Enter to send)");
    m_sendText->installEventFilter(this);
    singleLayout->addWidget(m_sendText, 1);

    auto *singleBtnCol = new QVBoxLayout();
    singleBtnCol->setSpacing(4);
    m_sendBtn = new QPushButton("Send", m_singleSendPage);
    m_sendBtn->setMinimumWidth(70);
    m_sendBtn->setEnabled(false);
    m_sendBtn->setStyleSheet("QPushButton { background-color: #2196F3; color: white; font-weight: bold; padding: 4px; border-radius: 2px; } QPushButton:hover { background-color: #1E88E5; } QPushButton:disabled { background-color: #90CAF9; }");
    singleBtnCol->addWidget(m_sendBtn);

    m_clearSendBtn = new QPushButton("Clear", m_singleSendPage);
    m_clearSendBtn->setMinimumWidth(70);
    connect(m_clearSendBtn, &QPushButton::clicked, m_sendText, &QPlainTextEdit::clear);
    singleBtnCol->addWidget(m_clearSendBtn);
    singleBtnCol->addStretch();
    singleLayout->addLayout(singleBtnCol);
    m_sendTab->addTab(m_singleSendPage, "Single Send");

    // Tab 2: Multi Send
    m_multiSendPage = new QWidget(m_sendTab);
    auto *multiLayout = new QVBoxLayout(m_multiSendPage);
    multiLayout->setSpacing(4);
    multiLayout->setContentsMargins(4, 4, 4, 4);

    auto *multiToolbar = new QHBoxLayout();
    multiToolbar->setSpacing(4);
    m_multiAddBtn = new QPushButton("Add", m_multiSendPage);
    m_multiAddBtn->setMaximumWidth(55);
    multiToolbar->addWidget(m_multiAddBtn);
    m_multiDelBtn = new QPushButton("Del", m_multiSendPage);
    m_multiDelBtn->setMaximumWidth(55);
    multiToolbar->addWidget(m_multiDelBtn);
    m_multiImportBtn = new QPushButton("Import", m_multiSendPage);
    m_multiImportBtn->setMaximumWidth(60);
    multiToolbar->addWidget(m_multiImportBtn);
    m_multiExportBtn = new QPushButton("Export", m_multiSendPage);
    m_multiExportBtn->setMaximumWidth(60);
    multiToolbar->addWidget(m_multiExportBtn);
    multiToolbar->addStretch();
    QLabel *intLabel = new QLabel("Int:", m_multiSendPage);
    multiToolbar->addWidget(intLabel);
    m_multiSendIntervalSpin = new QSpinBox(m_multiSendPage);
    m_multiSendIntervalSpin->setRange(0, 5000);
    m_multiSendIntervalSpin->setValue(1000);
    m_multiSendIntervalSpin->setSuffix("ms");
    m_multiSendIntervalSpin->setMaximumWidth(70);
    multiToolbar->addWidget(m_multiSendIntervalSpin);
    m_multiSendBtn = new QPushButton("Send", m_multiSendPage);
    m_multiSendBtn->setEnabled(false);
    m_multiSendBtn->setMinimumWidth(60);
    m_multiSendBtn->setStyleSheet("QPushButton { background-color: #2196F3; color: white; font-weight: bold; padding: 4px; border-radius: 2px; } QPushButton:hover { background-color: #1E88E5; } QPushButton:disabled { background-color: #90CAF9; }");
    multiToolbar->addWidget(m_multiSendBtn);
    multiLayout->addLayout(multiToolbar);

    // Multi send table with Note column
    m_multiSendTable = new QTableWidget(m_multiSendPage);
    m_multiSendTable->setColumnCount(4);
    m_multiSendTable->setRowCount(0);
    m_multiSendTable->setHorizontalHeaderLabels({"", "No.", "Command", "Note"});
    m_multiSendTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_multiSendTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_multiSendTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_multiSendTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_multiSendTable->verticalHeader()->setVisible(false);
    m_multiSendTable->setFont(QFont("Consolas", 9));
    m_multiSendTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_multiSendTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    multiLayout->addWidget(m_multiSendTable, 1);

    m_sendTab->addTab(m_multiSendPage, "Multi Send");
    leftLayout->addWidget(m_sendTab, 1);
    m_mainLayout->addWidget(m_leftPanel, 5);

    // ========== Right Panel (grouped layout) ==========
    m_rightPanel = new QWidget(this);
    m_rightPanel->setMaximumWidth(220);
    auto *rightLayout = new QVBoxLayout(m_rightPanel);
    rightLayout->setSpacing(6);
    rightLayout->setContentsMargins(4, 0, 0, 0);

    // --- Group 1: Port parameters ---
    auto *portGroup = new QGroupBox("Port Config", m_rightPanel);
    auto *portGrid = new QGridLayout(portGroup);
    portGrid->setSpacing(4);
    portGrid->setContentsMargins(6, 14, 6, 6);

    int r = 0;
    portGrid->addWidget(new QLabel("Port:", portGroup), r, 0, Qt::AlignLeft);
    auto *portLayout = new QHBoxLayout();
    portLayout->setSpacing(2);
    m_portCombo = new QComboBox(portGroup);
    m_portCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_portCombo->view()->setMinimumWidth(280);
    portLayout->addWidget(m_portCombo, 1);
    m_refreshBtn = new QPushButton("⟳", portGroup);
    m_refreshBtn->setToolTip("Refresh port list");
    m_refreshBtn->setMaximumWidth(26);
    portLayout->addWidget(m_refreshBtn);
    portGrid->addLayout(portLayout, r, 1);
    r++;

    portGrid->addWidget(new QLabel("Baud:", portGroup), r, 0, Qt::AlignLeft);
    m_baudCombo = new QComboBox(portGroup);
    portGrid->addWidget(m_baudCombo, r, 1);
    r++;

    portGrid->addWidget(new QLabel("Stop:", portGroup), r, 0, Qt::AlignLeft);
    m_stopBitsCombo = new QComboBox(portGroup);
    portGrid->addWidget(m_stopBitsCombo, r, 1);
    r++;

    portGrid->addWidget(new QLabel("Data:", portGroup), r, 0, Qt::AlignLeft);
    m_dataBitsCombo = new QComboBox(portGroup);
    portGrid->addWidget(m_dataBitsCombo, r, 1);
    r++;

    portGrid->addWidget(new QLabel("Parity:", portGroup), r, 0, Qt::AlignLeft);
    m_parityCombo = new QComboBox(portGroup);
    portGrid->addWidget(m_parityCombo, r, 1);
    r++;

    portGrid->addWidget(new QLabel("Flow:", portGroup), r, 0, Qt::AlignLeft);
    m_flowCombo = new QComboBox(portGroup);
    portGrid->addWidget(m_flowCombo, r, 1);
    r++;

    auto *pinRow = new QHBoxLayout();
    pinRow->setSpacing(10);
    m_dtrCheck = new QCheckBox("DTR", portGroup);
    m_dtrCheck->setEnabled(false);
    pinRow->addWidget(m_dtrCheck);
    m_rtsCheck = new QCheckBox("RTS", portGroup);
    m_rtsCheck->setEnabled(false);
    pinRow->addWidget(m_rtsCheck);
    pinRow->addStretch();
    portGrid->addLayout(pinRow, r, 0, 1, 2);
    r++;

    m_autoReconnectCheck = new QCheckBox("Auto Reconnect", portGroup);
    m_autoReconnectCheck->setChecked(true);
    portGrid->addWidget(m_autoReconnectCheck, r, 0, 1, 2);
    rightLayout->addWidget(portGroup);

    // Open/Close button
    m_openCloseBtn = new QPushButton("Open Port", m_rightPanel);
    m_openCloseBtn->setMinimumHeight(28);
    m_openCloseBtn->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; font-weight: bold; padding: 3px; border-radius: 2px; } QPushButton:hover { background-color: #43A047; }");
    rightLayout->addWidget(m_openCloseBtn);

    // YMODEM Send button
    m_ymodemSendBtn = new QPushButton("YMODEM Send", m_rightPanel);
    m_ymodemSendBtn->setMinimumHeight(28);
    m_ymodemSendBtn->setEnabled(false); // 默认未连接时禁用
    m_ymodemSendBtn->setStyleSheet("QPushButton { background-color: #FF9800; color: white; font-weight: bold; padding: 3px; border-radius: 2px; } QPushButton:hover { background-color: #FB8C00; } QPushButton:disabled { background-color: #FFCC80; }");
    connect(m_ymodemSendBtn, &QPushButton::clicked, this, &SerialAssistant::onYmodemSendClicked);
    rightLayout->addWidget(m_ymodemSendBtn);

    // Separator
    auto *line1 = new QFrame(m_rightPanel);
    line1->setFrameShape(QFrame::HLine);
    line1->setFrameShadow(QFrame::Sunken);
    rightLayout->addWidget(line1);

    // --- Group 2: Send settings ---
    auto *sendGroup = new QGroupBox("Send Settings", m_rightPanel);
    auto *sendGrid = new QGridLayout(sendGroup);
    sendGrid->setSpacing(4);
    sendGrid->setContentsMargins(6, 14, 6, 6);
    r = 0;

    m_autoSendCheck = new QCheckBox("Timed Send", sendGroup);
    sendGrid->addWidget(m_autoSendCheck, r, 0, 1, 2);
    r++;

    auto *intervalRow = new QHBoxLayout();
    intervalRow->setSpacing(4);
    intervalRow->addWidget(new QLabel("Interval:", sendGroup));
    m_autoSendIntervalSpin = new QSpinBox(sendGroup);
    m_autoSendIntervalSpin->setRange(10, 60000);
    m_autoSendIntervalSpin->setValue(1000);
    m_autoSendIntervalSpin->setSuffix("ms");
    intervalRow->addWidget(m_autoSendIntervalSpin, 1);
    sendGrid->addLayout(intervalRow, r, 0, 1, 2);
    r++;

    m_hexSendCheck = new QCheckBox("Hex Send", sendGroup);
    sendGrid->addWidget(m_hexSendCheck, r, 0, 1, 2);
    r++;

    // 新增Modbus CRC16选项
    m_addCrc16Check = new QCheckBox("Add CRC16 (Modbus)", sendGroup);
    sendGrid->addWidget(m_addCrc16Check, r, 0, 1, 2);
    r++;

    auto *endRow = new QHBoxLayout();
    endRow->setSpacing(4);
    endRow->addWidget(new QLabel("Line End:", sendGroup));
    m_lineEndingCombo = new QComboBox(sendGroup);
    endRow->addWidget(m_lineEndingCombo, 1);
    sendGrid->addLayout(endRow, r, 0, 1, 2);
    rightLayout->addWidget(sendGroup);

    // Separator
    auto *line2 = new QFrame(m_rightPanel);
    line2->setFrameShape(QFrame::HLine);
    line2->setFrameShadow(QFrame::Sunken);
    rightLayout->addWidget(line2);

    // --- Group 3: Receive settings ---
    auto *recvGroup = new QGroupBox("Receive Settings", m_rightPanel);
    auto *recvGrid = new QGridLayout(recvGroup);
    recvGrid->setSpacing(4);
    recvGrid->setContentsMargins(6, 14, 6, 6);
    r = 0;

    auto *recvBtnRow = new QHBoxLayout();
    recvBtnRow->setSpacing(4);
    m_saveLogBtn = new QPushButton("Save Log", recvGroup);
    recvBtnRow->addWidget(m_saveLogBtn);
    m_clearReceiveBtn = new QPushButton("Clear Recv", recvGroup);
    recvBtnRow->addWidget(m_clearReceiveBtn);
    recvGrid->addLayout(recvBtnRow, r, 0, 1, 2);
    r++;

    m_hexReceiveCheck = new QCheckBox("Hex Disp", recvGroup);
    recvGrid->addWidget(m_hexReceiveCheck, r, 0);
    m_timestampCheck = new QCheckBox("Timestamp", recvGroup);
    recvGrid->addWidget(m_timestampCheck, r, 1);
    r++;

    m_autoScrollCheck = new QCheckBox("Auto Scroll", recvGroup);
    m_autoScrollCheck->setChecked(true);
    recvGrid->addWidget(m_autoScrollCheck, r, 0);
    m_autoWrapCheck = new QCheckBox("Wrap", recvGroup);
    recvGrid->addWidget(m_autoWrapCheck, r, 1);
    r++;

    m_pauseCheck = new QCheckBox("Pause Display", recvGroup);
    recvGrid->addWidget(m_pauseCheck, r, 0, 1, 2);
    rightLayout->addWidget(recvGroup);

    // Reset Counter button
    m_resetCounterBtn = new QPushButton("Reset Counter", m_rightPanel);
    rightLayout->addWidget(m_resetCounterBtn);

    // Separator
    auto *line3 = new QFrame(m_rightPanel);
    line3->setFrameShape(QFrame::HLine);
    line3->setFrameShadow(QFrame::Sunken);
    rightLayout->addWidget(line3);

    // Status area
    auto *countRow = new QHBoxLayout();
    countRow->setSpacing(10);
    m_rxCountLabel = new QLabel("R: 0 B", m_rightPanel);
    countRow->addWidget(m_rxCountLabel);
    m_txCountLabel = new QLabel("S: 0 B", m_rightPanel);
    countRow->addWidget(m_txCountLabel);
    countRow->addStretch();
    rightLayout->addLayout(countRow);

    m_statusLabel = new QLabel("● Disconnected", m_rightPanel);
    m_statusLabel->setStyleSheet("QLabel { color: #f44336; font-weight: bold; }");
    rightLayout->addWidget(m_statusLabel);

    m_portInfoLabel = new QLabel("", m_rightPanel);
    m_portInfoLabel->setWordWrap(true);
    rightLayout->addWidget(m_portInfoLabel);

    rightLayout->addStretch();
    m_mainLayout->addWidget(m_rightPanel);

    // ========== Connections ==========
    connect(m_openCloseBtn, &QPushButton::clicked, this, &SerialAssistant::onOpenCloseClicked);
    connect(m_sendBtn, &QPushButton::clicked, this, &SerialAssistant::onSendClicked);
    connect(m_clearReceiveBtn, &QPushButton::clicked, this, [this]() {
        m_receiveText->clear();
        m_hasRecvData = false;
        emit clearReceivedRequested();
    });
    connect(m_saveLogBtn, &QPushButton::clicked, this, &SerialAssistant::saveLogRequested);
    connect(m_refreshBtn, &QPushButton::clicked, this, [this]() {
        updatePortList(QSerialPortInfo::availablePorts());
        showStatusMessage("Port list refreshed");
        emit refreshPortsRequested();
    });
    connect(m_autoSendCheck, &QCheckBox::toggled, this, &SerialAssistant::onAutoSendToggled);
    connect(m_resetCounterBtn, &QPushButton::clicked, this, &SerialAssistant::onResetCounterClicked);
    connect(m_pauseCheck, &QCheckBox::toggled, this, &SerialAssistant::onPauseToggled);
    connect(m_dtrCheck, &QCheckBox::toggled, this, &SerialAssistant::dtrToggled);
    connect(m_rtsCheck, &QCheckBox::toggled, this, &SerialAssistant::rtsToggled);
    connect(m_autoWrapCheck, &QCheckBox::toggled, this, [this](bool enabled) {
        m_receiveText->setLineWrapMode(enabled ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    });

    connect(m_multiAddBtn, &QPushButton::clicked, this, &SerialAssistant::onMultiSendAddRow);
    connect(m_multiDelBtn, &QPushButton::clicked, this, &SerialAssistant::onMultiSendDeleteRow);
    connect(m_multiImportBtn, &QPushButton::clicked, this, QOverload<>::of(&SerialAssistant::onMultiSendImportCsv));
    connect(m_multiExportBtn, &QPushButton::clicked, this, QOverload<>::of(&SerialAssistant::onMultiSendExportCsv));
    connect(m_multiSendBtn, &QPushButton::clicked, this, &SerialAssistant::onMultiSendSelected);

    const QString csvPath = QStringLiteral("./system_control.csv");
    if (!QFile::exists(csvPath)) {
        importCsvFile(QStringLiteral(":/miscfile/system_control.csv"), false);
        exportCsvFile(csvPath, false);
    } else {
        importCsvFile(csvPath, false);
    }

    m_autoWrapCheck->setChecked(true);
    m_receiveText->setLineWrapMode(QPlainTextEdit::WidgetWidth);
}

void SerialAssistant::populateBaudRates()
{
    QList<qint32> baudRates = {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};
    for (qint32 baud : baudRates) m_baudCombo->addItem(QString::number(baud), baud);
    m_baudCombo->setCurrentText("115200");
}

void SerialAssistant::populateDataBits()
{
    m_dataBitsCombo->addItem("5", QSerialPort::Data5);
    m_dataBitsCombo->addItem("6", QSerialPort::Data6);
    m_dataBitsCombo->addItem("7", QSerialPort::Data7);
    m_dataBitsCombo->addItem("8", QSerialPort::Data8);
    m_dataBitsCombo->setCurrentText("8");
}

void SerialAssistant::populateParity()
{
    m_parityCombo->addItem("None", QSerialPort::NoParity);
    m_parityCombo->addItem("Odd", QSerialPort::OddParity);
    m_parityCombo->addItem("Even", QSerialPort::EvenParity);
    m_parityCombo->addItem("Mark", QSerialPort::MarkParity);
    m_parityCombo->addItem("Space", QSerialPort::SpaceParity);
}

void SerialAssistant::populateStopBits()
{
    m_stopBitsCombo->addItem("1", QSerialPort::OneStop);
    m_stopBitsCombo->addItem("1.5", QSerialPort::OneAndHalfStop);
    m_stopBitsCombo->addItem("2", QSerialPort::TwoStop);
    m_stopBitsCombo->setCurrentText("1");
}

void SerialAssistant::populateFlowControl()
{
    m_flowCombo->addItem("None", QSerialPort::NoFlowControl);
    m_flowCombo->addItem("RTS/CTS", QSerialPort::HardwareControl);
    m_flowCombo->addItem("XON/XOFF", QSerialPort::SoftwareControl);
}

void SerialAssistant::populateLineEndings()
{
    m_lineEndingCombo->addItem("None", 0);
    m_lineEndingCombo->addItem("CR", 1);
    m_lineEndingCombo->addItem("LF", 2);
    m_lineEndingCombo->addItem("CRLF", 3);
    m_lineEndingCombo->setCurrentIndex(0);
}

QStringList SerialAssistant::parseCsvLine(const QString &line)
{
    QStringList fields;
    QString currentField;
    bool inQuotes = false;
    int i = 0;
    const int len = line.length();

    while (i < len) {
        const QChar c = line.at(i);

        if (!inQuotes) {
            if (c == ',') {
                fields.append(currentField);
                currentField.clear();
                i++;
            } else if (c == '"') {
                inQuotes = true;
                i++;
            } else {
                currentField.append(c);
                i++;
            }
        } else {
            if (c == '"') {
                if (i + 1 < len && line.at(i + 1) == '"') {
                    // 连续双引号 -> 转义为单个双引号
                    currentField.append('"');
                    i += 2;
                } else {
                    // 闭合引号
                    inQuotes = false;
                    i++;
                }
            } else {
                currentField.append(c);
                i++;
            }
        }
    }
    fields.append(currentField);
    return fields;

}

QString SerialAssistant::csvEscape(const QString &field)
{
    if (field.contains(',') || field.contains('"') || field.contains('\n') || field.contains('\r'))
    {
        QString escaped = field;
        escaped.replace('"', "\"\"");
        return QString("\"%1\"").arg(escaped);
    }
    return field;
}

QString SerialAssistant::selectedPortName() const { return m_portCombo->currentData().toString(); }
qint32 SerialAssistant::selectedBaudRate() const { return m_baudCombo->currentData().toInt(); }
QSerialPort::DataBits SerialAssistant::selectedDataBits() const { return static_cast<QSerialPort::DataBits>(m_dataBitsCombo->currentData().toInt()); }
QSerialPort::Parity SerialAssistant::selectedParity() const { return static_cast<QSerialPort::Parity>(m_parityCombo->currentData().toInt()); }
QSerialPort::StopBits SerialAssistant::selectedStopBits() const { return static_cast<QSerialPort::StopBits>(m_stopBitsCombo->currentData().toInt()); }
QSerialPort::FlowControl SerialAssistant::selectedFlowControl() const { return static_cast<QSerialPort::FlowControl>(m_flowCombo->currentData().toInt()); }
bool SerialAssistant::autoReconnectEnabled() const { return m_autoReconnectCheck->isChecked(); }
int SerialAssistant::reconnectInterval() const { return 3000; }

void SerialAssistant::setConnectionState(bool connected)
{
    m_isConnected = connected;
    m_portCombo->setEnabled(!connected);
    m_refreshBtn->setEnabled(!connected);
    m_baudCombo->setEnabled(!connected);
    m_dataBitsCombo->setEnabled(!connected);
    m_parityCombo->setEnabled(!connected);
    m_stopBitsCombo->setEnabled(!connected);
    m_flowCombo->setEnabled(!connected);
    m_autoReconnectCheck->setEnabled(!connected);
    m_dtrCheck->setEnabled(connected);
    m_rtsCheck->setEnabled(connected);
    m_sendBtn->setEnabled(connected);
    m_multiSendBtn->setEnabled(connected);
    m_ymodemSendBtn->setEnabled(connected);

    if (connected) {
        m_openCloseBtn->setText("Close Port");
        m_openCloseBtn->setStyleSheet("QPushButton { background-color: #f44336; color: white; font-weight: bold; padding: 3px; border-radius: 2px; } QPushButton:hover { background-color: #E53935; }");
        m_statusLabel->setText("● Connected");
        m_statusLabel->setStyleSheet("QLabel { color: #4CAF50; font-weight: bold; }");
        m_portInfoLabel->setText(QString("%1 @ %2").arg(m_portCombo->currentData().toString(), m_baudCombo->currentText()));
    } else {
        m_openCloseBtn->setText("Open Port");
        m_openCloseBtn->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; font-weight: bold; padding: 3px; border-radius: 2px; } QPushButton:hover { background-color: #43A047; }");
        m_statusLabel->setText("● Disconnected");
        m_statusLabel->setStyleSheet("QLabel { color: #f44336; font-weight: bold; }");
        m_portInfoLabel->setText("");
        m_dtrCheck->setChecked(false);
        m_rtsCheck->setChecked(false);
        if (m_autoSendTimer->isActive()) m_autoSendCheck->setChecked(false);
    }
}

void SerialAssistant::appendReceivedData(const QByteArray &data)
{
    if (data.isEmpty())
        return;

    m_rxBytes += static_cast<quint64>(data.size());
    m_rxCountLabel->setText(QStringLiteral("R: %1 B").arg(m_rxBytes));
    if (m_paused)
        return;

    QString text;
    if (m_timestampCheck->isChecked()) {
        if (m_hasRecvData)
            text += QLatin1Char('\n');
        text += QLatin1Char('[') + getTimestamp() + QStringLiteral("] ");
    }
    if (m_hexReceiveCheck->isChecked())
        text += ProtocolUtils::bytesToHexString(data);
    else
        text += QString::fromLocal8Bit(data);

    m_hasRecvData = true;
    QTextCursor cursor = m_receiveText->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text);
    if (m_autoScrollCheck->isChecked())
        m_receiveText->verticalScrollBar()->setValue(m_receiveText->verticalScrollBar()->maximum());
}

void SerialAssistant::showStatusMessage(const QString &message, int timeout)
{
    Q_UNUSED(timeout)
    m_portInfoLabel->setText(message);
}

void SerialAssistant::updatePortList(const QList<QSerialPortInfo> &ports)
{
    m_portCombo->clear();
    for (const QSerialPortInfo &info : ports) {
        QString portName = info.portName();
        QString description = info.description();
        QString displayText = description.isEmpty() ? portName : QString("%1:%2").arg(portName, description);
        m_portCombo->addItem(displayText, portName);
    }
}

void SerialAssistant::resetCounters()
{
    m_rxBytes = 0;
    m_txBytes = 0;
    m_rxCountLabel->setText("R: 0 B");
    m_txCountLabel->setText("S: 0 B");
}

QByteArray SerialAssistant::getReceiveText()
{
    return m_receiveText->toPlainText().toUtf8();
}

void SerialAssistant::onOpenCloseClicked()
{
    if (!m_isConnected) {
        if (m_portCombo->currentData().toString().isEmpty()) {
            QMessageBox::warning(this, "Warning", "Please select a serial port first!");
            return;
        }
        emit openPortRequested();
    } else {
        emit closePortRequested();
    }
}

void SerialAssistant::onSendClicked()
{
    QString sendStr = m_sendText->toPlainText();
    if (sendStr.isEmpty()) return;
    QByteArray data = processSendData(sendStr);
    if (!data.isEmpty()) {
        m_txBytes += data.size();
        m_txCountLabel->setText(QString("S: %1 B").arg(m_txBytes));
        emit sendDataRequested(data);
    }
}

void SerialAssistant::onAutoSendToggled(bool enabled)
{
    if (enabled) {
        if (!m_isConnected) {
            m_autoSendCheck->setChecked(false);
            QMessageBox::warning(this, "Warning", "Please open serial port first!");
            return;
        }
        m_autoSendTimer->start(m_autoSendIntervalSpin->value());
        m_sendBtn->setEnabled(false);
        m_multiSendBtn->setEnabled(false);
    } else {
        m_autoSendTimer->stop();
        m_sendBtn->setEnabled(true);
        m_multiSendBtn->setEnabled(true);
    }
}

void SerialAssistant::onAutoSendTimer()
{
    if (m_sendTab->currentIndex() == 0) onSendClicked();
    else onMultiSendSelected();
}

void SerialAssistant::onResetCounterClicked() { resetCounters(); }
void SerialAssistant::onPauseToggled(bool paused) { m_paused = paused; }

bool SerialAssistant::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_sendText && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent*>(event);
        if ((keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) &&
            (keyEvent->modifiers() & Qt::ControlModifier)) {
            onSendClicked();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void SerialAssistant::onMultiSendAddRow()
{
    int row = m_multiSendTable->rowCount();
    m_multiSendTable->insertRow(row);

    QTableWidgetItem *check = new QTableWidgetItem();
    check->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    check->setCheckState(Qt::Unchecked); // Default unchecked
    check->setTextAlignment(Qt::AlignCenter);
    m_multiSendTable->setItem(row, 0, check);

    QTableWidgetItem *numItem = new QTableWidgetItem(QString::number(row + 1));
    numItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    numItem->setTextAlignment(Qt::AlignCenter);
    m_multiSendTable->setItem(row, 1, numItem);

    m_multiSendTable->setItem(row, 2, new QTableWidgetItem("")); // Command
    m_multiSendTable->setItem(row, 3, new QTableWidgetItem("")); // Note
}

void SerialAssistant::onMultiSendDeleteRow()
{
    QList<QTableWidgetSelectionRange> ranges = m_multiSendTable->selectedRanges();
    if (ranges.isEmpty()) {
        if (m_multiSendTable->rowCount() > 1) m_multiSendTable->removeRow(m_multiSendTable->rowCount() - 1);
        return;
    }
    QList<int> rowsToDelete;
    for (int i = 0; i < ranges.size(); i++) {
        QTableWidgetSelectionRange range = ranges.at(i);
        for (int r = range.topRow(); r <= range.bottomRow(); r++) rowsToDelete << r;
    }
    std::sort(rowsToDelete.begin(), rowsToDelete.end(), std::greater<int>());
    for (int i = 0; i < rowsToDelete.size(); i++) m_multiSendTable->removeRow(rowsToDelete.at(i));
    for (int i = 0; i < m_multiSendTable->rowCount(); i++) m_multiSendTable->item(i, 1)->setText(QString::number(i + 1));
}

// 标准CSV行解析：正确处理双引号包裹、字段内逗号、转义双引号


void SerialAssistant::onMultiSendImportCsv()
{
    const QString fileName = QFileDialog::getOpenFileName(
        this, QStringLiteral("Import Commands CSV"), QString(),
        QStringLiteral("CSV Files (*.csv);;Text Files (*.txt);;All Files (*)"));
    if (!fileName.isEmpty())
        importCsvFile(fileName, true);
}

void SerialAssistant::onMultiSendImportCsv(QString fileName)
{
    importCsvFile(fileName, true);
}

static void setCsvCodec(QTextStream &stream)
{
    if (QTextCodec *codec = QTextCodec::codecForName("GBK"))
        stream.setCodec(codec);
    else
        stream.setCodec("UTF-8");
}

void SerialAssistant::importCsvFile(const QString &fileName, bool interactive)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (interactive)
            QMessageBox::warning(this, QStringLiteral("Error"), QStringLiteral("Failed to open file"));
        return;
    }

    QTextStream in(&file);
    setCsvCodec(in);
    m_multiSendTable->setRowCount(0);

    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty())
            continue;

        const QStringList parts = parseCsvLine(line);
        if (m_multiSendTable->rowCount() == 0
            && parts.size() >= 2
            && parts[0].trimmed().compare(QLatin1String("Enabled"), Qt::CaseInsensitive) == 0
            && parts[1].trimmed().compare(QLatin1String("Command"), Qt::CaseInsensitive) == 0) {
            continue;
        }

        onMultiSendAddRow();
        const int row = m_multiSendTable->rowCount() - 1;
        if (parts.size() >= 2) {
            const QString enabled = parts[0].trimmed().toLower();
            const bool checked = (enabled == QLatin1String("true") || enabled == QLatin1String("1"));
            m_multiSendTable->item(row, 0)->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
            m_multiSendTable->item(row, 2)->setText(parts[1].trimmed());
            if (parts.size() >= 3 && m_multiSendTable->item(row, 3))
                m_multiSendTable->item(row, 3)->setText(parts[2].trimmed());
        } else {
            m_multiSendTable->item(row, 2)->setText(line);
        }
    }

    file.close();
    showStatusMessage(QStringLiteral("Imported %1 commands").arg(m_multiSendTable->rowCount()));
}

void SerialAssistant::onMultiSendExportCsv()
{
    const QString fileName = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export Commands CSV"), QString(),
        QStringLiteral("CSV Files (*.csv);;All Files (*)"));
    if (!fileName.isEmpty())
        exportCsvFile(fileName, true);
}

void SerialAssistant::onMultiSendExportCsv(QString filename)
{
    exportCsvFile(filename, true);
}

void SerialAssistant::exportCsvFile(const QString &fileName, bool interactive)
{
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (interactive)
            QMessageBox::warning(this, QStringLiteral("Error"), QStringLiteral("Failed to save file"));
        return;
    }

    QTextStream out(&file);
    setCsvCodec(out);
    out << "Enabled,Command,Note\n";
    for (int i = 0; i < m_multiSendTable->rowCount(); ++i) {
        const bool checked = m_multiSendTable->item(i, 0)->checkState() == Qt::Checked;
        const QString cmd = m_multiSendTable->item(i, 2)->text().trimmed();
        const QString note = m_multiSendTable->item(i, 3)
                                 ? m_multiSendTable->item(i, 3)->text().trimmed()
                                 : QString();
        out << (checked ? "1," : "0,")
            << csvEscape(cmd) << ','
            << csvEscape(note) << '\n';
    }
    file.close();
    if (interactive)
        showStatusMessage(QStringLiteral("Exported %1 commands").arg(m_multiSendTable->rowCount()));
}

void SerialAssistant::onMultiSendSelected()
{
    if (!m_isConnected) return;
    int interval = m_multiSendIntervalSpin->value();
    QList<QByteArray> commandsToSend;

    for (int i = 0; i < m_multiSendTable->rowCount(); i++) {
        if (m_multiSendTable->item(i, 0)->checkState() == Qt::Checked) {
            QString cmd = m_multiSendTable->item(i, 2)->text();
            if (!cmd.trimmed().isEmpty()) commandsToSend << processSendData(cmd);
        }
    }
    if (commandsToSend.isEmpty()) {
        showStatusMessage("No checked commands to send");
        return;
    }

    int delay = 0;
    for (int i = 0; i < commandsToSend.size(); i++) {
        QByteArray data = commandsToSend.at(i);
        QTimer::singleShot(delay, this, [this, data]() {
            if (m_isConnected) {
                m_txBytes += data.size();
                m_txCountLabel->setText(QString("S: %1 B").arg(m_txBytes));
                emit sendDataRequested(data);
            }
        });
        delay += interval;
    }
    showStatusMessage(QString("Sending %1 commands...").arg(commandsToSend.size()));
}

QByteArray SerialAssistant::processSendData(const QString &text)
{
    QByteArray data;
    if (m_hexSendCheck->isChecked()) {
        data = ProtocolUtils::hexStringToBytes(text);
    } else {
        data = text.toLocal8Bit();
        switch (m_lineEndingCombo->currentData().toInt()) {
        case 1: data.append('\r'); break;
        case 2: data.append('\n'); break;
        case 3: data.append("\r\n"); break;
        default: break;
        }
    }

    if (m_addCrc16Check->isChecked() && !data.isEmpty())
        data = ProtocolUtils::appendCrc16Modbus(data);
    return data;
}

QString SerialAssistant::getTimestamp() const
{
    return ProtocolUtils::timestampMs();
}

// YModem发送按钮点击
void SerialAssistant::onYmodemSendClicked()
{
    if (!m_isConnected) {
        QMessageBox::warning(this, "Warning", "Please open serial port first!");
        return;
    }

    QString fileName = QFileDialog::getOpenFileName(this,
        "Select file for YModem send",
        "",
        "Firmware Files (*.bin *.hex *.elf);;All Files (*.*)");

    if (fileName.isEmpty()) return;

    showStatusMessage("YModem send selected: " + fileName);
    // 发出信号，你后续自己实现YModem发送逻辑，接收filePath即可
    emit ymodemSendRequested(fileName);
}

void SerialAssistant::onDoubleSendSelected(int row, int column)
{
    if(1==column){
        if (!m_isConnected) return;

        QByteArray commandsToSend;
        QString cmd = m_multiSendTable->item(row, column + 1)->text();
        if (!cmd.trimmed().isEmpty()) commandsToSend = processSendData(cmd);

        if (commandsToSend.isEmpty()) {
            showStatusMessage("No checked commands to send");
            return;
        }

        QTimer::singleShot(0, this, [this, commandsToSend]() {
            if (m_isConnected) {
                m_txBytes += commandsToSend.size();
                m_txCountLabel->setText(QString("S: %1 B").arg(m_txBytes));
                emit sendDataRequested(commandsToSend);
            }
        });

        showStatusMessage(QStringLiteral("Sending command..."));
    }
}

void SerialAssistant::loadSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("Serial"));
    const QString port = settings.value(QStringLiteral("port")).toString();
    if (!port.isEmpty()) {
        const int idx = m_portCombo->findData(port);
        if (idx >= 0)
            m_portCombo->setCurrentIndex(idx);
    }

    auto restoreCombo = [&settings](QComboBox *combo, const QString &key, const QVariant &fallback) {
        const QVariant stored = settings.value(key, fallback);
        const int idx = combo->findData(stored);
        if (idx >= 0)
            combo->setCurrentIndex(idx);
    };
    restoreCombo(m_baudCombo, QStringLiteral("baud"), 115200);
    restoreCombo(m_dataBitsCombo, QStringLiteral("dataBits"), static_cast<int>(QSerialPort::Data8));
    restoreCombo(m_parityCombo, QStringLiteral("parity"), static_cast<int>(QSerialPort::NoParity));
    restoreCombo(m_stopBitsCombo, QStringLiteral("stopBits"), static_cast<int>(QSerialPort::OneStop));
    restoreCombo(m_flowCombo, QStringLiteral("flow"), static_cast<int>(QSerialPort::NoFlowControl));
    restoreCombo(m_lineEndingCombo, QStringLiteral("lineEnding"), 0);

    m_hexSendCheck->setChecked(settings.value(QStringLiteral("hexSend"), false).toBool());
    m_hexReceiveCheck->setChecked(settings.value(QStringLiteral("hexRecv"), false).toBool());
    m_timestampCheck->setChecked(settings.value(QStringLiteral("timestamp"), false).toBool());
    m_autoWrapCheck->setChecked(settings.value(QStringLiteral("wrap"), true).toBool());
    m_autoScrollCheck->setChecked(settings.value(QStringLiteral("autoScroll"), true).toBool());
    m_autoReconnectCheck->setChecked(settings.value(QStringLiteral("autoReconnect"), true).toBool());
    m_addCrc16Check->setChecked(settings.value(QStringLiteral("crc16"), false).toBool());
    settings.endGroup();
}

void SerialAssistant::saveSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("Serial"));
    settings.setValue(QStringLiteral("port"), selectedPortName());
    settings.setValue(QStringLiteral("baud"), selectedBaudRate());
    settings.setValue(QStringLiteral("dataBits"), static_cast<int>(selectedDataBits()));
    settings.setValue(QStringLiteral("parity"), static_cast<int>(selectedParity()));
    settings.setValue(QStringLiteral("stopBits"), static_cast<int>(selectedStopBits()));
    settings.setValue(QStringLiteral("flow"), static_cast<int>(selectedFlowControl()));
    settings.setValue(QStringLiteral("lineEnding"), m_lineEndingCombo->currentData());
    settings.setValue(QStringLiteral("hexSend"), m_hexSendCheck->isChecked());
    settings.setValue(QStringLiteral("hexRecv"), m_hexReceiveCheck->isChecked());
    settings.setValue(QStringLiteral("timestamp"), m_timestampCheck->isChecked());
    settings.setValue(QStringLiteral("wrap"), m_autoWrapCheck->isChecked());
    settings.setValue(QStringLiteral("autoScroll"), m_autoScrollCheck->isChecked());
    settings.setValue(QStringLiteral("autoReconnect"), m_autoReconnectCheck->isChecked());
    settings.setValue(QStringLiteral("crc16"), m_addCrc16Check->isChecked());
    settings.endGroup();
}
