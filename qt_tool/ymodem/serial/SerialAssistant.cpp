#include "SerialAssistant.h"
#include "SettingsWidget.h"
#include "common/ProtocolUtils.h"

#include <QAbstractItemView>
#include <QDateTime>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScrollBar>
#include <QSettings>
#include <QTextCodec>
#include <QTextCursor>
#include <QTextStream>
#include <QTimer>
#include <algorithm>

namespace {

constexpr int kMinBaudRate = 50;        // 自定义波特率下限
constexpr int kMaxBaudRate = 12000000;  // 自定义波特率上限，再大的数多半不是串口速率

const QList<int> &builtinBaudRates()  // 固定波特率，按数值从小到大
{
    static const QList<int> rates = {
        1200, 2400, 4800, 9600,
        19200, 38400, 43000, 57600, 76800,
        115200, 128000, 230400, 256000,
        460800, 921600,
        1000000, 1500000, 2000000, 3000000
    };
    return rates;
}

bool isBuiltinBaudRate(int baud)  // 是否为下拉里的固定档
{
    return builtinBaudRates().contains(baud);
}

} // namespace

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
    setWindowTitle(tr("Serial Debug Assistant"));
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
    m_sendText->setPlaceholderText(tr("Enter command to send... (Ctrl+Enter to send)"));
    m_sendText->installEventFilter(this);
    singleLayout->addWidget(m_sendText, 1);

    auto *singleBtnCol = new QVBoxLayout();
    singleBtnCol->setSpacing(4);
    m_sendBtn = new QPushButton(tr("Send"), m_singleSendPage);
    m_sendBtn->setMinimumWidth(70);
    m_sendBtn->setEnabled(false);
    m_sendBtn->setStyleSheet("QPushButton { background-color: #2196F3; color: white; font-weight: bold; padding: 4px; border-radius: 2px; } QPushButton:hover { background-color: #1E88E5; } QPushButton:disabled { background-color: #90CAF9; }");
    singleBtnCol->addWidget(m_sendBtn);

    m_clearSendBtn = new QPushButton(tr("Clear"), m_singleSendPage);
    m_clearSendBtn->setMinimumWidth(70);
    connect(m_clearSendBtn, &QPushButton::clicked, m_sendText, &QPlainTextEdit::clear);
    singleBtnCol->addWidget(m_clearSendBtn);
    singleBtnCol->addStretch();
    singleLayout->addLayout(singleBtnCol);
    m_sendTab->addTab(m_singleSendPage, tr("Single Send"));

    // Tab 2: Multi Send
    m_multiSendPage = new QWidget(m_sendTab);
    auto *multiLayout = new QVBoxLayout(m_multiSendPage);
    multiLayout->setSpacing(4);
    multiLayout->setContentsMargins(4, 4, 4, 4);

    auto *multiToolbar = new QHBoxLayout();
    multiToolbar->setSpacing(4);
    m_multiAddBtn = new QPushButton(tr("Add"), m_multiSendPage);
    m_multiAddBtn->setMaximumWidth(55);
    multiToolbar->addWidget(m_multiAddBtn);
    m_multiDelBtn = new QPushButton(tr("Del"), m_multiSendPage);
    m_multiDelBtn->setMaximumWidth(55);
    multiToolbar->addWidget(m_multiDelBtn);
    m_multiImportBtn = new QPushButton(tr("Import"), m_multiSendPage);
    m_multiImportBtn->setMaximumWidth(60);
    multiToolbar->addWidget(m_multiImportBtn);
    m_multiExportBtn = new QPushButton(tr("Export"), m_multiSendPage);
    m_multiExportBtn->setMaximumWidth(60);
    multiToolbar->addWidget(m_multiExportBtn);
    multiToolbar->addStretch();
    m_intLabel = new QLabel(tr("Int:"), m_multiSendPage);
    multiToolbar->addWidget(m_intLabel);
    m_multiSendIntervalSpin = new QSpinBox(m_multiSendPage);
    m_multiSendIntervalSpin->setRange(0, 5000);
    m_multiSendIntervalSpin->setValue(1000);
    m_multiSendIntervalSpin->setSuffix(tr("ms"));
    m_multiSendIntervalSpin->setMaximumWidth(70);
    multiToolbar->addWidget(m_multiSendIntervalSpin);
    m_multiSendBtn = new QPushButton(tr("Send"), m_multiSendPage);
    m_multiSendBtn->setEnabled(false);
    m_multiSendBtn->setMinimumWidth(60);
    m_multiSendBtn->setStyleSheet("QPushButton { background-color: #2196F3; color: white; font-weight: bold; padding: 4px; border-radius: 2px; } QPushButton:hover { background-color: #1E88E5; } QPushButton:disabled { background-color: #90CAF9; }");
    multiToolbar->addWidget(m_multiSendBtn);
    multiLayout->addLayout(multiToolbar);

    // Multi send table with Note column
    m_multiSendTable = new QTableWidget(m_multiSendPage);
    m_multiSendTable->setColumnCount(4);
    m_multiSendTable->setRowCount(0);
    m_multiSendTable->setHorizontalHeaderLabels({QString(), tr("No."), tr("Command"), tr("Note")});
    m_multiSendTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_multiSendTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_multiSendTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_multiSendTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_multiSendTable->verticalHeader()->setVisible(false);
    m_multiSendTable->setFont(QFont("Consolas", 9));
    m_multiSendTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_multiSendTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    multiLayout->addWidget(m_multiSendTable, 1);

    m_sendTab->addTab(m_multiSendPage, tr("Multi Send"));
    leftLayout->addWidget(m_sendTab, 1);
    m_mainLayout->addWidget(m_leftPanel, 5);

    // ========== Right Panel (grouped layout) ==========
    m_rightPanel = new QWidget(this);
    m_rightPanel->setMaximumWidth(220);
    auto *rightLayout = new QVBoxLayout(m_rightPanel);
    rightLayout->setSpacing(6);
    rightLayout->setContentsMargins(4, 0, 0, 0);

    // --- Group 1: Port parameters ---
    m_portGroup = new QGroupBox(tr("Port Config"), m_rightPanel);
    auto *portGrid = new QGridLayout(m_portGroup);
    portGrid->setSpacing(4);
    portGrid->setContentsMargins(6, 14, 6, 6);

    int r = 0;
    m_portLabel = new QLabel(tr("Port:"), m_portGroup);
    portGrid->addWidget(m_portLabel, r, 0, Qt::AlignLeft);
    auto *portLayout = new QHBoxLayout();
    portLayout->setSpacing(2);
    m_portCombo = new QComboBox(m_portGroup);
    m_portCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_portCombo->view()->setMinimumWidth(280);
    portLayout->addWidget(m_portCombo, 1);
    m_refreshBtn = new QPushButton(QStringLiteral("⟳"), m_portGroup);
    m_refreshBtn->setToolTip(tr("Refresh port list"));
    m_refreshBtn->setMaximumWidth(26);
    portLayout->addWidget(m_refreshBtn);
    portGrid->addLayout(portLayout, r, 1);
    r++;

    m_baudLabel = new QLabel(tr("Baud:"), m_portGroup);
    portGrid->addWidget(m_baudLabel, r, 0, Qt::AlignLeft);
    m_baudCombo = new QComboBox(m_portGroup);
    portGrid->addWidget(m_baudCombo, r, 1);
    r++;

    m_stopLabel = new QLabel(tr("Stop:"), m_portGroup);
    portGrid->addWidget(m_stopLabel, r, 0, Qt::AlignLeft);
    m_stopBitsCombo = new QComboBox(m_portGroup);
    portGrid->addWidget(m_stopBitsCombo, r, 1);
    r++;

    m_dataLabel = new QLabel(tr("Data:"), m_portGroup);
    portGrid->addWidget(m_dataLabel, r, 0, Qt::AlignLeft);
    m_dataBitsCombo = new QComboBox(m_portGroup);
    portGrid->addWidget(m_dataBitsCombo, r, 1);
    r++;

    m_parityLabel = new QLabel(tr("Parity:"), m_portGroup);
    portGrid->addWidget(m_parityLabel, r, 0, Qt::AlignLeft);
    m_parityCombo = new QComboBox(m_portGroup);
    portGrid->addWidget(m_parityCombo, r, 1);
    r++;

    m_flowLabel = new QLabel(tr("Flow:"), m_portGroup);
    portGrid->addWidget(m_flowLabel, r, 0, Qt::AlignLeft);
    m_flowCombo = new QComboBox(m_portGroup);
    portGrid->addWidget(m_flowCombo, r, 1);
    r++;

    auto *pinRow = new QHBoxLayout();
    pinRow->setSpacing(10);
    m_dtrCheck = new QCheckBox(tr("DTR"), m_portGroup);
    m_dtrCheck->setEnabled(false);
    pinRow->addWidget(m_dtrCheck);
    m_rtsCheck = new QCheckBox(tr("RTS"), m_portGroup);
    m_rtsCheck->setEnabled(false);
    pinRow->addWidget(m_rtsCheck);
    pinRow->addStretch();
    portGrid->addLayout(pinRow, r, 0, 1, 2);
    r++;

    m_autoReconnectCheck = new QCheckBox(tr("Auto Reconnect"), m_portGroup);
    m_autoReconnectCheck->setChecked(true);
    portGrid->addWidget(m_autoReconnectCheck, r, 0, 1, 2);
    rightLayout->addWidget(m_portGroup);

    // Open/Close button
    m_openCloseBtn = new QPushButton(tr("Open Port"), m_rightPanel);
    m_openCloseBtn->setMinimumHeight(28);
    m_openCloseBtn->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; font-weight: bold; padding: 3px; border-radius: 2px; } QPushButton:hover { background-color: #43A047; }");
    rightLayout->addWidget(m_openCloseBtn);

    // YMODEM Send button
    m_ymodemSendBtn = new QPushButton(tr("YMODEM Send"), m_rightPanel);
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
    m_sendGroup = new QGroupBox(tr("Send Settings"), m_rightPanel);
    auto *sendGrid = new QGridLayout(m_sendGroup);
    sendGrid->setSpacing(4);
    sendGrid->setContentsMargins(6, 14, 6, 6);
    r = 0;

    m_autoSendCheck = new QCheckBox(tr("Timed Send"), m_sendGroup);
    sendGrid->addWidget(m_autoSendCheck, r, 0, 1, 2);
    r++;

    auto *intervalRow = new QHBoxLayout();
    intervalRow->setSpacing(4);
    m_intervalLabel = new QLabel(tr("Interval:"), m_sendGroup);
    intervalRow->addWidget(m_intervalLabel);
    m_autoSendIntervalSpin = new QSpinBox(m_sendGroup);
    m_autoSendIntervalSpin->setRange(10, 60000);
    m_autoSendIntervalSpin->setValue(1000);
    m_autoSendIntervalSpin->setSuffix(tr("ms"));
    intervalRow->addWidget(m_autoSendIntervalSpin, 1);
    sendGrid->addLayout(intervalRow, r, 0, 1, 2);
    r++;

    m_hexSendCheck = new QCheckBox(tr("Hex Send"), m_sendGroup);
    sendGrid->addWidget(m_hexSendCheck, r, 0, 1, 2);
    r++;

    // 新增Modbus CRC16选项
    m_addCrc16Check = new QCheckBox(tr("Add CRC16 (Modbus)"), m_sendGroup);
    sendGrid->addWidget(m_addCrc16Check, r, 0, 1, 2);
    r++;

    auto *endRow = new QHBoxLayout();
    endRow->setSpacing(4);
    m_lineEndLabel = new QLabel(tr("Line End:"), m_sendGroup);
    endRow->addWidget(m_lineEndLabel);
    m_lineEndingCombo = new QComboBox(m_sendGroup);
    endRow->addWidget(m_lineEndingCombo, 1);
    sendGrid->addLayout(endRow, r, 0, 1, 2);
    rightLayout->addWidget(m_sendGroup);

    // Separator
    auto *line2 = new QFrame(m_rightPanel);
    line2->setFrameShape(QFrame::HLine);
    line2->setFrameShadow(QFrame::Sunken);
    rightLayout->addWidget(line2);

    // --- Group 3: Receive settings ---
    m_recvGroup = new QGroupBox(tr("Receive Settings"), m_rightPanel);
    auto *recvGrid = new QGridLayout(m_recvGroup);
    recvGrid->setSpacing(4);
    recvGrid->setContentsMargins(6, 14, 6, 6);
    r = 0;

    auto *recvBtnRow = new QHBoxLayout();
    recvBtnRow->setSpacing(4);
    m_saveLogBtn = new QPushButton(tr("Save Log"), m_recvGroup);
    recvBtnRow->addWidget(m_saveLogBtn);
    m_clearReceiveBtn = new QPushButton(tr("Clear Recv"), m_recvGroup);
    recvBtnRow->addWidget(m_clearReceiveBtn);
    recvGrid->addLayout(recvBtnRow, r, 0, 1, 2);
    r++;

    m_hexReceiveCheck = new QCheckBox(tr("Hex Disp"), m_recvGroup);
    recvGrid->addWidget(m_hexReceiveCheck, r, 0);
    m_timestampCheck = new QCheckBox(tr("Timestamp"), m_recvGroup);
    recvGrid->addWidget(m_timestampCheck, r, 1);
    r++;

    m_autoScrollCheck = new QCheckBox(tr("Auto Scroll"), m_recvGroup);
    m_autoScrollCheck->setChecked(true);
    recvGrid->addWidget(m_autoScrollCheck, r, 0);
    m_autoWrapCheck = new QCheckBox(tr("Wrap"), m_recvGroup);
    recvGrid->addWidget(m_autoWrapCheck, r, 1);
    r++;

    m_pauseCheck = new QCheckBox(tr("Pause Display"), m_recvGroup);
    recvGrid->addWidget(m_pauseCheck, r, 0);
    m_autoSaveCheck = new QCheckBox(tr("Auto Save"), m_recvGroup);
    m_autoSaveCheck->setToolTip(tr("Append new receive text to serial_auto.txt in the log directory"));
    recvGrid->addWidget(m_autoSaveCheck, r, 1);
    rightLayout->addWidget(m_recvGroup);

    // Reset Counter button
    m_resetCounterBtn = new QPushButton(tr("Reset Counter"), m_rightPanel);
    rightLayout->addWidget(m_resetCounterBtn);

    // Separator
    auto *line3 = new QFrame(m_rightPanel);
    line3->setFrameShape(QFrame::HLine);
    line3->setFrameShadow(QFrame::Sunken);
    rightLayout->addWidget(line3);

    // Status area
    auto *countRow = new QHBoxLayout();
    countRow->setSpacing(10);
    m_rxCountLabel = new QLabel(tr("R: %1 B").arg(0), m_rightPanel);
    countRow->addWidget(m_rxCountLabel);
    m_txCountLabel = new QLabel(tr("S: %1 B").arg(0), m_rightPanel);
    countRow->addWidget(m_txCountLabel);
    countRow->addStretch();
    rightLayout->addLayout(countRow);

    m_statusLabel = new QLabel(tr("● Disconnected"), m_rightPanel);
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
        showStatusMessage(tr("Port list refreshed"));
        emit refreshPortsRequested();
    });
    connect(m_autoSendCheck, &QCheckBox::toggled, this, &SerialAssistant::onAutoSendToggled);
    connect(m_resetCounterBtn, &QPushButton::clicked, this, &SerialAssistant::onResetCounterClicked);
    connect(m_pauseCheck, &QCheckBox::toggled, this, &SerialAssistant::onPauseToggled);
    connect(m_autoSaveCheck, &QCheckBox::toggled, this, &SerialAssistant::setAutoSaveEnabled);
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

