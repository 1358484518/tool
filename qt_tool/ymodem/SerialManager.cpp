#include "SerialManager.h"
#include "ProtocolUtils.h"

#include <QDebug>

static const int kMaxReceiveBufferBytes = 256 * 1024;

SerialManager::SerialManager(QObject *parent)
    : QObject(parent)
    , m_reconnectTimer(new QTimer(this))
    , m_readBufferTimer(new QTimer(this))
{
    qRegisterMetaType<SerialManager::SerialConfig>("SerialManager::SerialConfig");
    qRegisterMetaType<SerialManager::SerialConfig>("SerialConfig");
    qRegisterMetaType<SerialManager::ConnectionState>("SerialManager::ConnectionState");

    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &SerialManager::onReconnectTimer);
    m_readBufferTimer->setSingleShot(true);
    connect(m_readBufferTimer, &QTimer::timeout, this, &SerialManager::onReadBufferTimeout);
}

SerialManager::SerialManager(const SerialConfig &config, QObject *parent)
    : SerialManager(parent)
{
    m_config = config;
}

SerialManager::~SerialManager()
{
    m_manualClose = true;
    m_reconnectTimer->stop();
    m_readBufferTimer->stop();
    destroySerialPort();
}

SerialManager::SerialConfig SerialManager::getConfig() const
{
    return m_config;
}

QList<QSerialPortInfo> SerialManager::availablePorts()
{
    return QSerialPortInfo::availablePorts();
}

void SerialManager::setConfig(const SerialManager::SerialConfig &config)
{
    if (m_state == Connected || m_state == Connecting) {
        qWarning() << "SerialManager: Cannot change config while port is open";
        return;
    }
    m_config = config;
}

void SerialManager::openWithConfig(const SerialManager::SerialConfig &config)
{
    if (m_state == Connected || m_state == Connecting)
        close();
    m_config = config;
    const bool ok = open();
    emit openResult(ok, ok ? QString() : m_lastError);
}

bool SerialManager::createSerialPort()
{
    destroySerialPort();
    m_serial = new QSerialPort(this);
    connect(m_serial, &QSerialPort::readyRead, this, &SerialManager::onReadyRead);
    connect(m_serial, &QSerialPort::errorOccurred, this, &SerialManager::onErrorOccurred);
    return true;
}

void SerialManager::destroySerialPort()
{
    if (!m_serial)
        return;
    if (m_serial->isOpen())
        m_serial->close();
    m_serial->deleteLater();
    m_serial = nullptr;
}

bool SerialManager::open()
{
    if (m_state == Connected || m_state == Connecting) {
        qWarning() << "SerialManager: Port is already open or connecting";
        return true;
    }
    if (m_config.portName.isEmpty()) {
        m_lastError = QStringLiteral("Port name is empty");
        emit errorOccurred(QSerialPort::NotOpenError, m_lastError);
        setState(Error);
        return false;
    }

    setState(Connecting);
    m_manualClose = false;
    createSerialPort();
    m_serial->setPortName(m_config.portName);

    if (!applyConfig()) {
        destroySerialPort();
        setState(Error);
        if (m_config.autoReconnect && !m_manualClose)
            m_reconnectTimer->start(m_config.reconnectIntervalMs);
        return false;
    }

    if (m_serial->open(QIODevice::ReadWrite)) {
        m_serial->clear();
        m_receiveBuffer.clear();
        m_readBufferTimer->stop();
        setState(Connected);
        emit portConnected();
        return true;
    }

    m_lastError = m_serial->errorString();
    qWarning() << "SerialManager: Open failed:" << m_lastError;
    destroySerialPort();
    setState(Error);
    if (m_config.autoReconnect && !m_manualClose)
        m_reconnectTimer->start(m_config.reconnectIntervalMs);
    return false;
}

void SerialManager::close()
{
    m_manualClose = true;
    m_reconnectTimer->stop();
    m_readBufferTimer->stop();
    flushReceiveBuffer();
    destroySerialPort();

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
    const qint64 written = m_serial->write(data, len);
    if (written != len) {
        qWarning() << "SerialManager: Write incomplete - wrote" << written << "of" << len << "bytes";
    }
    return written;
}

void SerialManager::flush()
{
    if (m_serial && m_serial->isOpen())
        m_serial->flush();
}

void SerialManager::clearReceiveBuffer()
{
    m_receiveBuffer.clear();
    m_readBufferTimer->stop();
}

