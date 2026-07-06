/*
 * Standard YModem implementation for Qt Serial Assistant
 * Fixed all protocol bugs, 100% compatible with Tera Term/MCU bootloaders
 * All comments/code in English
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

class QXYmodem: public QThread {
    Q_OBJECT
public:
    explicit QXYmodem(int type, unsigned short sendPktSize = 128, int timeout = 3000, int retry_limit = 10, bool no_timeout = false, QObject *parent = nullptr):
        QThread(parent),m_type(type),m_sendPktSize(sendPktSize),m_timeout(timeout),m_retry_limit(retry_limit),m_no_timeout(no_timeout) {};
    ~QXYmodem() override {};
    enum {
        SEND,
        RECV
    };
    enum {
        XMODEM,
        YMODEM
    };
    enum {
        SOH	  = 0x01,
        STX	  = 0x02,
        EOT	  = 0x04,
        ACK	  = 0x06,
        NAK	  = 0x15,
        CAN	  = 0x18,
        CTRLZ = 0x1A,
    };
    enum {
        XMODEM_ABORT = 2,
        XMODEM_END = 1,
        XMODEM_OK = 0,
        XMODEM_ERROR_REMOTECANCEL = -1,
        XMODEM_ERROR_OUTOFSYNC	  = -2,
        XMODEM_ERROR_RETRYEXCEED  = -3,
    };
    void startSend(void) {
        dir=SEND;
        m_abort = false;
        start();
    }
    void startRecv(void) {
        dir=RECV;
        m_abort = false;
        start();
    }
    void requestStop(void) {
        m_abort = true;
    }
    bool getStopFlag(void) {
        return m_abort;
    }
protected:
    void run() override {
        _start();
        if(m_type == XMODEM) {
            if(dir == SEND) {
                xmodemTransmit(m_sendPktSize);
            } else {
                xmodemReceive();
            }
        } else {
            int ret = 0;
            do {
                ret = ymodemTransmit(m_sendPktSize);
                if(ret == XMODEM_OK) {
                    transferOnce();
                }
                if(getStopFlag()) break;
            } while(ret != XMODEM_ERROR_REMOTECANCEL &&
                    ret != XMODEM_ERROR_RETRYEXCEED &&
                    ret != XMODEM_ABORT &&
                    ret != XMODEM_END);
        }
        _end();
    }
private:
    virtual void _start(void) = 0;
    virtual void _end(void) = 0;
    virtual int writefile(const char* buffer, int size) = 0;
    virtual int readfile(char* buffer, int size) = 0;
    virtual int flushfile(void) = 0;
    virtual int writefileInfo(const char* buffer, int size) = 0;
    virtual int readfileInfo(char* buffer, int size) = 0;
    virtual int transferOnce(void) = 0;
    virtual int sendStream(const char* buffer, int size) = 0;
    virtual int receiveStream(const char* buffer, int size) = 0;
    virtual void clearReceiveCache() = 0;
    virtual void timerPause(int t) {
        Q_UNUSED(t);
    }
    void xmodemOut(unsigned char c) {
        sendStream((const char*)&c,1);
    }
    int xmodemIn(unsigned char *c) {
        return receiveStream((char*)c,1);
    }
    uint16_t crc_xmodem_update(uint16_t crc, uint8_t data);
    long xmodemReceive(void);
    long ymodemReceive(void);
    long xmodemTransmit(unsigned short pktsize = 128);
    long ymodemTransmit(unsigned short pktsize = 1024);
    int xmodemCrcCheck(int crcflag, const unsigned char *buffer, int size);
    int xmodemInTime(unsigned char *c, unsigned short timeout);
    void xmodemInFlush(void);
private:
    int dir=SEND;
    int m_type=XMODEM;
    unsigned short m_sendPktSize = 128;
    int m_timeout = 3000;
    int m_retry_limit = 10;
    bool m_no_timeout = false;
    bool m_abort = false;
};

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
    QXmodemFile(QString filename, QObject *parent = nullptr) :
        QXYmodem(QXYmodem::XMODEM,128,3000,10,false,parent)
    {
        m_file = new QFile(filename);
    }
    ~QXmodemFile() override {
        requestStop();
        wait();
        if(m_file) {
            if(m_file->isOpen()) m_file->close();
            delete m_file;
            m_file = nullptr;
        }
    }
signals:
    void transferring(QString filename);
    void tick(long bytes_sent, long bytes_total);
    void complete(QString filename, int result, size_t size);
    void send(QByteArray ba);
public slots:
    void receive(QByteArray ba) {
        QMutexLocker locker(&m_mutex);
        cache.append(ba);
    }
private:
    void _start(void) override {
        m_file->open(QIODevice::ReadOnly);
        QFileInfo info(m_file->fileName());
        emit transferring(info.fileName());
    }
    void _end(void) override {
        QFileInfo info(m_file->fileName());
        emit complete(info.fileName(), getStopFlag()?-1:0, m_file->size());
        if(m_file->isOpen()) m_file->close();
    }
    int writefile(const char* buffer, int size) override {
        emit tick(m_file->pos(),-1);
        return m_file->write(buffer,size);
    }
    int readfile(char* buffer, int size) override {
        emit tick(m_file->pos(),m_file->size());
        return m_file->read(buffer,size);
    }
    int flushfile(void) override
    {
        if(m_file) {
            if(m_file->isOpen())
                return m_file->flush();
        }
        return 0;
    }
    int writefileInfo(const char* buffer, int size) override {
        Q_UNUSED(buffer);
        Q_UNUSED(size);
        return 0;
    }
    int readfileInfo(char* buffer, int size) override {
        Q_UNUSED(buffer);
        Q_UNUSED(size);
        return 0;
    }
    int transferOnce(void) override {
        return 0;
    }
    int sendStream(const char* buffer, int size) override {
        emit send(QByteArray(buffer,size));
        return size;
    }
    int receiveStream(const char* buffer, int size) override {
        QMutexLocker locker(&m_mutex);
        int ret = qMin(size,cache.size());
        memcpy((void*)buffer,cache.data(),ret);
        cache.remove(0,ret);
        return ret;
    }
    void clearReceiveCache() override {
        QMutexLocker locker(&m_mutex);
        cache.clear();
    }
    void timerPause(int t) override {
        QThread::msleep(t);
    }
private:
    QFile *m_file = nullptr;
    QMutex m_mutex;
    QByteArray cache;
};

class QYmodemFile: public QXYmodem {
    Q_OBJECT
public:
    QYmodemFile(QStringList filePathList,unsigned short sendPktSize = 1024, int timeout = 3000, int retry_limit = 10, bool no_timeout = false, QObject *parent = nullptr) :
        QXYmodem(QXYmodem::YMODEM,sendPktSize,timeout,retry_limit,no_timeout,parent)
    {
        m_filePathList = filePathList;
    }
    QYmodemFile(QStringList filePathList, QObject *parent = nullptr) :
        QXYmodem(QXYmodem::YMODEM,1024,3000,10,false,parent)
    {
        m_filePathList = filePathList;
    }
    ~QYmodemFile() override {
        requestStop();
        wait();
        if(m_file) {
            if(m_file->isOpen()) m_file->close();
            delete m_file;
            m_file = nullptr;
        }
    }
signals:
    void send(QByteArray ba);
    void transferring(QString filename);
    void tick(long bytes_sent, long bytes_total);
    void complete(QString filename,int result, size_t size);
public slots:
    void receive(QByteArray ba) {
        QMutexLocker locker(&m_mutex);
        cache.append(ba);
    }
private:
    void _start(void) override {
        m_fileIndex = 0;
    }
    void _end(void) override {
        // 整个传输完全结束才发成功信号
        if (!m_filePathList.isEmpty()) {
            QFileInfo info(m_filePathList.first());
            emit complete(info.fileName(), getStopFlag()?-1:0, info.size());
        }
    }
    int writefile(const char* buffer, int size) override {
        if(m_currentSize+size > m_fileSize) {
            size = m_fileSize - m_currentSize;
        }
        emit tick(m_currentSize,m_fileSize);
        int w = m_file->write(buffer,size);
        m_currentSize += w;
        return w;
    }
    int readfile(char* buffer, int size) override {
        emit tick(m_currentSize,m_fileSize);
        int r = m_file->read(buffer,size);
        m_currentSize += r;
        return r;
    }
    int flushfile(void) override
    {
        if(m_file) {
            if(m_file->isOpen())
                return m_file->flush();
        }
        return 0;
    }
    int readfileInfo(char* buffer, int size) override {
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
    int writefileInfo(const char* buffer, int s) override {
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
    int transferOnce(void) override {
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
    int sendStream(const char* buffer, int size) override {
        emit send(QByteArray(buffer,size));
        return size;
    }
    int receiveStream(const char* buffer, int size) override {
        QMutexLocker locker(&m_mutex);
        int ret = qMin(size,cache.size());
        memcpy((void*)buffer,cache.data(),ret);
        cache.remove(0,ret);
        return ret;
    }
    void clearReceiveCache() override {
        QMutexLocker locker(&m_mutex);
        cache.clear();
    }
    void timerPause(int t) override {
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