void SerialAssistant::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QWidget::changeEvent(event);
}

void SerialAssistant::retranslateUi()
{
    setWindowTitle(tr("Serial Debug Assistant"));
    m_sendText->setPlaceholderText(tr("Enter command to send... (Ctrl+Enter to send)"));
    m_sendBtn->setText(tr("Send"));
    m_clearSendBtn->setText(tr("Clear"));
    m_sendTab->setTabText(0, tr("Single Send"));
    m_multiAddBtn->setText(tr("Add"));
    m_multiDelBtn->setText(tr("Del"));
    m_multiImportBtn->setText(tr("Import"));
    m_multiExportBtn->setText(tr("Export"));
    m_intLabel->setText(tr("Int:"));
    m_multiSendIntervalSpin->setSuffix(tr("ms"));
    m_multiSendBtn->setText(tr("Send"));
    m_multiSendTable->setHorizontalHeaderLabels({QString(), tr("No."), tr("Command"), tr("Note")});
    m_sendTab->setTabText(1, tr("Multi Send"));

    m_portGroup->setTitle(tr("Port Config"));
    m_portLabel->setText(tr("Port:"));
    m_refreshBtn->setToolTip(tr("Refresh port list"));
    m_baudLabel->setText(tr("Baud:"));
    updateBaudToolTip();
    m_stopLabel->setText(tr("Stop:"));
    m_dataLabel->setText(tr("Data:"));
    m_parityLabel->setText(tr("Parity:"));
    m_flowLabel->setText(tr("Flow:"));
    m_dtrCheck->setText(tr("DTR"));
    m_rtsCheck->setText(tr("RTS"));
    m_autoReconnectCheck->setText(tr("Auto Reconnect"));
    m_openCloseBtn->setText(m_isConnected ? tr("Close Port") : tr("Open Port"));
    m_ymodemSendBtn->setText(tr("YMODEM Send"));

    m_sendGroup->setTitle(tr("Send Settings"));
    m_autoSendCheck->setText(tr("Timed Send"));
    m_intervalLabel->setText(tr("Interval:"));
    m_autoSendIntervalSpin->setSuffix(tr("ms"));
    m_hexSendCheck->setText(tr("Hex Send"));
    m_addCrc16Check->setText(tr("Add CRC16 (Modbus)"));
    m_lineEndLabel->setText(tr("Line End:"));

    m_recvGroup->setTitle(tr("Receive Settings"));
    m_saveLogBtn->setText(tr("Save Log"));
    m_clearReceiveBtn->setText(tr("Clear Recv"));
    m_hexReceiveCheck->setText(tr("Hex Disp"));
    m_timestampCheck->setText(tr("Timestamp"));
    m_autoScrollCheck->setText(tr("Auto Scroll"));
    m_autoWrapCheck->setText(tr("Wrap"));
    m_pauseCheck->setText(tr("Pause Display"));
    m_autoSaveCheck->setText(tr("Auto Save"));
    m_autoSaveCheck->setToolTip(tr("Append new receive text to serial_auto.txt in the log directory"));
    m_resetCounterBtn->setText(tr("Reset Counter"));
    m_rxCountLabel->setText(tr("R: %1 B").arg(m_rxBytes));
    m_txCountLabel->setText(tr("S: %1 B").arg(m_txBytes));
    m_statusLabel->setText(m_isConnected ? tr("● Connected") : tr("● Disconnected"));

    const QString parity[] = {tr("None"), tr("Odd"), tr("Even"), tr("Mark"), tr("Space")};
    for (int i = 0; i < m_parityCombo->count() && i < 5; ++i)
        m_parityCombo->setItemText(i, parity[i]);
    const QString flow[] = {tr("None"), tr("RTS/CTS"), tr("XON/XOFF")};
    for (int i = 0; i < m_flowCombo->count() && i < 3; ++i)
        m_flowCombo->setItemText(i, flow[i]);
    const QString endings[] = {tr("None"), tr("CR"), tr("LF"), tr("CRLF")};
    for (int i = 0; i < m_lineEndingCombo->count() && i < 4; ++i)
        m_lineEndingCombo->setItemText(i, endings[i]);
}