void SerialManager::flushReceiveBuffer()
{
    if (m_receiveBuffer.isEmpty())
        return;
    const QByteArray receivedData = m_receiveBuffer;
    m_receiveBuffer.clear();
    emit dataReceived(receivedData);
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
    if (!m_serial)
        return;
    const QByteArray newData = m_serial->readAll();
    if (newData.isEmpty())
        return;

    if (m_config.readBufferTimeoutMs <= 0) {
        emit dataReceived(newData);
        return;
    }

    m_receiveBuffer.append(newData);
    if (m_receiveBuffer.size() >= kMaxReceiveBufferBytes) {
        m_readBufferTimer->stop();
        flushReceiveBuffer();
        return;
    }
    m_readBufferTimer->start(m_config.readBufferTimeoutMs);
}

void SerialManager::onErrorOccurred(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError || !m_serial)
        return;

    m_lastError = m_serial->errorString();
    qWarning() << "SerialManager: Error on port" << m_config.portName << ":" << error << m_lastError;
    emit errorOccurred(error, m_lastError);

    const bool isDisconnectionError = (error == QSerialPort::ResourceError ||
                                       error == QSerialPort::DeviceNotFoundError ||
                                       error == QSerialPort::PermissionError);
    if (!isDisconnectionError || m_state != Connected)
        return;

    m_readBufferTimer->stop();
    flushReceiveBuffer();
    m_serial->close();
    setState(Disconnected);
    emit portDisconnected();

    if (m_config.autoReconnect && !m_manualClose) {
        setState(Connecting);
        destroySerialPort();
        if (!m_reconnectTimer->isActive())
            m_reconnectTimer->start(m_config.reconnectIntervalMs);
    } else {
        setState(Error);
        destroySerialPort();
    }
}

void SerialManager::onReconnectTimer()
{
    if (m_manualClose || m_state == Connected) {
        m_reconnectTimer->stop();
        return;
    }

    createSerialPort();
    m_serial->setPortName(m_config.portName);
    if (!applyConfig()) {
        destroySerialPort();
        m_reconnectTimer->start(m_config.reconnectIntervalMs);
        return;
    }

    if (m_serial->open(QIODevice::ReadWrite)) {
        m_serial->clear();
        m_receiveBuffer.clear();
        m_reconnectTimer->stop();
        setState(Connected);
        emit portConnected();
    } else {
        destroySerialPort();
        m_reconnectTimer->start(m_config.reconnectIntervalMs);
    }
}

void SerialManager::onReadBufferTimeout()
{
    flushReceiveBuffer();
}

void SerialManager::setState(ConnectionState state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit connectionStateChanged(m_state);
}

bool SerialManager::applyConfig()
{
    if (!m_serial)
        return false;

    auto fail = [this](const QString &what) {
        m_lastError = what + QStringLiteral(": ") + m_serial->errorString();
        emit errorOccurred(QSerialPort::OpenError, m_lastError);
        return false;
    };

    if (!m_serial->setBaudRate(m_config.baudRate))
        return fail(QStringLiteral("Failed to set baud rate"));
    if (!m_serial->setDataBits(m_config.dataBits))
        return fail(QStringLiteral("Failed to set data bits"));
    if (!m_serial->setParity(m_config.parity))
        return fail(QStringLiteral("Failed to set parity"));
    if (!m_serial->setStopBits(m_config.stopBits))
        return fail(QStringLiteral("Failed to set stop bits"));
    if (!m_serial->setFlowControl(m_config.flowControl))
        return fail(QStringLiteral("Failed to set flow control"));
    return true;
}

qint64 SerialManager::sendBinary(const QByteArray &data)
{
    return write(data);
}

qint64 SerialManager::sendString(const QString &text, bool appendCRLF)
{
    QByteArray data = text.toLocal8Bit();
    if (appendCRLF)
        data.append("\r\n");
    return write(data);
}

qint64 SerialManager::sendHex(const QString &hexStr)
{
    const QByteArray data = ProtocolUtils::hexStringToBytes(hexStr);
    if (data.isEmpty() && !hexStr.trimmed().isEmpty()) {
        qWarning() << "SerialManager: Invalid hex string";
        return -1;
    }
    return write(data);
}

void SerialManager::setDtr(bool enabled)
{
    if (m_serial && m_serial->isOpen())
        m_serial->setDataTerminalReady(enabled);
}

void SerialManager::setRts(bool enabled)
{
    if (m_serial && m_serial->isOpen())
        m_serial->setRequestToSend(enabled);
}
