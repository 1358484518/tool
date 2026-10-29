#include "TftpClient.h"

#include "TftpProtocol.h"

#include <QFileInfo>
#include <QHostInfo>

namespace TftpQt {

class DallySocket : public QObject {
public:
    // 挂在 TftpClient 上，套接字留在工作线程。不能挂到 QCoreApplication，那是界面线程。
    DallySocket(QObject *parent, QUdpSocket *udp, const QByteArray &ackPacket, const QHostAddress &remote,
                quint16 remotePort, int holdMs, quint16 dataBlock)
        : QObject(parent)
        , socket(udp)
        , ackPacket(ackPacket)
        , remote(remote)
        , remotePort(remotePort)
        , dataBlock(dataBlock)
        , timer(this)
    {
        socket->setParent(this);
        connect(socket, &QUdpSocket::readyRead, this, [this]() { readPending(); });
        timer.setSingleShot(true);
        connect(&timer, &QTimer::timeout, this, [this]() { deleteLater(); });
        timer.start(holdMs);
    }

private:
    void readPending()
    {
        while (socket->hasPendingDatagrams()) {
            const qint64 pending = socket->pendingDatagramSize();
            if (pending <= 0)
                break;
            QByteArray datagram;
            datagram.resize(int(pending));
            QHostAddress sender;
            quint16 senderPort = 0;
            const qint64 got = socket->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);
            if (got < 0)
                break;
            datagram.resize(int(got));
            if (!samePeer(sender, remote) || senderPort != remotePort)
                continue;
            const Packet packet = decodePacket(datagram);
            if (packet.valid && packet.opcode == Opcode::Data && packet.block == dataBlock)
                socket->writeDatagram(ackPacket, remote, remotePort);
        }
    }

    QUdpSocket *socket;
    QByteArray ackPacket;
    QHostAddress remote;
    quint16 remotePort = 0;
    quint16 dataBlock = 0;
    QTimer timer;
};

TftpClient::TftpClient(QObject *parent)
    : QObject(parent)
    , timer(this)
{
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, this, &TftpClient::onTimeout);
}

bool TftpClient::isActive() const
{
    return active;
}

int TftpClient::negotiatedBlockSize() const
{
    return blockSize;
}

int TftpClient::negotiatedTimeout() const
{
    return timeoutSec;
}

qint64 TftpClient::announcedSize() const
{
    return totalSize;
}

QString TftpClient::validate(const Request &candidate) const
{
    if (candidate.host.trimmed().isEmpty())
        return QString::fromUtf8(u8"请填写服务器地址");
    if (candidate.port == 0)
        return QString::fromUtf8(u8"端口无效");
    if (candidate.remoteFile.isEmpty() || candidate.remoteFile.contains(QLatin1Char('\0')))
        return QString::fromUtf8(u8"请填写远端文件名");
    if (candidate.localFile.isEmpty())
        return QString::fromUtf8(u8"请填写本地文件");
    if (candidate.timeoutSec < 1 || candidate.timeoutSec > 255)
        return QString::fromUtf8(u8"超时需要在 1 到 255 秒之间");
    if (candidate.maxRetries < 0 || candidate.maxRetries > 30)
        return QString::fromUtf8(u8"重传次数需要在 0 到 30 之间");
    if (candidate.blockSize != 0
        && (candidate.blockSize < MinBlockSize || candidate.blockSize > MaxBlockSize)) {
        return QString::fromUtf8(u8"块大小需要在 8 到 65464 之间");
    }
    if (!candidate.download && !QFileInfo(candidate.localFile).isFile())
        return QString::fromUtf8(u8"本地文件不存在");
    return {};
}