void SerialAssistant::populateBaudRates()
{
    m_baudCombo->setEditable(true);
    // 自己按数值插入。默认策略会把输入文本再追加一条，和已有档重复。
    m_baudCombo->setInsertPolicy(QComboBox::NoInsert);
    for (int baud : builtinBaudRates())
        m_baudCombo->addItem(QString::number(baud), baud);

    if (QLineEdit *edit = m_baudCombo->lineEdit()) {
        edit->setValidator(new QRegularExpressionValidator(
            QRegularExpression(QStringLiteral("\\d{0,8}")), edit));
        connect(edit, &QLineEdit::editingFinished, this, &SerialAssistant::commitBaudEdit);
    }
    connect(m_baudCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SerialAssistant::onBaudIndexChanged);
    selectBaudRate(115200);
    updateBaudToolTip();
}

void SerialAssistant::insertBaudRate(qint32 baud)
{
    if (m_baudCombo->findData(baud) >= 0)
        return;
    int row = 0;
    // 按数值插入，自定义档不堆在列表末尾。
    while (row < m_baudCombo->count() && m_baudCombo->itemData(row).toInt() < baud)
        ++row;
    m_baudCombo->insertItem(row, QString::number(baud), baud);
}

void SerialAssistant::selectBaudRate(qint32 baud)
{
    insertBaudRate(baud);
    const int idx = m_baudCombo->findData(baud);
    if (idx >= 0)
        m_baudCombo->setCurrentIndex(idx);
    m_lastValidBaud = baud;
}

