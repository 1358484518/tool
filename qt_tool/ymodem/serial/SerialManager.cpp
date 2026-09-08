#include "SerialManager.h"
#include "common/ProtocolUtils.h"

#include <QDebug>
#include <QMetaObject>

static const int kMaxReceiveBufferBytes = 256 * 1024;

SerialManager::SerialManager(QObject *parent)
    : IoSource(parent)
{
    qRegisterMetaType<SerialManager::SerialConfig>("SerialManager::SerialConfig");
    qRegisterMetaType<SerialManager::SerialConfig>("SerialConfig");
    qRegisterMetaType<SerialManager::ConnectionState>("SerialManager::ConnectionState");
    qRegisterMetaType<QSerialPort::SerialPortError>("QSerialPort::SerialPortError");
}

SerialManager::SerialManager(const SerialConfig &config, QObject *parent)
    : SerialManager(parent)
{
    m_config = config;
}

SerialManager::~SerialManager()
{
    m_manualClose = true;
    if (m_reconnectTimer)
        m_reconnectTimer->stop();
    if (m_readBufferTimer)
        m_readBufferTimer->stop();
    destroySerialPort();
}

void SerialManager::initWorker()
{
    if (m_reconnectTimer)
        return;

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &SerialManager::onReconnectTimer);

    m_readBufferTimer = new QTimer(this);
    m_readBufferTimer->setSingleShot(true);
    connect(m_readBufferTimer, &QTimer::timeout, this, &SerialManager::onReadBufferTimeout);
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
    QSerialPort *port = m_serial;
    m_serial = nullptr;
    port->disconnect();
    if (port->isOpen())
        port->close();
    port->deleteLater();
}

bool SerialManager::open()
{
    initWorker();

    if (m_state == Connected || m_state == Connecting) {
        qWarning() << "SerialManager: Port is already open or connecting";
        return true;
    }
    if (m_config.portName.isEmpty()) {
        m_lastError = QStringLiteral("Port name is empty");
        emit errorOccurred(static_cast<int>(QSerialPort::NotOpenError), m_lastError);
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
    initWorker();
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
    if (m_state != Connected || !m_serial || !m_serial->isOpen()) {
        qWarning() << "SerialManager: Cannot write - port not connected";
        return -1;
    }
    const qint64 written = m_serial->write(data);
    if (written != data.size()) {
        qWarning() << "SerialManager: Write incomplete - wrote" << written << "of" << data.size() << "bytes";
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
    if (m_readBufferTimer)
        m_readBufferTimer->stop();
}

void SerialManager::emitReceived(const QByteArray &data)
{
    if (data.isEmpty())
        return;
    emit dataReceived(data);
    emitIoData(IoPacket::fromSerial(data, m_config.portName));
}

void SerialManager::flushReceiveBuffer()
{
    if (m_receiveBuffer.isEmpty())
        return;
    const QByteArray receivedData = m_receiveBuffer;
    m_receiveBuffer.clear();
    emitReceived(receivedData);
}

QString SerialManager::lastError() const
{
    return m_lastError;
}

void SerialManager::onReadyRead()
{
    if (!m_serial)
        return;
    const QByteArray newData = m_serial->readAll();
    if (newData.isEmpty())
        return;

    if (m_config.readBufferTimeoutMs <= 0) {
        emitReceived(newData);
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
    if (error == QSerialPort::NoError)
        return;
    const QString errStr = m_serial ? m_serial->errorString() : m_lastError;
    // Windows 上在 QSerialPort 的 errorOccurred 里 close/delete 会直接把进程打崩。
    QMetaObject::invokeMethod(this, "handlePortError", Qt::QueuedConnection,
                              Q_ARG(int, static_cast<int>(error)),
                              Q_ARG(QString, errStr));
}

void SerialManager::handlePortError(int error, const QString &errorString)
{
    if (m_manualClose)
        return;

    m_lastError = errorString;
    qWarning() << "SerialManager: Error on port" << m_config.portName << ":" << error << m_lastError;
    emit errorOccurred(error, m_lastError);

    if (m_state != Connected)
        return;

    if (m_readBufferTimer)
        m_readBufferTimer->stop();
    flushReceiveBuffer();
    setState(Disconnected);
    emit portDisconnected();
    destroySerialPort();

    if (m_config.autoReconnect && !m_manualClose) {
        setState(Connecting);
        if (m_reconnectTimer && !m_reconnectTimer->isActive())
            m_reconnectTimer->start(m_config.reconnectIntervalMs);
    } else {
        setState(Error);
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
        emit errorOccurred(static_cast<int>(QSerialPort::OpenError), m_lastError);
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
    sendIoData(IoPacket::fromSerial(data, m_config.portName));
    return (m_state == Connected) ? data.size() : -1;
}

bool SerialManager::writeIoData(const IoPacket &packet)
{
    return write(packet.data) >= 0;
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