void TftpClient::start(const Request &next)
{
    if (active) {
        emit logMessage(QString::fromUtf8(u8"当前已有传输"));
        return;
    }

    const QString error = validate(next);
    if (!error.isEmpty()) {
        emit finished(false, error);
        return;
    }

    request = next;
    request.host = request.host.trimmed();
    active = true;
    discardLocalOnFail = false;
    waitingAck0 = false;
    sentData = false;
    shortSent = false;
    peerPortKnown = false;
    timeoutSec = request.timeoutSec;
    maxRetries = request.maxRetries;
    attempt = 0;
    blockSize = DefaultBlockSize;
    txCount = 0;
    rxCount = 0;
    peerPort = request.port;
    lastBlock = 0;
    expectBlock = 1;
    bytesTransferred = 0;
    totalSize = request.download ? -1 : QFileInfo(request.localFile).size();
    lastPacket.clear();
    phase = Phase::Resolving;
    elapsed.start();

    emit logMessage(QString::fromUtf8(request.download ? u8"准备下载 %1" : u8"准备上传 %1")
                        .arg(request.remoteFile));
    lookupId = QHostInfo::lookupHost(request.host, this, SLOT(onLookup(QHostInfo)));
}

void TftpClient::cancel()
{
    if (!active)
        return;
    if (lookupId >= 0) {
        QHostInfo::abortHostLookup(lookupId);
        lookupId = -1;
    }
    if (socket && phase != Phase::Resolving)
        socket->writeDatagram(encodeError(quint16(ErrorCode::NotDefined), "cancelled"), peer, peerPort);
    fail(QString::fromUtf8(u8"已取消"));
}

void TftpClient::onLookup(const QHostInfo &info)
{
    if (info.lookupId() != lookupId)
        return;
    lookupId = -1;
    if (!active)
        return;

    if (info.error() != QHostInfo::NoError) {
        fail(QString::fromUtf8(u8"解析地址失败：%1").arg(info.errorString()));
        return;
    }

    for (const QHostAddress &address : info.addresses()) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol) {
            peer = address;
            beginTransfer();
            return;
        }
    }
    fail(QString::fromUtf8(u8"没有可用的 IPv4 地址"));
}

void TftpClient::beginTransfer()
{
    if (request.download) {
        file.setFileName(request.localFile);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            fail(QString::fromUtf8(u8"无法写入本地文件：%1").arg(file.errorString()));
            return;
        }
        discardLocalOnFail = true;
    } else {
        file.setFileName(request.localFile);
        if (!file.open(QIODevice::ReadOnly)) {
            fail(QString::fromUtf8(u8"无法读取本地文件：%1").arg(file.errorString()));
            return;
        }
        totalSize = file.size();
    }

    socket = new QUdpSocket(this);
    connect(socket, &QUdpSocket::readyRead, this, &TftpClient::onReadyRead);
    if (!socket->bind(QHostAddress(QHostAddress::AnyIPv4), quint16(0))) {
        fail(QString::fromUtf8(u8"无法绑定本地端口：%1").arg(socket->errorString()));
        return;
    }

    QVector<Option> options;
    if (request.blockSize > 0)
        options.append({QByteArrayLiteral("blksize"), QByteArray::number(request.blockSize)});
    if (request.timeoutOption)
        options.append({QByteArrayLiteral("timeout"), QByteArray::number(timeoutSec)});
    if (request.transferSizeOption) {
        const qint64 advertised = request.download ? 0 : totalSize;
        options.append({QByteArrayLiteral("tsize"), QByteArray::number(advertised)});
    }

    const QByteArray requestPacket = encodeRequest(
        request.download ? Opcode::Rrq : Opcode::Wrq,
        request.remoteFile.toUtf8(),
        QByteArrayLiteral("octet"),
        options);
    phase = Phase::WaitFirst;
    peerPortKnown = false;
    peerPort = request.port;
    reportProgress();
    if (!transmit(requestPacket))
        return;
    emit logMessage(QString::fromUtf8(u8"已向 %1:%2 发送%3")
                        .arg(peer.toString())
                        .arg(request.port)
                        .arg(request.download ? QStringLiteral(" RRQ") : QStringLiteral(" WRQ")));
}

