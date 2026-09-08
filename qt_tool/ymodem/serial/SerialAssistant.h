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
#include <QPlainTextEdit>
#include "common/IoData.h"

/**
 * 串口页 UI。只读控件、发请求，不创建 QSerialPort。
 * 打开/发送由 TMX_TOOL 转到 SerialManager；收包走 onIoData。
 * 其它控件请用 TMX_TOOL::serialIoSource() 的 sendIoData / ioDataReceived。
 */
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
    void showStatusMessage(const QString &message, int timeout = 3000);
    void updatePortList(const QList<QSerialPortInfo> &ports);
    void resetCounters();
    QByteArray getReceiveText();

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
    void ioDataReceived(const IoPacket &packet);  // 转发，方便再接控件

public slots:
    void onIoData(const IoPacket &packet);        // 统一收包入口
    void appendReceivedData(const QByteArray &data);

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
    void onMultiSendExportCsv(QString filename);
    void onMultiSendSelected();
    void onYmodemSendClicked();
    void onDoubleSendSelected(int row, int column);  // 双击多条发送表一行

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;  // Ctrl+Enter 发送

private:
    void setupUi();
    void populateBaudRates();
    void populateDataBits();
    void populateParity();
    void populateStopBits();
    void populateFlowControl();
    void populateLineEndings();

    QStringList parseCsvLine(const QString &line);
    QString csvEscape(const QString &field);

    QByteArray processSendData(const QString &text);  // 按 HEX / 换行 / CRC 选项组包
    QString getTimestamp() const;

    void importCsvFile(const QString &fileName, bool interactive);
    void exportCsvFile(const QString &fileName, bool interactive);
    void loadSettings();
    void saveSettings();

    QHBoxLayout *m_mainLayout;
    QWidget *m_leftPanel;
    QWidget *m_rightPanel;

    // 接收区
    QPlainTextEdit *m_receiveText;
    QCheckBox *m_hexReceiveCheck;
    QCheckBox *m_timestampCheck;
    QCheckBox *m_autoScrollCheck;
    QCheckBox *m_autoWrapCheck;
    QCheckBox *m_pauseCheck;
    QPushButton *m_clearReceiveBtn;
    QPushButton *m_saveLogBtn;

    // 发送：单条 / 多条
    QTabWidget *m_sendTab;
    QWidget *m_singleSendPage;
    QPlainTextEdit *m_sendText;
    QPushButton *m_sendBtn;
    QPushButton *m_clearSendBtn;

    QWidget *m_multiSendPage;
    QTableWidget *m_multiSendTable;
    QPushButton *m_multiAddBtn;
    QPushButton *m_multiDelBtn;
    QPushButton *m_multiImportBtn;
    QPushButton *m_multiExportBtn;
    QPushButton *m_multiSendBtn;
    QSpinBox *m_multiSendIntervalSpin;

    // 右侧串口参数
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

    QCheckBox *m_hexSendCheck;
    QComboBox *m_lineEndingCombo;
    QCheckBox *m_autoSendCheck;
    QSpinBox *m_autoSendIntervalSpin;

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
    bool m_hasRecvData = false;
};

#endif // SERIALASSISTANT_H