void SerialAssistant::updateBaudToolTip()
{
    m_baudCombo->setToolTip(tr("Type an integer from %1 to %2 to add a custom baud rate")
                                .arg(kMinBaudRate)
                                .arg(kMaxBaudRate));
}

bool SerialAssistant::baudFromText(const QString &text, qint32 *baud) const
{
    bool ok = false;
    const int value = text.trimmed().toInt(&ok);
    // 只拦明显无效的数。芯片和 USB 转串口各自还有上限，打不开时由串口后端报错。
    if (!ok || value < kMinBaudRate || value > kMaxBaudRate)
        return false;
    if (baud)
        *baud = value;
    return true;
}

void SerialAssistant::onBaudIndexChanged(int index)
{
    if (m_baudEditLock || index < 0)
        return;
    qint32 baud = 0;
    // 点列表时输入框还没失焦，先记住这档，后面输入非法才能退回来。
    if (!baudFromText(m_baudCombo->itemText(index), &baud))
        return;
    m_lastValidBaud = baud;
}

void SerialAssistant::commitBaudEdit()
{
    // setCurrentIndex 会让输入框再发一次 editingFinished，重入时直接返回。
    // 下拉列表弹出来时输入框也会失焦，这时还没输完，不能当成一次提交。
    if (m_baudEditLock || m_baudCombo->view()->isVisible())
        return;

    qint32 baud = 0;
    if (!baudFromText(m_baudCombo->currentText(), &baud)) {
        showStatusMessage(tr("Baud rate must be an integer from %1 to %2")
                              .arg(kMinBaudRate)
                              .arg(kMaxBaudRate));
        m_baudEditLock = true;
        const int idx = m_baudCombo->findData(m_lastValidBaud);
        if (idx >= 0)
            m_baudCombo->setCurrentIndex(idx);
        else
            m_baudCombo->setEditText(QString::number(m_lastValidBaud));
        m_baudEditLock = false;
        return;
    }

    const bool newlyAdded = !isBuiltinBaudRate(baud) && !m_customBaudRates.contains(baud);
    m_baudEditLock = true;
    selectBaudRate(baud);
    m_baudEditLock = false;
    if (!newlyAdded)
        return;

    m_customBaudRates.append(baud);
    // 自定义档马上落盘。只等析构保存的话，异常退出会把新档丢掉。
    saveSettings();
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
    m_parityCombo->addItem(tr("None"), QSerialPort::NoParity);
    m_parityCombo->addItem(tr("Odd"), QSerialPort::OddParity);
    m_parityCombo->addItem(tr("Even"), QSerialPort::EvenParity);
    m_parityCombo->addItem(tr("Mark"), QSerialPort::MarkParity);
    m_parityCombo->addItem(tr("Space"), QSerialPort::SpaceParity);
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
    m_flowCombo->addItem(tr("None"), QSerialPort::NoFlowControl);
    m_flowCombo->addItem(tr("RTS/CTS"), QSerialPort::HardwareControl);
    m_flowCombo->addItem(tr("XON/XOFF"), QSerialPort::SoftwareControl);
}