void TftpClient::onReadyRead()
{
    QUdpSocket *udp = socket;
    if (!udp)
        return;
    while (udp->hasPendingDatagrams()) {
        const qint64 pending = udp->pendingDatagramSize();
        if (pending <= 0)
            break;
        QByteArray datagram;
        datagram.resize(int(pending));
        QHostAddress sender;
        quint16 senderPort = 0;
        const qint64 got = udp->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);
        if (got < 0)
            break;
        datagram.resize(int(got));
        handlePacket(datagram, sender, senderPort);
        if (!active || socket != udp)
            return;
    }
}

void TftpClient::handlePacket(const QByteArray &datagram, const QHostAddress &sender, quint16 senderPort)
{
    if (!samePeer(sender, peer))
        return;

    if (peerPortKnown && senderPort != peerPort) {
        socket->writeDatagram(
            encodeError(quint16(ErrorCode::UnknownTransferId), "unknown transfer id"),
            sender,
            senderPort);
        ++txCount;
        return;
    }

    const Packet packet = decodePacket(datagram);
    if (!packet.valid) {
        emit logMessage(QString::fromUtf8(u8"忽略无法解析的数据包"));
        return;
    }
    ++rxCount;

    if (packet.opcode == Opcode::Error) {
        const QString detail = packet.errorMessage.isEmpty()
            ? errorCodeName(packet.errorCode)
            : QString::fromUtf8(u8"%1：%2").arg(errorCodeName(packet.errorCode), QString::fromUtf8(packet.errorMessage));
        fail(detail);
        return;
    }

    switch (phase) {
    case Phase::WaitFirst:
        handleFirst(packet, senderPort);
        break;
    case Phase::Recv:
    case Phase::Dally:
        handleReceive(packet);
        break;
    case Phase::Send:
        handleSend(packet);
        break;
    case Phase::Idle:
    case Phase::Resolving:
        break;
    }
}

void TftpClient::handleFirst(const Packet &packet, quint16 senderPort)
{
    peerPort = senderPort;
    peerPortKnown = true;

    if (request.download) {
        if (packet.opcode == Opcode::Oack) {
            QString error;
            if (!applyOack(packet, &error)) {
                socket->writeDatagram(encodeError(quint16(ErrorCode::OptionRefused), error.toUtf8()), peer, peerPort);
                fail(error);
                return;
            }
            phase = Phase::Recv;
            expectBlock = 1;
            attempt = 0;
            if (!transmit(encodeAck(0)))
                return;
            emit logMessage(QString::fromUtf8(u8"选项已协商，块大小 %1，超时 %2 秒")
                                .arg(blockSize)
                                .arg(timeoutSec));
            reportProgress();
            return;
        }
        if (packet.opcode == Opcode::Data && packet.block == 1) {
            if (request.blockSize > 0 || request.timeoutOption || request.transferSizeOption)
                emit logMessage(QString::fromUtf8(u8"服务器未接受选项，使用 512 字节块"));
            phase = Phase::Recv;
            expectBlock = 1;
            handleReceive(packet);
            return;
        }
    } else if (packet.opcode == Opcode::Oack) {
        QString error;
        if (!applyOack(packet, &error)) {
            socket->writeDatagram(encodeError(quint16(ErrorCode::OptionRefused), error.toUtf8()), peer, peerPort);
            fail(error);
            return;
        }
        phase = Phase::Send;
        emit logMessage(QString::fromUtf8(u8"选项已协商，块大小 %1，超时 %2 秒")
                            .arg(blockSize)
                            .arg(timeoutSec));
        sendNextData();
        return;
    } else if (packet.opcode == Opcode::Ack && packet.block == 0) {
        if (request.blockSize > 0 || request.timeoutOption || request.transferSizeOption)
            emit logMessage(QString::fromUtf8(u8"服务器未接受选项，使用 512 字节块"));
        phase = Phase::Send;
        sendNextData();
        return;
    }

    socket->writeDatagram(encodeError(quint16(ErrorCode::IllegalOperation), "unexpected packet"), peer, peerPort);
    fail(QString::fromUtf8(u8"收到意外的首包"));
}

