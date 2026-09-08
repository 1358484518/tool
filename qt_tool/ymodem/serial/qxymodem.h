/*
 * XModem / YModem。协议跑在独立 QThread 里，和串口用信号交换字节。
 * 对端 5 秒不回则 XMODEM_ERROR_IDLETIMEOUT。
 */
#ifndef QXYMODEM_H
#define QXYMODEM_H
#include <QString>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QDebug>
#include <QElapsedTimer>
#include <QtGlobal>
#include <atomic>

/** 协议状态机基类。子类负责读文件、把字节发到串口。 */
class QXYmodem: public QThread {
    Q_OBJECT
public:
    static const int DefaultIdleTimeoutMs = 5000;

    /** type 为 XMODEM 或 YMODEM；sendPktSize 是数据包长度（128 或 1024）。 */
    explicit QXYmodem(int type, unsigned short sendPktSize = 128, int timeout = 3000, int retry_limit = 10, bool no_timeout = false, QObject *parent = nullptr):
        QThread(parent),m_type(type),m_sendPktSize(sendPktSize),m_timeout(timeout),m_retry_limit(retry_limit),m_no_timeout(no_timeout) {};
    ~QXYmodem() override {};  // 子类析构里会 requestStop + wait
    enum {
        SEND,
        RECV
    };
    enum {
        XMODEM,
        YMODEM
    };
    enum {  // 控制字符
        SOH	  = 0x01,
        STX	  = 0x02,
        EOT	  = 0x04,
        ACK	  = 0x06,
        NAK	  = 0x15,
        CAN	  = 0x18,
        CTRLZ = 0x1A,
    };
    enum {  // 结束码，0 成功，负数为失败
        XMODEM_ABORT = 2,
        XMODEM_END = 1,
        XMODEM_OK = 0,
        XMODEM_ERROR_REMOTECANCEL = -1,
        XMODEM_ERROR_OUTOFSYNC	  = -2,
        XMODEM_ERROR_RETRYEXCEED  = -3,
        XMODEM_ERROR_IDLETIMEOUT  = -4,  // 对端一直没回
    };
    void startSend(void) {   // 置为发送方向并启动协议线程
        dir=SEND;
        m_abort.store(false);
        m_result = XMODEM_OK;
        start();
    }
    void startRecv(void) {   // 置为接收方向并启动协议线程
        dir=RECV;
        m_abort.store(false);
        m_result = XMODEM_OK;
        start();
    }
    void requestStop(void) {  // 协作式停止，run() 里会看到
        m_abort.store(true);
    }
    bool getStopFlag(void) {  // 协议循环是否已被要求退出
        return m_abort.load();
    }
    int lastResult() const {  // 最近一次传输结束码，见上面 XMODEM_* 枚举
        return m_result;
    }
    void setIdleTimeoutMs(int ms) {  // 对端多久不回算超时，0 表示不启用
        m_idleTimeoutMs = qMax(0, ms);
    }
protected:
    void run() override {  // 线程入口：按类型跑 X/YModem 收或发，结束时写 m_result
        m_idleTimer.start();
        _start();
        int ret = XMODEM_OK;
        if(m_type == XMODEM) {
            if(dir == SEND) {
                ret = xmodemTransmit(m_sendPktSize);
            } else {
                ret = xmodemReceive();
            }
        } else {
            do {
                ret = ymodemTransmit(m_sendPktSize);
                if(ret == XMODEM_OK) {
                    transferOnce();
                }
                if(getStopFlag()) {
                    ret = idleTimedOut() ? XMODEM_ERROR_IDLETIMEOUT : XMODEM_ABORT;
                    break;
                }
            } while(ret != XMODEM_ERROR_REMOTECANCEL &&
                    ret != XMODEM_ERROR_RETRYEXCEED &&
                    ret != XMODEM_ABORT &&
                    ret != XMODEM_ERROR_IDLETIMEOUT &&
                    ret != XMODEM_END);
        }
        if(getStopFlag() && idleTimedOut())
            ret = XMODEM_ERROR_IDLETIMEOUT;
        m_result = ret;
        _end();
    }
private:
    virtual void _start(void) = 0;                 // 协议开始：打开文件或复位文件下标
    virtual void _end(void) = 0;                   // 协议结束：关文件并发 complete
    virtual int writefile(const char* buffer, int size) = 0;   // 把收到的数据块写入文件
    virtual int readfile(char* buffer, int size) = 0;          // 从文件读出一块准备发送
    virtual int flushfile(void) = 0;                           // 把文件缓冲刷到磁盘
    virtual int writefileInfo(const char* buffer, int size) = 0; // YModem 收：按包头文件名建文件
    virtual int readfileInfo(char* buffer, int size) = 0;        // YModem 发：把文件名和长度填进包头
    virtual int transferOnce(void) = 0;            // 一个文件传完后的收尾（关文件，准备下一个）
    virtual int sendStream(const char* buffer, int size) = 0;    // 把协议字节交给串口发出
    virtual int receiveStream(const char* buffer, int size) = 0; // 从串口缓存取出协议字节
    virtual void clearReceiveCache() = 0;          // 丢掉尚未消费的串口缓存
    virtual void timerPause(int t) {               // 等待对端时休眠，默认空实现
        Q_UNUSED(t);
    }
    void xmodemOut(unsigned char c) {              // 发一个控制字符（ACK/NAK/EOT 等）
        sendStream((const char*)&c,1);
    }
    int xmodemIn(unsigned char *c) {               // 试图读一个字节，没有则返回 0
        return receiveStream((char*)c,1);
    }
    uint16_t crc_xmodem_update(uint16_t crc, uint8_t data);  // XModem CRC16 累加一字节
    long xmodemReceive(void);                       // XModem 收文件主循环
    long ymodemReceive(void);                       // YModem 收文件主循环
    long xmodemTransmit(unsigned short pktsize = 128);  // XModem 发文件主循环
    long ymodemTransmit(unsigned short pktsize = 1024); // YModem 发一个文件（含文件名包）
    int xmodemCrcCheck(int crcflag, const unsigned char *buffer, int size);  // 校验数据块 CRC 或校验和
    int xmodemInTime(unsigned char *c, unsigned short timeout);  // 限时等一个字节
    void xmodemInFlush(void);                       // 丢掉接收缓存里残留字节
    bool idleTimedOut() const {                     // 对端是否已经超过空闲超时
        return m_idleTimeoutMs > 0 && m_idleTimer.isValid() && m_idleTimer.hasExpired(m_idleTimeoutMs);
    }
    void notePeerResponse() {                       // 对端有回应时把空闲计时清零
        if (m_idleTimeoutMs > 0)
            m_idleTimer.restart();
    }
private:
    int dir=SEND;
    int m_type=XMODEM;
    unsigned short m_sendPktSize = 128;
    int m_timeout = 3000;
    int m_retry_limit = 10;
    bool m_no_timeout = false;
    std::atomic<bool> m_abort{false};
    int m_idleTimeoutMs = DefaultIdleTimeoutMs;
    int m_result = XMODEM_OK;
    QElapsedTimer m_idleTimer;
};

