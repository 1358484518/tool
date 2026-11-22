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

class FastTextView;
class SerialManager;
class QYmodemFile;
class QThread;

class QFile;
class QGroupBox;

/**
 * 串口页。界面、串口线程和 YModem 都收在这一页里。
 * 主窗口只把页面放进页签。别的模块要收发字节时用 ioSource()，不要直接碰串口。
 */
class SerialAssistant : public QWidget
{
    Q_OBJECT
public:
    explicit SerialAssistant(QWidget *parent = nullptr);  // 搭界面、拉起串口线程、读上次保存的参数
    ~SerialAssistant() override;                          // 停 YModem 和串口线程，再把参数写回配置

    IoSource *ioSource() const;                           // 串口链路。外部只连这个对象收发

signals:
    void ioDataReceived(const IoPacket &packet);          // 把收到的包再发出来，方便别的控件跟着看

private slots:
    void onIoData(const IoPacket &packet);                // 串口线程送来的包，写入接收区
    void appendReceivedData(const QByteArray &data);      // 记时间戳，原始字节进显示缓冲
    void onPortConnected();                               // 打开成功，刷新按钮和状态
    void onPortDisconnected(bool manualClose);            // 端口关掉。掉线且允许重连时提示正在重连
    void onOpenResult(bool ok, const QString &errorString);  // 打开失败时弹出原因
    void onSerialError(int error, const QString &errorString);  // 串口报错，只显示文字
    void onOpenCloseClicked();                            // 打开前先确认波特率，再按连接状态投递给后端
    void onBaudIndexChanged(int index);                   // 从列表点选时记住这档，非法输入才能退回
    void commitBaudEdit();                                // 校验输入框，合法就选中，新的自定义档立刻写入配置
    void onSendClicked();                                 // 发送按钮：组包后交给串口线程
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
    void onYmodemSendClicked();                           // 选文件后在本页启动 YModem
    void onDoubleSendSelected(int row, int column);       // 双击一行，立刻发这一条

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;  // 发送框 Ctrl+Enter 触发发送
    void changeEvent(QEvent *event) override;                    // 语言切换时刷新本页文字

private:
    void setupUi();                                       // 拼左右面板、接收区、发送页签
    void retranslateUi();                                 // 按当前语言重写本页控件文字
    void populateBaudRates();                             // 填常用波特率，并允许输入自定义值
    void insertBaudRate(qint32 baud);                     // 按数值把一档插进下拉，已有则跳过
    void selectBaudRate(qint32 baud);                     // 选中这一档，没有就先插入
    void updateBaudToolTip();                             // 按当前语言刷新波特率输入提示
    bool baudFromText(const QString &text, qint32 *baud) const;  // 文本是否为允许范围内的整数波特率
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
    void loadSettings();                                  // 从 QSettings 恢复上次串口参数和自定义波特率
    void saveSettings();                                  // 把当前参数和自定义波特率写入 QSettings
    void setAutoSaveEnabled(bool enabled);                // 打开或关掉接收内容自动追加
    bool openAutoSave();                                  // 按当前日志目录打开追加文件，失败则取消勾选
    void appendAutoSave(const QByteArray &bytes);         // 把时间戳和原始字节追加进文件，攒一批再落盘
    void scheduleUiFlush();                               // 计数和自动保存最多 200 毫秒刷一次
    void flushUiPending();                                // 把接收计数和自动保存写出去
    void startSerialBackend();                            // 创建串口线程，并把本页信号接到后端
    void shutdownSerial();                                // 在串口线程里关掉端口并销毁后端
    void requestOpen();                                   // 用当前界面参数打开串口
    void requestClose();                                  // 让串口线程关闭端口
    void requestSend(const QByteArray &data);             // 把要发的字节投到串口线程
    void onSaveLog();                                     // 按当前勾选把接收缓冲写成文本文件
    void startYmodem(const QString &filePath);            // 接上串口收发，开始传这个文件
    void stopYmodemTransfer();                            // 停传输并拆掉和串口的连接
    QString selectedPortName() const;                     // 下拉框里当前选中的 COM 口
    qint32 selectedBaudRate() const;                      // 当前波特率，以输入框里的数字为准
    QSerialPort::DataBits selectedDataBits() const;       // 当前数据位
    QSerialPort::Parity selectedParity() const;           // 当前校验位
    QSerialPort::StopBits selectedStopBits() const;       // 当前停止位
    QSerialPort::FlowControl selectedFlowControl() const; // 当前流控
    bool autoReconnectEnabled() const;                    // 是否勾了掉线自动重连
    int reconnectInterval() const;                        // 自动重连间隔，毫秒
    void setConnectionState(bool connected);              // 按打开/关闭刷新按钮和控件可用状态
    void showStatusMessage(const QString &message, int timeout = 3000);  // 状态栏提示
    void updatePortList(const QList<QSerialPortInfo> &ports);            // 刷新 COM 口下拉框
    void resetCounters();                                 // RX/TX 计数清零
    QByteArray getReceiveText();                          // 按当前勾选格式化接收缓冲，给保存日志用

    QHBoxLayout *m_mainLayout;
    QWidget *m_leftPanel;
    QWidget *m_rightPanel;

    // 接收区
    FastTextView *m_receiveText;
    QCheckBox *m_hexReceiveCheck;
    QCheckBox *m_timestampCheck;
    QCheckBox *m_autoScrollCheck;
    QCheckBox *m_autoWrapCheck;
    QCheckBox *m_pauseCheck;
    QCheckBox *m_autoSaveCheck = nullptr;  // 勾选后把接收窗口的新内容追加进文件
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

    QGroupBox *m_portGroup = nullptr;     // 端口参数分组
    QGroupBox *m_sendGroup = nullptr;     // 发送设置分组
    QGroupBox *m_recvGroup = nullptr;     // 接收设置分组
    QLabel *m_intLabel = nullptr;         // 多条发送的间隔标签
    QLabel *m_portLabel = nullptr;        // 端口
    QLabel *m_baudLabel = nullptr;        // 波特率
    QLabel *m_stopLabel = nullptr;        // 停止位
    QLabel *m_dataLabel = nullptr;        // 数据位
    QLabel *m_parityLabel = nullptr;      // 校验
    QLabel *m_flowLabel = nullptr;        // 流控
    QLabel *m_intervalLabel = nullptr;    // 定时发送间隔
    QLabel *m_lineEndLabel = nullptr;     // 行结束符
    QFile *m_autoSaveFile = nullptr;      // 自动保存打开时追加写入，关掉就释放
    QTimer *m_uiFlushTimer = nullptr;     // 接收计数和自动保存共用，避免每包都刷界面和磁盘
    int m_autoSavePending = 0;            // 距上次 flush 又写入的字节数
    QList<qint32> m_customBaudRates;      // 用户输入过、需要写进配置的波特率
    qint32 m_lastValidBaud = 115200;      // 最近一次合法波特率，非法输入时退回这一档
    bool m_baudEditLock = false;          // 正在改下拉框，避免 editingFinished 重入
    QThread *m_serialThread = nullptr;    // 串口后端所在的线程，页面销毁时退出
    SerialManager *m_serial = nullptr;    // 无 parent，一直活在 m_serialThread
    QYmodemFile *m_ymodem = nullptr;      // 有传输时才创建，结束或断口后释放
};

#endif // SERIALASSISTANT_H