void SerialAssistant::populateLineEndings()
{
    m_lineEndingCombo->addItem(tr("None"), 0);
    m_lineEndingCombo->addItem(tr("CR"), 1);
    m_lineEndingCombo->addItem(tr("LF"), 2);
    m_lineEndingCombo->addItem(tr("CRLF"), 3);
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
qint32 SerialAssistant::selectedBaudRate() const
{
    qint32 baud = 0;
    if (baudFromText(m_baudCombo->currentText(), &baud))
        return baud;
    return m_lastValidBaud;
}
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
        m_openCloseBtn->setText(tr("Close Port"));
        m_openCloseBtn->setStyleSheet("QPushButton { background-color: #f44336; color: white; font-weight: bold; padding: 3px; border-radius: 2px; } QPushButton:hover { background-color: #E53935; }");
        m_statusLabel->setText(tr("● Connected"));
        m_statusLabel->setStyleSheet("QLabel { color: #4CAF50; font-weight: bold; }");
        m_portInfoLabel->setText(QString("%1 @ %2").arg(m_portCombo->currentData().toString(), m_baudCombo->currentText()));
    } else {
        m_openCloseBtn->setText(tr("Open Port"));
        m_openCloseBtn->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; font-weight: bold; padding: 3px; border-radius: 2px; } QPushButton:hover { background-color: #43A047; }");
        m_statusLabel->setText(tr("● Disconnected"));
        m_statusLabel->setStyleSheet("QLabel { color: #f44336; font-weight: bold; }");
        m_portInfoLabel->setText("");
        m_dtrCheck->setChecked(false);
        m_rtsCheck->setChecked(false);
        if (m_autoSendTimer->isActive()) m_autoSendCheck->setChecked(false);
    }
}

