#include "SerialManager.h"
#include <QDebug>
#include <QRegExp>

SerialManager::SerialManager(QObject *parent)
    : QObject(parent)
    , m_serial(nullptr)
    , m_state(Disconnected)
    , m_reconnectTimer(new QTimer(this))
    , m_readBufferTimer(new QTimer(this))
    , m_manualClose(false)
{
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &SerialManager::onReconnectTimer);
    m_readBufferTimer->setSingleShot(true);
    connect(m_readBufferTimer, &QTimer::timeout, this, &SerialManager::onReadBufferTimeout);
}

SerialManager::SerialManager(const SerialConfig &config, QObject *parent)
    : QObject(parent)
    , m_serial(nullptr)
    , m_config(config)
    , m_state(Disconnected)
    , m_reconnectTimer(new QTimer(this))
    , m_readBufferTimer(new QTimer(this))
    , m_manualClose(false)
{
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &SerialManager::onReconnectTimer);
    m_readBufferTimer->setSingleShot(true);
    connect(m_readBufferTimer, &QTimer::timeout, this, &SerialManager::onReadBufferTimeout);
}

SerialManager::~SerialManager()
{
    close();
}

bool SerialManager::createSerialPort()
{
    if (m_serial) {
        if (m_serial->isOpen()) {
            m_serial->close();
        }
        m_serial->deleteLater();
        m_serial = nullptr;
    }
    m_serial = new QSerialPort(this);
    connect(m_serial, &QSerialPort::readyRead, this, &SerialManager::onReadyRead);
    connect(m_serial, &QSerialPort::errorOccurred, this, &SerialManager::onErrorOccurred);
    return true;
}

void SerialManager::setConfig(const SerialConfig &config)
{
    if (m_state == Connected || m_state == Connecting) {
        qWarning() << "SerialManager: Cannot change config while port is open";
        return;
    }
    m_config = config;
}

SerialManager::SerialConfig SerialManager::getConfig() const
{
    return m_config;
}

QList<QSerialPortInfo> SerialManager::availablePorts()
{
    return QSerialPortInfo::availablePorts();
}

bool SerialManager::open()
{
    if (m_state == Connected || m_state == Connecting) {
        qWarning() << "SerialManager: Port is already open or connecting";
        return true;
    }
    if (m_config.portName.isEmpty()) {
        m_lastError = "Port name is empty";
        emit errorOccurred(QSerialPort::NotOpenError, m_lastError);
        setState(Error);
        return false;
    }

    setState(Connecting);
    m_manualClose = false;
    createSerialPort();
    m_serial->setPortName(m_config.portName);

    if (!applyConfig()) {
        // 配置失败销毁实例
        if (m_serial) {
            m_serial->deleteLater();
            m_serial = nullptr;
        }
        setState(Error);
        if (m_config.autoReconnect && !m_manualClose) {
            m_reconnectTimer->start(m_config.reconnectIntervalMs);
        }
        return false;
    }

    if (m_serial->open(QIODevice::ReadWrite)) {
        m_serial->clear();
        m_receiveBuffer.clear();
        m_readBufferTimer->stop();
        setState(Connected);

        emit portConnected();
        qDebug() << "SerialManager: Port" << m_config.portName << "opened successfully @" << m_config.baudRate;
        return true;
    } else {
        m_lastError = m_serial->errorString();
        qWarning() << "SerialManager: Open failed:" << m_lastError;
        // 打开失败销毁实例
        m_serial->deleteLater();
        m_serial = nullptr;
        setState(Error);

        if (m_config.autoReconnect && !m_manualClose) {
            m_reconnectTimer->start(m_config.reconnectIntervalMs);
            qDebug() << "SerialManager: Will attempt reconnect in" << m_config.reconnectIntervalMs << "ms";
        }
        return false;
    }
}

void SerialManager::close()
{
    m_manualClose = true;
    m_reconnectTimer->stop();
    m_readBufferTimer->stop();

    if (m_serial) {
        if (m_serial->isOpen()) {
            m_serial->close();
            qDebug() << "SerialManager: Port" << m_config.portName << "closed";
        }
        m_serial->deleteLater();
        m_serial = nullptr;
    }

    m_receiveBuffer.clear();
    if (m_state != Disconnected) {
        setState(Disconnected);
        emit portDisconnected();
    }
}

bool SerialManager::isOpen() const
{
    return m_serial && m_serial->isOpen() && m_state == Connected;
}

SerialManager::ConnectionState SerialManager::connectionState() const
{
    return m_state;
}

qint64 SerialManager::write(const QByteArray &data)
{
    return write(data.constData(), data.size());
}

qint64 SerialManager::write(const char *data, qint64 len)
{
    if (m_state != Connected || !m_serial || !m_serial->isOpen()) {
        qWarning() << "SerialManager: Cannot write - port not connected";
        return -1;
    }
    qint64 written = m_serial->write(data, len);
    if (written != len) {
        qWarning() << "SerialManager: Write incomplete - wrote" << written << "of" << len << "bytes";
    }
    return written;
}

void SerialManager::flush()
{
    if (m_serial && m_serial->isOpen()) {
        m_serial->flush();
    }
}

void SerialManager::clearReceiveBuffer()
{
    m_receiveBuffer.clear();
    m_readBufferTimer->stop();
}

QString SerialManager::lastError() const
{
    return m_lastError;
}

qint64 SerialManager::bytesAvailable() const
{
    return m_serial ? m_serial->bytesAvailable() : 0;
}

qint64 SerialManager::bytesToWrite() const
{
    return m_serial ? m_serial->bytesToWrite() : 0;
}