/** 单文件 XModem，通过 send/receive 信号和串口交换数据。 */
class QXmodemFile: public QXYmodem {
    Q_OBJECT
public:
    QXmodemFile(QString filename,unsigned short sendPktSize = 128, int timeout = 3000, int retry_limit = 10, bool no_timeout = false, QObject *parent = nullptr) :
        QXYmodem(QXYmodem::XMODEM,sendPktSize,timeout,retry_limit,no_timeout,parent)
    {
        m_file = new QFile(filename);
    }
    QXmodemFile(const char *filename,unsigned short sendPktSize = 128, int timeout = 3000, int retry_limit = 10, bool no_timeout = false,QObject *parent = nullptr) :
        QXYmodem(QXYmodem::XMODEM,sendPktSize,timeout,retry_limit,no_timeout,parent)
    {
        m_file = new QFile(QString(filename));
    }
    QXmodemFile(QString filename, QObject *parent = nullptr) :  // 用默认 128 字节包发/收单文件
        QXYmodem(QXYmodem::XMODEM,128,3000,10,false,parent)
    {
        m_file = new QFile(filename);
    }
    ~QXmodemFile() override {                       // 停线程后关文件
        requestStop();
        wait();
        if(m_file) {
            if(m_file->isOpen()) m_file->close();
            delete m_file;
            m_file = nullptr;
        }
    }
signals:
    void transferring(QString filename);            // 开始传某个文件
    void tick(long bytes_sent, long bytes_total);   // 进度：已传字节 / 总字节
    void complete(QString filename, int result, size_t size);  // 结束，result 见 XMODEM_*
    void send(QByteArray ba);                       // 协议要写到串口的字节
public slots:
    void receive(QByteArray ba) {                   // 串口来的字节先堆进 cache
        QMutexLocker locker(&m_mutex);
        cache.append(ba);
    }
private:
    void _start(void) override {                    // 只读打开文件并通知 UI 开始传
        m_file->open(QIODevice::ReadOnly);
        QFileInfo info(m_file->fileName());
        emit transferring(info.fileName());
    }
    void _end(void) override {                      // 关文件并发 complete
        QFileInfo info(m_file->fileName());
        emit complete(info.fileName(), lastResult(), m_file->size());
        if(m_file->isOpen()) m_file->close();
    }
    int writefile(const char* buffer, int size) override {  // 收模式：写入本地文件
        emit tick(m_file->pos(),-1);
        return m_file->write(buffer,size);
    }
    int readfile(char* buffer, int size) override {         // 发模式：从文件读一块
        emit tick(m_file->pos(),m_file->size());
        return m_file->read(buffer,size);
    }
    int flushfile(void) override                    // 把已写入的文件刷到磁盘
    {
        if(m_file) {
            if(m_file->isOpen())
                return m_file->flush();
        }
        return 0;
    }
    int writefileInfo(const char* buffer, int size) override {  // XModem 没有文件名包，空实现
        Q_UNUSED(buffer);
        Q_UNUSED(size);
        return 0;
    }
    int readfileInfo(char* buffer, int size) override {         // XModem 没有文件名包，空实现
        Q_UNUSED(buffer);
        Q_UNUSED(size);
        return 0;
    }
    int transferOnce(void) override {               // XModem 只传一个文件，不用切下一个
        return 0;
    }
    int sendStream(const char* buffer, int size) override {     // 发出 send 信号，由串口真正写出
        emit send(QByteArray(buffer,size));
        return size;
    }
    int receiveStream(const char* buffer, int size) override {  // 从 cache 取出最多 size 字节
        QMutexLocker locker(&m_mutex);
        int ret = qMin(size,cache.size());
        memcpy((void*)buffer,cache.data(),ret);
        cache.remove(0,ret);
        return ret;
    }
    void clearReceiveCache() override {             // 丢掉尚未消费的串口缓存
        QMutexLocker locker(&m_mutex);
        cache.clear();
    }
    void timerPause(int t) override {               // 协议等待时休眠 t 毫秒
        QThread::msleep(t);
    }
private:
    QFile *m_file = nullptr;
    QMutex m_mutex;
    QByteArray cache;
};