bool TftpClient::applyOack(const Packet &packet, QString *errorMessage)
{
    bool sawBlockSize = false;
    bool sawTimeout = false;
    bool sawSize = false;
    for (const Option &option : packet.options) {
        if (optionNameIs(option.name, "blksize")) {
            if (request.blockSize <= 0 || sawBlockSize) {
                *errorMessage = QString::fromUtf8(u8"拒绝未请求的 blksize");
                return false;
            }
            bool ok = false;
            const int value = option.value.toInt(&ok);
            if (!ok || value < MinBlockSize || value > request.blockSize) {
                *errorMessage = QString::fromUtf8(u8"blksize 无效");
                return false;
            }
            blockSize = value;
            sawBlockSize = true;
        } else if (optionNameIs(option.name, "timeout")) {
            if (!request.timeoutOption || sawTimeout) {
                *errorMessage = QString::fromUtf8(u8"拒绝未请求的 timeout");
                return false;
            }
            bool ok = false;
            const int value = option.value.toInt(&ok);
            if (!ok || value < 1 || value > 255) {
                *errorMessage = QString::fromUtf8(u8"timeout 无效");
                return false;
            }
            timeoutSec = value;
            sawTimeout = true;
        } else if (optionNameIs(option.name, "tsize")) {
            if (!request.transferSizeOption || sawSize) {
                *errorMessage = QString::fromUtf8(u8"拒绝未请求的 tsize");
                return false;
            }
            bool ok = false;
            const qint64 value = option.value.toLongLong(&ok);
            if (!ok || value < 0) {
                *errorMessage = QString::fromUtf8(u8"tsize 无效");
                return false;
            }
            totalSize = value;
            sawSize = true;
        } else {
            *errorMessage = QString::fromUtf8(u8"拒绝未知选项");
            return false;
        }
    }
    return true;
}

void TftpClient::handleReceive(const Packet &packet)
{
    if (phase == Phase::Dally) {
        if (packet.opcode == Opcode::Data && packet.block == lastBlock) {
            attempt = 0;
            transmit(lastPacket);
        }
        return;
    }

    if (packet.opcode == Opcode::Oack && bytesTransferred == 0 && expectBlock == 1) {
        attempt = 0;
        transmit(lastPacket);
        return;
    }
    if (packet.opcode != Opcode::Data)
        return;

    if (packet.block != expectBlock) {
        if (expectBlock != 1 && packet.block == quint16(expectBlock - 1)) {
            attempt = 0;
            transmit(lastPacket);
        }
        return;
    }
    if (packet.data.size() > blockSize) {
        socket->writeDatagram(encodeError(quint16(ErrorCode::IllegalOperation), "block too large"), peer, peerPort);
        fail(QString::fromUtf8(u8"数据块超过协商大小"));
        return;
    }
    if (file.write(packet.data) != packet.data.size() || !file.flush()) {
        socket->writeDatagram(encodeError(quint16(ErrorCode::DiskFull), "disk full"), peer, peerPort);
        fail(QString::fromUtf8(u8"写入本地文件失败"));
        return;
    }

    bytesTransferred += packet.data.size();
    lastBlock = packet.block;
    const bool finishedBlock = packet.data.size() < blockSize;
    if (!finishedBlock)
        expectBlock = quint16(expectBlock + 1);
    attempt = 0;
    reportProgress();
    if (!transmit(encodeAck(packet.block)))
        return;
    if (finishedBlock)
        enterDally();
}

void TftpClient::handleSend(const Packet &packet)
{
    if (packet.opcode == Opcode::Oack && sentData) {
        attempt = 0;
        transmit(lastPacket);
        return;
    }
    if (packet.opcode != Opcode::Ack)
        return;

    if (waitingAck0) {
        if (packet.block == 0) {
            waitingAck0 = false;
            sendNextData();
        }
        return;
    }

    if (packet.block == lastBlock) {
        if (shortSent)
            succeed(QString::fromUtf8(u8"上传完成"));
        else
            sendNextData();
        return;
    }
    if (packet.block == quint16(lastBlock - 1)) {
        attempt = 0;
        transmit(lastPacket);
    }
}

