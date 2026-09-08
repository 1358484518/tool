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
    explicit SerialAssistant(QWidget *parent = nullptr);  // 搭界面、读上次保存的串口参数
    ~SerialAssistant() override;                          // 把当前参数写回配置

    QString selectedPortName() const;                     // 下拉框里当前选中的 COM 口
    qint32 selectedBaudRate() const;                      // 当前波特率
    QSerialPort::DataBits selectedDataBits() const;       // 当前数据位
    QSerialPort::Parity selectedParity() const;           // 当前校验位
    QSerialPort::StopBits selectedStopBits() const;       // 当前停止位
    QSerialPort::FlowControl selectedFlowControl() const; // 当前流控
    bool autoReconnectEnabled() const;                    // 是否勾了掉线自动重连
    int reconnectInterval() const;                        // 自动重连间隔，毫秒

    void setConnectionState(bool connected);              // 按打开/关闭刷新按钮和控件可用状态
    void showStatusMessage(const QString &message, int timeout = 3000);  // 状态栏提示，timeout 后清掉
    void updatePortList(const QList<QSerialPortInfo> &ports);            // 刷新 COM 口下拉框
    void resetCounters();                                 // RX/TX 计数清零
    QByteArray getReceiveText();                          // 取出接收区当前文本，给保存日志用

signals:
    void openPortRequested();                             // 用户点打开，主窗口拿参数去开串口
    void closePortRequested();                            // 用户点关闭
    void sendDataRequested(const QByteArray &data);       // 组好包后交给串口后端发出
    void dtrToggled(bool enabled);                        // DTR 勾选变化
    void rtsToggled(bool enabled);                        // RTS 勾选变化
    void saveLogRequested();                              // 保存接收日志
    void clearReceivedRequested();                        // 清空接收区
    void refreshPortsRequested();                         // 刷新端口列表
    void ymodemSendRequested(const QString &filePath);    // 选中文件后请求 YModem 发送
    void ioDataReceived(const IoPacket &packet);          // 转发收到的包，方便再接其它控件

public slots:
    void onIoData(const IoPacket &packet);                // 统一收包入口：串口包写入接收区
    void appendReceivedData(const QByteArray &data);      // 把原始字节按 HEX/文本选项显示出来

private slots:
    void onOpenCloseClicked();                            // 打开/关闭按钮：按当前状态发请求
    void onSendClicked();                                 // 发送按钮：组包后发 sendDataRequested
    void onAutoSendToggled(bool enabled);                 // 定时发送开关
    void onAutoSendTimer();                               // 定时器到点再发一次
    void onResetCounterClicked();                         // 清零 RX/TX 计数
    void onPauseToggled(bool paused);                     // 暂停显示接收（数据仍会进计数）
    void onMultiSendAddRow();                             // 多条发送表加一行
    void onMultiSendDeleteRow();                          // 删掉选中行
    void onMultiSendImportCsv();                          // 弹文件框导入 CSV
    void onMultiSendImportCsv(QString fileName);          // 按路径导入多条发送表
    void onMultiSendExportCsv();                          // 弹文件框导出 CSV
    void onMultiSendExportCsv(QString filename);          // 按路径导出多条发送表
    void onMultiSendSelected();                           // 按间隔依次发送勾选的多条
    void onYmodemSendClicked();                           // 选文件并发 ymodemSendRequested
    void onDoubleSendSelected(int row, int column);       // 双击一行，立刻发这一条

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;  // 发送框 Ctrl+Enter 触发发送

private:
    void setupUi();                                       // 拼左右面板、接收区、发送页签
    void populateBaudRates();                             // 填波特率下拉
    void populateDataBits();                              // 填数据位
    void populateParity();                                // 填校验位
    void populateStopBits();                              // 填停止位
    void populateFlowControl();                           // 填流控
    void populateLineEndings();                           // 填行结束符（无 / CR / LF / CRLF）

    QStringList parseCsvLine(const QString &line);        // 按 CSV 规则拆一行
    QString csvEscape(const QString &field);              // 导出时给字段加引号转义

    QByteArray processSendData(const QString &text);      // 按 HEX / 换行 / CRC 选项把文本编成要发的字节
    QString getTimestamp() const;                         // 接收区时间戳前缀

    void importCsvFile(const QString &fileName, bool interactive);  // 读 CSV 填表，interactive 决定是否弹错
    void exportCsvFile(const QString &fileName, bool interactive);  // 把表写成 CSV
    void loadSettings();                                  // 从 QSettings 恢复上次串口参数
    void saveSettings();                                  // 把当前参数写入 QSettings

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