void SerialAssistant::onIoData(const IoPacket &packet)
{
    appendReceivedData(packet.data);
    emit ioDataReceived(packet);
}

void SerialAssistant::appendReceivedData(const QByteArray &data)
{
    if (data.isEmpty())
        return;

    m_rxBytes += static_cast<quint64>(data.size());
    m_rxCountLabel->setText(tr("R: %1 B").arg(m_rxBytes));

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
    // 暂停只停画面。自动保存仍写这段，避免勾了保存却在暂停时丢数据。
    appendAutoSave(text);
    if (m_paused)
        return;
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
    m_rxCountLabel->setText(tr("R: %1 B").arg(0));
    m_txCountLabel->setText(tr("S: %1 B").arg(0));
}

QByteArray SerialAssistant::getReceiveText()
{
    return m_receiveText->toPlainText().toUtf8();
}

void SerialAssistant::onOpenCloseClicked()
{
    if (!m_isConnected) {
        // 点打开时输入框可能还没失焦，先把当前文本收成一档再读波特率。
        commitBaudEdit();
        if (m_portCombo->currentData().toString().isEmpty()) {
            QMessageBox::warning(this, tr("Warning"), tr("Please select a serial port first!"));
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
        m_txCountLabel->setText(tr("S: %1 B").arg(m_txBytes));
        emit sendDataRequested(data);
    }
}

void SerialAssistant::onAutoSendToggled(bool enabled)
{
    if (enabled) {
        if (!m_isConnected) {
            m_autoSendCheck->setChecked(false);
            QMessageBox::warning(this, tr("Warning"), tr("Please open serial port first!"));
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
        this, tr("Import Commands CSV"), QString(),
        tr("CSV Files (*.csv);;Text Files (*.txt);;All Files (*)"));
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
            QMessageBox::warning(this, tr("Error"), tr("Failed to open file"));
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
    showStatusMessage(tr("Imported %1 commands").arg(m_multiSendTable->rowCount()));
}

void SerialAssistant::onMultiSendExportCsv()
{
    const QString fileName = QFileDialog::getSaveFileName(
        this, tr("Export Commands CSV"), QString(),
        tr("CSV Files (*.csv);;All Files (*)"));
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
            QMessageBox::warning(this, tr("Error"), tr("Failed to save file"));
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
        showStatusMessage(tr("Exported %1 commands").arg(m_multiSendTable->rowCount()));
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
        showStatusMessage(tr("No checked commands to send"));
        return;
    }

    int delay = 0;
    for (int i = 0; i < commandsToSend.size(); i++) {
        QByteArray data = commandsToSend.at(i);
        QTimer::singleShot(delay, this, [this, data]() {
            if (m_isConnected) {
                m_txBytes += data.size();
                m_txCountLabel->setText(tr("S: %1 B").arg(m_txBytes));
                emit sendDataRequested(data);
            }
        });
        delay += interval;
    }
    showStatusMessage(tr("Sending %1 commands...").arg(commandsToSend.size()));
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
        QMessageBox::warning(this, tr("Warning"), tr("Please open serial port first!"));
        return;
    }

    QString fileName = QFileDialog::getOpenFileName(this,
        tr("Select file for YModem send"),
        QString(),
        tr("Firmware Files (*.bin *.hex *.elf);;All Files (*.*)"));

    if (fileName.isEmpty()) return;

    showStatusMessage(tr("YModem send selected: %1").arg(fileName));
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
            showStatusMessage(tr("No checked commands to send"));
            return;
        }

        QTimer::singleShot(0, this, [this, commandsToSend]() {
            if (m_isConnected) {
                m_txBytes += commandsToSend.size();
                m_txCountLabel->setText(tr("S: %1 B").arg(m_txBytes));
                emit sendDataRequested(commandsToSend);
            }
        });

        showStatusMessage(tr("Sending command..."));
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

    const QVariantList storedCustom = settings.value(QStringLiteral("customBauds")).toList();
    for (const QVariant &value : storedCustom) {
        bool ok = false;
        const int raw = value.toInt(&ok);
        qint32 baud = 0;
        if (!ok || !baudFromText(QString::number(raw), &baud))
            continue;
        if (isBuiltinBaudRate(baud) || m_customBaudRates.contains(baud))
            continue;
        m_customBaudRates.append(baud);
        insertBaudRate(baud);
    }

    qint32 storedBaud = 0;
    // 旧配置只存了当前值。自定义档不在列表里时补上，下次下拉还能看到。
    if (baudFromText(settings.value(QStringLiteral("baud"), 115200).toString(), &storedBaud)) {
        if (!isBuiltinBaudRate(storedBaud) && !m_customBaudRates.contains(storedBaud)) {
            m_customBaudRates.append(storedBaud);
            insertBaudRate(storedBaud);
        }
        selectBaudRate(storedBaud);
    }

    auto restoreCombo = [&settings](QComboBox *combo, const QString &key, const QVariant &fallback) {
        const QVariant stored = settings.value(key, fallback);
        const int idx = combo->findData(stored);
        if (idx >= 0)
            combo->setCurrentIndex(idx);
    };
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
    m_autoSaveCheck->setChecked(settings.value(QStringLiteral("autoSave"), false).toBool());
    settings.endGroup();
}

