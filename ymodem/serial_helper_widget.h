#pragma once

#include <QWidget>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QTimer>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QGroupBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QProgressBar>
#include <QCloseEvent>
#include <QAtomicInt>

class SerialHelperWidget : public QWidget
{
    Q_OBJECT
public:
    explicit SerialHelperWidget(QWidget *parent = nullptr);
    ~SerialHelperWidget() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onRefreshPorts();
    void onOpenCloseClicked();
    void onDtrToggled(bool checked);
    void onRtsToggled(bool checked);

    void onClearRecvClicked();
    void onSaveLogClicked();

    void onSendClicked();
    void onClearSendClicked();
    void onAutoSendToggled(bool checked);
    void onCalcCrc16();
    void onOpenFileClicked();
    void onSendFileClicked();

    void onAddMultiItem();
    void onDeleteMultiItem();
    void onClearMultiItems();
    void onImportMultiList();
    void onExportMultiList();
    void onMultiLoopToggled(bool checked);
    void onMultiSendTimer();
    void onSendMultiRowClicked();

    void onSerialReadyRead();
    void onSerialError(QSerialPort::SerialPortError err);
    void onUpdateTime();

private:
    // UI members
    QComboBox* m_cbPort = nullptr;
    QPushButton* m_btnRefreshPort = nullptr;
    QComboBox* m_cbBaudrate = nullptr;
    QComboBox* m_cbStopBits = nullptr;
    QComboBox* m_cbDataBits = nullptr;
    QComboBox* m_cbParity = nullptr;
    QPushButton* m_btnOpenClose = nullptr;

    QPushButton* m_btnSaveWindow = nullptr;
    QPushButton* m_btnClearRecv = nullptr;

    QCheckBox* m_chkHexDisplay = nullptr;
    QCheckBox* m_chkDtr = nullptr;
    QCheckBox* m_chkRts = nullptr;
    QCheckBox* m_chkAutoSave = nullptr;
    QCheckBox* m_chkTimestamp = nullptr;
    QLineEdit* m_edtTimestampPeriod = nullptr;

    QPlainTextEdit* m_edtRecv = nullptr;

    QTabWidget* m_sendTab = nullptr;
    QPlainTextEdit* m_edtSend = nullptr;
    QPushButton* m_btnSend = nullptr;
    QPushButton* m_btnClearSend = nullptr;

    QCheckBox* m_chkAutoSend = nullptr;
    QLineEdit* m_edtAutoSendPeriod = nullptr;
    QPushButton* m_btnCalcCrc = nullptr;
    QLineEdit* m_edtCrcResult = nullptr;
    QPushButton* m_btnOpenFile = nullptr;
    QPushButton* m_btnSendFile = nullptr;
    QPushButton* m_btnStopSend = nullptr;

    QCheckBox* m_chkHexSend = nullptr;
    QCheckBox* m_chkSendNewline = nullptr;
    QProgressBar* m_sendProgress = nullptr;

    QTableWidget* m_multiSendTable = nullptr;
    QPushButton* m_btnAddItem = nullptr;
    QPushButton* m_btnDeleteItem = nullptr;
    QPushButton* m_btnClearItems = nullptr;
    QPushButton* m_btnImportList = nullptr;
    QPushButton* m_btnExportList = nullptr;
    QCheckBox* m_chkMultiLoop = nullptr;
    QLineEdit* m_edtMultiPeriod = nullptr;
    QTimer* m_multiSendTimer = nullptr;
    int m_multiLoopIndex = 0;

    QLabel* m_lblSendCount = nullptr;
    QLabel* m_lblRecvCount = nullptr;
    QLabel* m_lblCurrentTime = nullptr;

    // Core objects
    QSerialPort* m_serial = nullptr;
    QTimer* m_autoSendTimer = nullptr;
    QTimer* m_timeTimer = nullptr;
    quint32 m_recvBytes = 0;
    quint32 m_sendBytes = 0;
    QString m_sendFilePath;

    // Destroy guard
    bool m_isDestroying = false;

    // Helper functions
    void initUI();
    void loadPortList();
    void updateUIState(bool opened);
    void appendRecvData(const QByteArray& data);
    void sendData(const QByteArray& data);
    void onSendMultiRow(int row);
    void clearMultiTableSafe();
    QByteArray hexStringToBytes(const QString& hex);
    QString bytesToHexString(const QByteArray& bytes);
    static uint16_t crc16Modbus(const uint8_t* data, int len);
};