void SerialManager::onReadyRead()
{
    if (!m_serial) return;
    QByteArray newData = m_serial->readAll();
    m_receiveBuffer.append(newData);
    m_readBufferTimer->start(m_config.readBufferTimeoutMs);
}

void SerialManager::onErrorOccurred(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError || !m_serial) {
        return;
    }
    m_lastError = m_serial->errorString();
    qWarning() << "SerialManager: Error on port" << m_config.portName << ":" << error << m_lastError;
    emit errorOccurred(error, m_lastError);

    // 【关键修复】只有已连接状态下的断连错误才处理重连，连接过程中的错误不在这里销毁实例
    bool isDisconnectionError = (error == QSerialPort::ResourceError ||
                                 error == QSerialPort::DeviceNotFoundError ||
                                 error == QSerialPort::PermissionError);
    if (isDisconnectionError && m_state == Connected) {
        m_readBufferTimer->stop();
        m_receiveBuffer.clear();
        m_serial->close();

        setState(Disconnected);
        emit portDisconnected();

        if (m_config.autoReconnect && !m_manualClose) {
            setState(Connecting);
            // 运行中断开才在这里销毁实例
            m_serial->deleteLater();
            m_serial = nullptr;
            if (!m_reconnectTimer->isActive()) {
                m_reconnectTimer->start(m_config.reconnectIntervalMs);
                qDebug() << "SerialManager: Starting reconnection attempts...";
            }
        } else {
            setState(Error);
            m_serial->deleteLater();
            m_serial = nullptr;
        }
    }
}

void SerialManager::onReconnectTimer()
{
    if (m_manualClose || m_state == Connected) {
        m_reconnectTimer->stop();
        return;
    }
    qDebug() << "SerialManager: Attempting reconnection to" << m_config.portName << "...";

    createSerialPort();
    m_serial->setPortName(m_config.portName);

    if (!applyConfig()) {
        m_serial->deleteLater();
        m_serial = nullptr;
        m_reconnectTimer->start(m_config.reconnectIntervalMs);
        return;
    }

    if (m_serial->open(QIODevice::ReadWrite)) {
        m_serial->clear();
        m_receiveBuffer.clear();
        m_reconnectTimer->stop();
        setState(Connected);

        emit portConnected();
        qDebug() << "SerialManager: Reconnected to" << m_config.portName << "successfully";
    } else {
        qDebug() << "SerialManager: Reconnect failed:" << m_serial->errorString();
        m_serial->deleteLater();
        m_serial = nullptr;
        m_reconnectTimer->start(m_config.reconnectIntervalMs);
    }
}

void SerialManager::onReadBufferTimeout()
{
    if (m_receiveBuffer.isEmpty()) return;
    QByteArray receivedData = m_receiveBuffer;
    m_receiveBuffer.clear();
    emit dataReceived(receivedData);
}

void SerialManager::setState(ConnectionState state)
{
    if (m_state != state) {
        m_state = state;
        emit connectionStateChanged(m_state);
    }
}

bool SerialManager::applyConfig()
{
    if (!m_serial) return false;

    if (!m_serial->setBaudRate(m_config.baudRate)) {
        m_lastError = QString("Failed to set baud rate: %1").arg(m_serial->errorString());
        emit errorOccurred(QSerialPort::OpenError, m_lastError);
        return false;
    }
    if (!m_serial->setDataBits(m_config.dataBits)) {
        m_lastError = QString("Failed to set data bits: %1").arg(m_serial->errorString());
        emit errorOccurred(QSerialPort::OpenError, m_lastError);
        return false;
    }
    if (!m_serial->setParity(m_config.parity)) {
        m_lastError = QString("Failed to set parity: %1").arg(m_serial->errorString());
        emit errorOccurred(QSerialPort::OpenError, m_lastError);
        return false;
    }
    if (!m_serial->setStopBits(m_config.stopBits)) {
        m_lastError = QString("Failed to set stop bits: %1").arg(m_serial->errorString());
        emit errorOccurred(QSerialPort::OpenError, m_lastError);
        return false;
    }
    if (!m_serial->setFlowControl(m_config.flowControl)) {
        m_lastError = QString("Failed to set flow control: %1").arg(m_serial->errorString());
        emit errorOccurred(QSerialPort::OpenError, m_lastError);
        return false;
    }
    return true;
}

qint64 SerialManager::sendBinary(const QByteArray &data)
{
    return write(data);
}

qint64 SerialManager::sendString(const QString &text, bool appendCRLF)
{
    QByteArray data = text.toLocal8Bit();
    if (appendCRLF) data.append("\r\n");
    return write(data);
}

qint64 SerialManager::sendHex(const QString &hexStr)
{
    QByteArray data;
    QString cleanHex = hexStr;
    cleanHex.remove(QRegExp("[^0-9A-Fa-f]"));
    if (cleanHex.length() % 2 != 0) {
        qWarning() << "SerialManager: Invalid hex string length, must be even";
        return -1;
    }
    for (int i = 0; i < cleanHex.length(); i += 2) {
        bool ok;
        quint8 byte = cleanHex.mid(i, 2).toUInt(&ok, 16);
        if (!ok) {
            qWarning() << "SerialManager: Invalid hex character at position" << i;
            return -1;
        }
        data.append(static_cast<char>(byte));
    }
    return write(data);
}

void SerialManager::setDtr(bool enabled)
{
    if (m_serial && m_serial->isOpen()) {
        m_serial->setDataTerminalReady(enabled);
    }
}

void SerialManager::setRts(bool enabled)
{
    if (m_serial && m_serial->isOpen()) {
        m_serial->setRequestToSend(enabled);
    }
}