void SerialAssistant::saveSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("Serial"));
    settings.setValue(QStringLiteral("port"), selectedPortName());
    settings.setValue(QStringLiteral("baud"), selectedBaudRate());
    QVariantList customBauds;
    for (qint32 baud : m_customBaudRates)
        customBauds.append(baud);
    settings.setValue(QStringLiteral("customBauds"), customBauds);
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
    settings.setValue(QStringLiteral("autoSave"), m_autoSaveCheck->isChecked());
    settings.endGroup();
}

void SerialAssistant::setAutoSaveEnabled(bool enabled)
{
    if (!enabled) {
        delete m_autoSaveFile;
        m_autoSaveFile = nullptr;
        return;
    }
    openAutoSave();
}

bool SerialAssistant::openAutoSave()
{
    const QString path = QFileInfo(SettingsWidget::logFilePath(QStringLiteral("serial_auto.txt"))).absoluteFilePath();
    if (m_autoSaveFile && m_autoSaveFile->isOpen() && m_autoSaveFile->fileName() == path)
        return true;
    delete m_autoSaveFile;
    m_autoSaveFile = new QFile(path, this);
    if (m_autoSaveFile->open(QIODevice::WriteOnly | QIODevice::Append))
        return true;
    delete m_autoSaveFile;
    m_autoSaveFile = nullptr;
    // 失败时取消勾选，避免每个字节都再试一次并刷状态栏。
    m_autoSaveCheck->blockSignals(true);
    m_autoSaveCheck->setChecked(false);
    m_autoSaveCheck->blockSignals(false);
    showStatusMessage(tr("Cannot open auto-save file: %1").arg(path));
    return false;
}

void SerialAssistant::appendAutoSave(const QString &text)
{
    if (!m_autoSaveCheck->isChecked() || text.isEmpty())
        return;
    if (!openAutoSave())
        return;
    m_autoSaveFile->write(text.toUtf8());
    m_autoSaveFile->flush();  // 立刻落盘，方便一边收一边用别的程序打开看
}
