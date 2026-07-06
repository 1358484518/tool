#ifndef SERIALASSISTANT_H
#define SERIALASSISTANT_H

#include <QWidget>
#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>
#include <QTextEdit>
#include <QLineEdit>
#include <QLabel>
#include <QTimer>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSerialPort>
#include <QSpinBox>
#include <QFrame>
#include <QSerialPortInfo>
#include <QTabWidget>
#include <QTableWidget>

class SerialAssistant : public QWidget
{
    Q_OBJECT
public:
    explicit SerialAssistant(QWidget *parent = nullptr);
    ~SerialAssistant() override;

    QString selectedPortName() const;
    qint32 selectedBaudRate() const;
    QSerialPort::DataBits selectedDataBits() const;
    QSerialPort::Parity selectedParity() const;
    QSerialPort::StopBits selectedStopBits() const;
    QSerialPort::FlowControl selectedFlowControl() const;
    bool autoReconnectEnabled() const;
    int reconnectInterval() const;

    void setConnectionState(bool connected);
    void appendReceivedData(const QByteArray &data);
    void showStatusMessage(const QString &message, int timeout = 3000);
    void updatePortList(const QList<QSerialPortInfo> &ports);
    void resetCounters();

signals:
    void openPortRequested();
    void closePortRequested();
    void sendDataRequested(const QByteArray &data);
    void dtrToggled(bool enabled);
    void rtsToggled(bool enabled);
    void saveLogRequested();
    void clearReceivedRequested();
    void refreshPortsRequested();
    void ymodemSendRequested(const QString &filePath);
private slots:
    void onOpenCloseClicked();
    void onSendClicked();
    void onAutoSendToggled(bool enabled);
    void onAutoSendTimer();
    void onResetCounterClicked();
    void onPauseToggled(bool paused);
    void onMultiSendAddRow();
    void onMultiSendDeleteRow();
    void onMultiSendImportCsv();
    void onMultiSendImportCsv(QString fileName);
    void onMultiSendExportCsv();
    void onMultiSendSelected();
    void onYmodemSendClicked();
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setupUi();
    void populateBaudRates();
    void populateDataBits();
    void populateParity();
    void populateStopBits();
    void populateFlowControl();
    void populateLineEndings();

    QStringList parseCsvLine(const QString &line);
    // CSV字段标准转义
    QString csvEscape(const QString &field);


    QByteArray processSendData(const QString &text);
    QByteArray hexStringToBytes(const QString &str) const;
    QString bytesToHexString(const QByteArray &data) const;
    QString getTimestamp() const;
    quint16 crc16Modbus(const QByteArray &data) const;

    QHBoxLayout *m_mainLayout;
    QWidget *m_leftPanel;
    QWidget *m_rightPanel;

    // Receive
    QTextEdit *m_receiveText;
    QCheckBox *m_hexReceiveCheck;
    QCheckBox *m_timestampCheck;
    QCheckBox *m_autoScrollCheck;
    QCheckBox *m_autoWrapCheck;
    QCheckBox *m_pauseCheck;
    QPushButton *m_clearReceiveBtn;
    QPushButton *m_saveLogBtn;

    // Send Tabs
    QTabWidget *m_sendTab;
    QWidget *m_singleSendPage;
    QTextEdit *m_sendText;
    QPushButton *m_sendBtn;
    QPushButton *m_clearSendBtn;

    // Multi send
    QWidget *m_multiSendPage;
    QTableWidget *m_multiSendTable;
    QPushButton *m_multiAddBtn;
    QPushButton *m_multiDelBtn;
    QPushButton *m_multiImportBtn;
    QPushButton *m_multiExportBtn;
    QPushButton *m_multiSendBtn;
    QSpinBox *m_multiSendIntervalSpin;

    // Port config
    QComboBox *m_portCombo;
    QPushButton *m_refreshBtn;
    QComboBox *m_baudCombo;
    QComboBox *m_dataBitsCombo;
    QComboBox *m_parityCombo;
    QComboBox *m_stopBitsCombo;
    QComboBox *m_flowCombo;
    QCheckBox *m_dtrCheck;
    QCheckBox *m_rtsCheck;
    QCheckBox *m_autoReconnectCheck;
    QPushButton *m_openCloseBtn;

    // Send settings
    QCheckBox *m_hexSendCheck;
    QComboBox *m_lineEndingCombo;
    QCheckBox *m_autoSendCheck;
    QSpinBox *m_autoSendIntervalSpin;

    // Status
    QLabel *m_statusLabel;
    QLabel *m_portInfoLabel;
    QLabel *m_rxCountLabel;
    QLabel *m_txCountLabel;
    QPushButton *m_resetCounterBtn;

    QTimer *m_autoSendTimer;
    bool m_isConnected;
    bool m_paused;
    quint64 m_rxBytes;
    quint64 m_txBytes;

    QCheckBox *m_addCrc16Check;
    QPushButton *m_ymodemSendBtn;
};

#endif // SERIALASSISTANT_H