/** 多文件 YModem。本程序用它从串口页发文件。 */
class QYmodemFile: public QXYmodem {
    Q_OBJECT
public:
    QYmodemFile(QStringList filePathList,unsigned short sendPktSize = 1024, int timeout = 3000, int retry_limit = 10, bool no_timeout = false, QObject *parent = nullptr) :
        QXYmodem(QXYmodem::YMODEM,sendPktSize,timeout,retry_limit,no_timeout,parent)
    {
        m_filePathList = filePathList;
    }
    QYmodemFile(QStringList filePathList, QObject *parent = nullptr) :  // 用默认 1024 字节包发多个文件
        QXYmodem(QXYmodem::YMODEM,1024,3000,10,false,parent)
    {
        m_filePathList = filePathList;
    }
    ~QYmodemFile() override {                       // 停线程后关当前文件
        requestStop();
        wait();
        if(m_file) {
            if(m_file->isOpen()) m_file->close();
            delete m_file;
            m_file = nullptr;
        }
    }
signals:
    void send(QByteArray ba);                       // 协议要写到串口的字节
    void transferring(QString filename);            // 开始传某个文件
    void tick(long bytes_sent, long bytes_total);   // 当前文件进度
    void complete(QString filename,int result, size_t size);  // 整批结束
public slots:
    void receive(QByteArray ba) {                   // 串口来的字节先堆进 cache
        QMutexLocker locker(&m_mutex);
        cache.append(ba);
    }
private:
    void _start(void) override {                    // 从列表第一个文件开始
        m_fileIndex = 0;
    }
    void _end(void) override {                      // 用第一个文件名发 complete
        if (!m_filePathList.isEmpty()) {
            QFileInfo info(m_filePathList.first());
            emit complete(info.fileName(), lastResult(), info.size());
        }
    }
    int writefile(const char* buffer, int size) override {  // 收模式：写入当前文件，不超过声明长度
        if(m_currentSize+size > m_fileSize) {
            size = m_fileSize - m_currentSize;
        }
        emit tick(m_currentSize,m_fileSize);
        int w = m_file->write(buffer,size);
        m_currentSize += w;
        return w;
    }
    int readfile(char* buffer, int size) override {         // 发模式：从当前文件读一块
        emit tick(m_currentSize,m_fileSize);
        int r = m_file->read(buffer,size);
        m_currentSize += r;
        return r;
    }
    int flushfile(void) override                    // 把当前文件刷到磁盘
    {
        if(m_file) {
            if(m_file->isOpen())
                return m_file->flush();
        }
        return 0;
    }
    int readfileInfo(char* buffer, int size) override {     // 发：打开下一个文件，把文件名和长度填进包头
        memset(buffer, 0, size);
        if(m_fileIndex < m_filePathList.size()) {
            QString filename = m_filePathList.at(m_fileIndex);
            m_file = new QFile(filename);
            QFileInfo fileInfo(filename);
            QByteArray nameBytes = fileInfo.fileName().toLocal8Bit();
            QByteArray sizeBytes = QByteArray::number(fileInfo.size());
            int nameLen = qMin(nameBytes.size(), size - 64);
            memcpy(buffer, nameBytes.data(), nameLen);
            buffer[nameLen] = 0;
            memcpy(buffer + nameLen + 1, sizeBytes.data(), qMin(sizeBytes.size(), size - nameLen - 2));
            m_file->open(QIODevice::ReadOnly);
            m_fileSize  = fileInfo.size();
            m_currentSize = 0;
            m_fileIndex++;
            emit transferring(fileInfo.fileName());
            return size;
        } else {
            return 0;
        }
    }
    int writefileInfo(const char* buffer, int s) override {  // 收：按包头文件名在目录里建文件
        Q_UNUSED(s);
        if(m_filePathDir.size() > 0) {
            int  i =  0;
            char name[256] = {0};
//            char size[256] = {0};
            for(int j = 0; buffer[i] != 0 && i < 255; i++, j++) {
                name[j] = buffer[i];
            }
//            i++;
//            for(int j = 0; buffer[i] != 0 && i < 511; i++, j++) {
//                size[j] = buffer[i];
//            }
            QString fileName = QString::fromLocal8Bit(name);
            m_file = new QFile(m_filePathDir + QDir::separator() + fileName);
            m_file->open(QIODevice::WriteOnly);
            emit transferring(fileName);
            return s;
        } else {
            return 0;
        }
    }
    int transferOnce(void) override {               // 当前文件传完：关掉，不在这里发 complete
        // 只关闭文件，不发成功信号
        if(m_file){
            if(m_file->isOpen()) {
                m_file->close();
                delete m_file;
                m_file = nullptr;
            }
        }
        return 0;
    }
    int sendStream(const char* buffer, int size) override {     // 发出 send 信号，由串口真正写出
        emit send(QByteArray(buffer,size));
        return size;
    }
    int receiveStream(const char* buffer, int size) override {  // 从 cache 取出最多 size 字节
        QMutexLocker locker(&m_mutex);
        int ret = qMin(size,cache.size());
        memcpy((void*)buffer,cache.data(),ret);
        cache.remove(0,ret);
        return ret;
    }
    void clearReceiveCache() override {             // 丢掉尚未消费的串口缓存
        QMutexLocker locker(&m_mutex);
        cache.clear();
    }
    void timerPause(int t) override {               // 协议等待时休眠 t 毫秒
        QThread::msleep(t);
    }
private:
    QFile *m_file = nullptr;
    QStringList m_filePathList;
    QString m_filePathDir;
    int m_fileIndex = 0;
    qint64 m_fileSize = 0;
    qint64 m_currentSize = 0;
    QMutex m_mutex;
    QByteArray cache;
};
#endif