void TftpClient::sendNextData()
{
    QByteArray chunk;
    chunk.resize(blockSize);
    const qint64 read = file.read(chunk.data(), blockSize);
    if (read < 0) {
        socket->writeDatagram(encodeError(quint16(ErrorCode::NotDefined), "read error"), peer, peerPort);
        fail(QString::fromUtf8(u8"读取本地文件失败"));
        return;
    }
    chunk.resize(int(read));
    lastBlock = quint16(lastBlock + 1);
    shortSent = chunk.size() < blockSize;
    sentData = true;
    waitingAck0 = false;
    phase = Phase::Send;
    bytesTransferred += chunk.size();
    attempt = 0;
    reportProgress();
    transmit(encodeData(lastBlock, chunk));
}

void TftpClient::enterDally()
{
    phase = Phase::Dally;
    discardLocalOnFail = false;
    file.flush();
    file.close();
    timer.stop();
    if (socket) {
        socket->disconnect(this);
        new DallySocket(this, socket, lastPacket, peer, peerPort, qMax(1, timeoutSec) * 1000, lastBlock);
        socket = nullptr;
    }
    succeed(QString::fromUtf8(u8"下载完成"));
}

void TftpClient::onTimeout()
{
    if (!active)
        return;
    if (phase == Phase::Dally) {
        succeed(QString::fromUtf8(u8"下载完成"));
        return;
    }
    if (attempt >= maxRetries) {
        if (socket)
            socket->writeDatagram(encodeError(quint16(ErrorCode::NotDefined), "timeout"), peer, peerPort);
        fail(QString::fromUtf8(u8"超时，已重传 %1 次").arg(maxRetries));
        return;
    }
    ++attempt;
    emit logMessage(QString::fromUtf8(u8"超时，第 %1 次重传").arg(attempt));
    transmit(lastPacket);
}

bool TftpClient::transmit(const QByteArray &datagram)
{
    if (!active || !socket)
        return false;
    lastPacket = datagram;
    const qint64 sent = socket->writeDatagram(datagram, peer, peerPort);
    if (sent != datagram.size()) {
        fail(QString::fromUtf8(u8"发送失败：%1").arg(socket->errorString()));
        return false;
    }
    ++txCount;
    if (phase != Phase::Dally)
        timer.start(timeoutSec * 1000);
    return true;
}

void TftpClient::reportProgress()
{
    emit progress(bytesTransferred, totalSize);
}

void TftpClient::succeed(const QString &message)
{
    if (!active)
        return;
    active = false;
    phase = Phase::Idle;
    timer.stop();
    discardLocalOnFail = false;
    if (file.isOpen())
        file.close();
    if (socket) {
        socket->disconnect(this);
        socket->close();
        socket->deleteLater();
        socket = nullptr;
    }
    emit progress(bytesTransferred, totalSize < 0 ? bytesTransferred : totalSize);
    emit finished(true, QString::fromUtf8(u8"%1，%2 字节，用时 %3 ms，发送 %4 包，接收 %5 包")
                             .arg(message)
                             .arg(bytesTransferred)
                             .arg(elapsed.elapsed())
                             .arg(txCount)
                             .arg(rxCount));
}

void TftpClient::fail(const QString &message)
{
    if (!active)
        return;
    active = false;
    phase = Phase::Idle;
    timer.stop();
    if (file.isOpen())
        file.close();
    if (discardLocalOnFail)
        QFile::remove(request.localFile);
    discardLocalOnFail = false;
    if (socket) {
        socket->disconnect(this);
        socket->close();
        socket->deleteLater();
        socket = nullptr;
    }
    emit finished(false, message);
}

} // namespace TftpQt
