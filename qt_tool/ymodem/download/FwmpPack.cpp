#include "FwmpPack.h"

#include "SettingsWidget.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QAbstractItemView>
#include <QBrush>
#include <QColor>
#include <QFont>
#include <QHeaderView>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QVector>
#include <QVBoxLayout>

namespace {

QByteArray fwmpHeader(const QByteArray &payload, quint8 imageType, quint8 target)  // 64 字节，和小端 Python struct 一致
{
    const QByteArray hash = QCryptographicHash::hash(payload, QCryptographicHash::Sha256);
    QByteArray header;
    QDataStream stream(&header, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("FWMP", 4);
    stream << quint8(1) << quint16(64) << quint8(0);
    stream << imageType << quint8(0) << quint8(0) << quint8(0) << quint16(0) << target << quint8(0);
    stream.writeRawData("\0\0\0\0", 4);
    stream << quint32(payload.size());
    stream.writeRawData(hash.constData(), hash.size());
    stream.writeRawData(QByteArray(8, '\0').constData(), 8);
    return header;
}

QString fileBeside(const QString &path, const QString &suffix)  // 同目录、同名，换成给定扩展名
{
    const QFileInfo info(path);
    if (info.fileName().isEmpty())
        return QString();
    return QDir::toNativeSeparators(info.dir().filePath(info.completeBaseName() + suffix));
}

QString fwmpBeside(const QString &binPath)  // 同目录、同名，扩展名改成 .fwmp
{
    const QFileInfo info(binPath);
    if (info.fileName().isEmpty())
        return QString();
    return fileBeside(binPath, QStringLiteral(".fwmp"));
}

QString imageKind(quint8 imageType)  // 0 是非安全侧，1 是安全侧，其它照数字显示
{
    if (imageType == 0)
        return QStringLiteral("NS");
    if (imageType == 1)
        return QStringLiteral("S");
    return QString::number(imageType);
}

// 签名镜像按槽补齐：S 512 KB，NS 1 MB。对不上时返回提醒文字，对得上返回空。
QString firmwareSizeWarning(quint8 imageType, qint64 actual)
{
    if (imageType == 1 && actual != qint64(512) * 1024)
        return QObject::tr("S firmware is %1 bytes, expected 512 KB").arg(actual);
    if (imageType == 0 && actual != qint64(1024) * 1024)
        return QObject::tr("NS firmware is %1 bytes, expected 1 MB").arg(actual);
    return QString();
}

} // namespace

FwmpPackDialog::FwmpPackDialog(Side side, const QString &defaultBin, QWidget *parent)
    : QDialog(parent)
    , m_side(side)
{
    m_binKey = side == Side::Secure ? QStringLiteral("fwmpSBin") : QStringLiteral("fwmpNsBin");
    m_outKey = side == Side::Secure ? QStringLiteral("fwmpSOut") : QStringLiteral("fwmpNsOut");

    auto *layout = new QVBoxLayout(this);
    auto *binRow = new QHBoxLayout();
    m_binLabel = new QLabel(this);
    m_bin = new QLineEdit(this);
    m_browseBin = new QPushButton(this);
    binRow->addWidget(m_binLabel);
    binRow->addWidget(m_bin, 1);
    binRow->addWidget(m_browseBin);
    layout->addLayout(binRow);

    auto *outRow = new QHBoxLayout();
    m_outLabel = new QLabel(this);
    m_out = new QLineEdit(this);
    m_browseOut = new QPushButton(this);
    outRow->addWidget(m_outLabel);
    outRow->addWidget(m_out, 1);
    outRow->addWidget(m_browseOut);
    layout->addLayout(outRow);

    m_pack = new QPushButton(this);
    layout->addWidget(m_pack, 0, Qt::AlignLeft);

    QSettings settings;
    settings.beginGroup(QStringLiteral("ProgramDownload"));
    const QString savedBin = settings.value(m_binKey).toString();
    const QString savedOut = settings.value(m_outKey).toString();
    settings.endGroup();
    const QString bin = savedBin.isEmpty() ? defaultBin : savedBin;
    m_bin->setText(QDir::toNativeSeparators(bin));
    if (!savedOut.isEmpty())
        m_out->setText(QDir::toNativeSeparators(savedOut));
    else
        suggestOutput(bin);

    connect(m_browseBin, &QPushButton::clicked, this, &FwmpPackDialog::chooseBin);
    connect(m_browseOut, &QPushButton::clicked, this, &FwmpPackDialog::chooseOutput);
    connect(m_bin, &QLineEdit::editingFinished, this, [this]() {
        if (m_out->text().trimmed().isEmpty())
            suggestOutput(m_bin->text());
    });
    connect(m_pack, &QPushButton::clicked, this, &FwmpPackDialog::pack);
    retranslateUi();
    resize(720, 140);
}

void FwmpPackDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QDialog::changeEvent(event);
}

void FwmpPackDialog::retranslateUi()
{
    setWindowTitle(m_side == Side::Secure ? tr("Pack S upgrade firmware")
                                           : tr("Pack NS upgrade firmware"));
    m_binLabel->setText(tr("BIN:"));
    m_browseBin->setText(tr("Select File"));
    m_outLabel->setText(tr("FWMP Output:"));
    m_browseOut->setText(tr("Select File"));
    m_pack->setText(tr("Pack FWMP"));
}

QString FwmpPackDialog::sideName() const
{
    return m_side == Side::Secure ? QStringLiteral("S") : QStringLiteral("NS");
}

void FwmpPackDialog::suggestOutput(const QString &binPath)
{
    const QString suggested = fwmpBeside(binPath.trimmed());
    if (!suggested.isEmpty())
        m_out->setText(suggested);
}

QString FwmpPackDialog::pickFile(QLineEdit *edit, const QString &title, const QString &filter,
                                 bool save, bool directory)
{
    const QFileInfo info(QDir::fromNativeSeparators(edit->text().trimmed()));
    QString dir = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    while (!dir.isEmpty() && !QDir(dir).exists()) {
        const QString parent = QFileInfo(dir).dir().absolutePath();
        if (parent == dir)
            break;
        dir = parent;
    }
    QFileDialog dialog(this, title, dir, directory ? QString() : filter);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setAcceptMode(save ? QFileDialog::AcceptSave : QFileDialog::AcceptOpen);
    dialog.setFileMode(directory ? QFileDialog::Directory
                                 : (save ? QFileDialog::AnyFile : QFileDialog::ExistingFile));
    if (directory)
        dialog.setOption(QFileDialog::ShowDirsOnly, false);
    if (save && !directory)
        dialog.setDefaultSuffix(QStringLiteral("fwmp"));
    if (!dir.isEmpty())
        dialog.setDirectory(dir);
    if (!directory && !info.isDir() && !info.fileName().isEmpty() && QDir(info.absolutePath()).exists())
        dialog.selectFile(info.fileName());
    if (dialog.exec() != QDialog::Accepted)
        return QString();
    const QStringList files = dialog.selectedFiles();
    return files.isEmpty() ? QString() : files.first();
}

void FwmpPackDialog::chooseBin()
{
    const QString path = pickFile(m_bin, tr("Select File"),
                                  tr("BIN Files (*.bin);;All Files (*)"), false);
    if (path.isEmpty())
        return;
    m_bin->setText(QDir::toNativeSeparators(path));
    suggestOutput(path);
}

void FwmpPackDialog::chooseOutput()
{
    const QString dir = pickFile(m_out, tr("Select File"), QString(), false, true);
    if (dir.isEmpty())
        return;
    const QString base = QFileInfo(QDir::fromNativeSeparators(m_bin->text().trimmed())).completeBaseName();
    const QString path = base.isEmpty()
            ? dir
            : QDir(dir).filePath(base + QStringLiteral(".fwmp"));
    m_out->setText(QDir::toNativeSeparators(path));
}

void FwmpPackDialog::pack()
{
    const QString binPath = m_bin->text().trimmed();
    const QString outPath = m_out->text().trimmed();
    const QString title = windowTitle();
    QFile input(binPath);
    if (!input.open(QIODevice::ReadOnly)) {
        const QString message = tr("Cannot read BIN: %1").arg(binPath);
        emit logged(message + QLatin1Char('\n'));
        QMessageBox::warning(this, title, message);
        return;
    }
    const QByteArray payload = input.readAll();
    input.close();
    const quint8 imageType = m_side == Side::Secure ? 1 : 0;
    const QString sizeWarning = firmwareSizeWarning(imageType, payload.size());
    if (!sizeWarning.isEmpty()) {
        emit logged(sizeWarning + QLatin1Char('\n'));
        QMessageBox::warning(this, title, sizeWarning);
    }
    const quint8 target = imageType;
    const QByteArray header = fwmpHeader(payload, imageType, target);
    if (header.size() != 64) {
        const QString message = tr("Invalid FWMP header size: %1").arg(header.size());
        emit logged(message + QLatin1Char('\n'));
        QMessageBox::warning(this, title, message);
        return;
    }
    QFileInfo outInfo(outPath);
    if (!outInfo.dir().exists() && !outInfo.dir().mkpath(QStringLiteral("."))) {
        const QString message = tr("Cannot write FWMP: %1").arg(outPath);
        emit logged(message + QLatin1Char('\n'));
        QMessageBox::warning(this, title, message);
        return;
    }
    QFile output(outPath);
    if (!output.open(QIODevice::WriteOnly)) {
        const QString message = tr("Cannot write FWMP: %1").arg(outPath);
        emit logged(message + QLatin1Char('\n'));
        QMessageBox::warning(this, title, message);
        return;
    }
    if (output.write(header) != header.size() || output.write(payload) != payload.size()) {
        output.close();
        const QString message = tr("Cannot write FWMP: %1").arg(outPath);
        emit logged(message + QLatin1Char('\n'));
        QMessageBox::warning(this, title, message);
        return;
    }
    output.close();
    const QByteArray hash = header.mid(24, 32);
    const QString kind = sideName();
    QString report;
    report += title + QLatin1Char('\n');
    report += tr("Input: %1").arg(QDir::toNativeSeparators(binPath)) + QLatin1Char('\n');
    report += tr("Output: %1").arg(QDir::toNativeSeparators(outPath)) + QLatin1Char('\n');
    report += tr("Image type: %1 (%2)").arg(imageType).arg(kind) + QLatin1Char('\n');
    report += tr("Board type: 0 (CCU)") + QLatin1Char('\n');
    report += tr("Target component: %1 (%2)").arg(target).arg(kind) + QLatin1Char('\n');
    report += tr("Payload: %1 bytes").arg(payload.size()) + QLatin1Char('\n');
    report += tr("Payload SHA-256: %1").arg(QString::fromLatin1(hash.toHex())) + QLatin1Char('\n');
    report += tr("Total: %1 bytes").arg(header.size() + payload.size()) + QLatin1Char('\n');
    emit logged(report);

    QSettings settings;
    settings.beginGroup(QStringLiteral("ProgramDownload"));
    settings.setValue(m_binKey, QDir::toNativeSeparators(binPath));
    settings.setValue(m_outKey, QDir::toNativeSeparators(outPath));
    settings.endGroup();
}

FwmpUnpackDialog::FwmpUnpackDialog(Side side, const QString &defaultFwmp, QWidget *parent)
    : QDialog(parent)
    , m_side(side)
{
    m_inKey = side == Side::Secure ? QStringLiteral("fwmpSUnpackIn") : QStringLiteral("fwmpNsUnpackIn");
    m_outKey = side == Side::Secure ? QStringLiteral("fwmpSUnpackOut") : QStringLiteral("fwmpNsUnpackOut");
    auto *layout = new QVBoxLayout(this);
    auto *fwmpRow = new QHBoxLayout();
    m_fwmpLabel = new QLabel(this);
    m_fwmp = new QLineEdit(this);
    m_browseFwmp = new QPushButton(this);
    fwmpRow->addWidget(m_fwmpLabel);
    fwmpRow->addWidget(m_fwmp, 1);
    fwmpRow->addWidget(m_browseFwmp);
    layout->addLayout(fwmpRow);

    auto *outRow = new QHBoxLayout();
    m_outLabel = new QLabel(this);
    m_out = new QLineEdit(this);
    m_browseOut = new QPushButton(this);
    outRow->addWidget(m_outLabel);
    outRow->addWidget(m_out, 1);
    outRow->addWidget(m_browseOut);
    layout->addLayout(outRow);

    m_unpack = new QPushButton(this);
    layout->addWidget(m_unpack, 0, Qt::AlignLeft);

    QSettings settings;
    settings.beginGroup(QStringLiteral("ProgramDownload"));
    QString savedFwmp = settings.value(m_inKey).toString();
    QString savedOut = settings.value(m_outKey).toString();
    if (m_side == Side::Secure && savedFwmp.isEmpty())
        savedFwmp = settings.value(QStringLiteral("fwmpUnpackIn")).toString();
    if (m_side == Side::Secure && savedOut.isEmpty())
        savedOut = settings.value(QStringLiteral("fwmpUnpackOut")).toString();
    settings.endGroup();
    const QString fwmp = savedFwmp.isEmpty() ? defaultFwmp : savedFwmp;
    m_fwmp->setText(QDir::toNativeSeparators(fwmp));
    if (!savedOut.isEmpty())
        m_out->setText(QDir::toNativeSeparators(savedOut));
    else
        suggestOutput(fwmp);

    connect(m_browseFwmp, &QPushButton::clicked, this, &FwmpUnpackDialog::chooseFwmp);
    connect(m_browseOut, &QPushButton::clicked, this, &FwmpUnpackDialog::chooseOutput);
    connect(m_fwmp, &QLineEdit::editingFinished, this, [this]() {
        if (m_out->text().trimmed().isEmpty())
            suggestOutput(m_fwmp->text());
    });
    connect(m_unpack, &QPushButton::clicked, this, &FwmpUnpackDialog::unpack);
    retranslateUi();
    resize(720, 140);
}

void FwmpUnpackDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QDialog::changeEvent(event);
}

void FwmpUnpackDialog::retranslateUi()
{
    setWindowTitle(m_side == Side::Secure ? tr("Unpack S upgrade firmware")
                                          : tr("Unpack NS upgrade firmware"));
    m_fwmpLabel->setText(tr("FWMP:"));
    m_browseFwmp->setText(tr("Select File"));
    m_outLabel->setText(tr("BIN Output:"));
    m_browseOut->setText(tr("Select File"));
    m_unpack->setText(tr("Unpack FWMP"));
}

void FwmpUnpackDialog::suggestOutput(const QString &fwmpPath)
{
    const QString suggested = fileBeside(fwmpPath.trimmed(), QStringLiteral(".bin"));
    if (!suggested.isEmpty())
        m_out->setText(suggested);
}

QString FwmpUnpackDialog::pickFile(QLineEdit *edit, const QString &title, const QString &filter,
                                   bool save, const QString &suffix)
{
    const QFileInfo info(QDir::fromNativeSeparators(edit->text().trimmed()));
    QString dir = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    while (!dir.isEmpty() && !QDir(dir).exists()) {
        const QString parent = QFileInfo(dir).dir().absolutePath();
        if (parent == dir)
            break;
        dir = parent;
    }
    QFileDialog dialog(this, title, dir, filter);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setAcceptMode(save ? QFileDialog::AcceptSave : QFileDialog::AcceptOpen);
    dialog.setFileMode(save ? QFileDialog::AnyFile : QFileDialog::ExistingFile);
    if (save && !suffix.isEmpty())
        dialog.setDefaultSuffix(suffix);
    if (!dir.isEmpty())
        dialog.setDirectory(dir);
    if (!info.isDir() && !info.fileName().isEmpty() && QDir(info.absolutePath()).exists())
        dialog.selectFile(info.fileName());
    if (dialog.exec() != QDialog::Accepted)
        return QString();
    const QStringList files = dialog.selectedFiles();
    return files.isEmpty() ? QString() : files.first();
}

void FwmpUnpackDialog::chooseFwmp()
{
    const QString path = pickFile(m_fwmp, tr("Select File"),
                                  tr("FWMP Files (*.fwmp);;All Files (*)"), false, QString());
    if (path.isEmpty())
        return;
    m_fwmp->setText(QDir::toNativeSeparators(path));
    suggestOutput(path);
}

void FwmpUnpackDialog::chooseOutput()
{
    const QString path = pickFile(m_out, tr("Select File"),
                                  tr("BIN Files (*.bin);;All Files (*)"), true, QStringLiteral("bin"));
    if (!path.isEmpty())
        m_out->setText(QDir::toNativeSeparators(path));
}

bool readFwmpPayload(const QString &path, FwmpPayload *payload, QString *error)
{
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("Cannot read FWMP: %1").arg(path);
        return false;
    }
    const QByteArray file = input.readAll();
    input.close();
    if (file.size() < 64 || !file.startsWith("FWMP")) {
        if (error)
            *error = QObject::tr("Not an FWMP file: %1").arg(path);
        return false;
    }
    QDataStream stream(file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.skipRawData(4);
    quint8 version = 0;
    quint16 headerSize = 0;
    quint8 ignored = 0;
    stream >> version >> headerSize >> ignored;
    if (stream.status() != QDataStream::Ok || version != 1 || headerSize != 64) {
        if (error)
            *error = QObject::tr("Invalid FWMP header size: %1").arg(headerSize);
        return false;
    }
    quint8 imageType = 0;
    quint8 boardType = 0;
    quint8 reservedByte = 0;
    quint16 reservedWord = 0;
    quint8 target = 0;
    stream >> imageType >> boardType >> reservedByte >> reservedByte >> reservedWord >> target >> reservedByte;
    Q_UNUSED(boardType)
    Q_UNUSED(reservedWord)
    Q_UNUSED(target)
    stream.skipRawData(4);
    quint32 payloadSize = 0;
    stream >> payloadSize;
    QByteArray storedHash(32, 0);
    if (stream.readRawData(storedHash.data(), storedHash.size()) != storedHash.size()) {
        if (error)
            *error = QObject::tr("Not an FWMP file: %1").arg(path);
        return false;
    }
    if (quint64(headerSize) + payloadSize != quint64(file.size())) {
        if (error)
            *error = QObject::tr("FWMP length does not match the file: %1").arg(path);
        return false;
    }
    const QByteArray bytes = file.mid(headerSize, int(payloadSize));
    if (QCryptographicHash::hash(bytes, QCryptographicHash::Sha256) != storedHash) {
        if (error)
            *error = QObject::tr("FWMP SHA-256 does not match");
        return false;
    }
    if (!payload)
        return false;
    payload->bytes = bytes;
    payload->imageType = imageType;
    return true;
}

void FwmpUnpackDialog::reportFailure(const QString &message)
{
    emit logged(message + QLatin1Char('\n'));
    QMessageBox::warning(this, windowTitle(), message);
}

void FwmpUnpackDialog::unpack()
{
    const QString fwmpPath = m_fwmp->text().trimmed();
    const QString outPath = m_out->text().trimmed();
    QFile input(fwmpPath);
    if (!input.open(QIODevice::ReadOnly)) {
        reportFailure(tr("Cannot read FWMP: %1").arg(fwmpPath));
        return;
    }
    const QByteArray file = input.readAll();
    input.close();
    if (file.size() < 64 || !file.startsWith("FWMP")) {
        reportFailure(tr("Not an FWMP file: %1").arg(fwmpPath));
        return;
    }
    QDataStream stream(file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.skipRawData(4);
    quint8 version = 0;
    quint16 headerSize = 0;
    quint8 ignored = 0;
    stream >> version >> headerSize >> ignored;
    if (stream.status() != QDataStream::Ok || version != 1 || headerSize != 64) {
        reportFailure(tr("Invalid FWMP header size: %1").arg(headerSize));
        return;
    }
    quint8 imageType = 0;
    quint8 boardType = 0;
    quint8 reservedByte = 0;
    quint16 reservedWord = 0;
    quint8 target = 0;
    stream >> imageType >> boardType >> reservedByte >> reservedByte >> reservedWord >> target >> reservedByte;
    stream.skipRawData(4);
    quint32 payloadSize = 0;
    stream >> payloadSize;
    QByteArray storedHash(32, 0);
    if (stream.readRawData(storedHash.data(), storedHash.size()) != storedHash.size()) {
        reportFailure(tr("Not an FWMP file: %1").arg(fwmpPath));
        return;
    }
    if (quint64(headerSize) + payloadSize != quint64(file.size())) {
        reportFailure(tr("FWMP length does not match the file: %1").arg(fwmpPath));
        return;
    }
    const QByteArray payload = file.mid(headerSize, int(payloadSize));
    const QByteArray actualHash = QCryptographicHash::hash(payload, QCryptographicHash::Sha256);
    if (actualHash != storedHash) {
        reportFailure(tr("FWMP SHA-256 does not match"));
        return;
    }
    const quint8 expectedType = m_side == Side::Secure ? 1 : 0;
    if (imageType != expectedType) {
        const QString message = tr("This FWMP is %1, this dialog unpacks %2")
                                    .arg(imageKind(imageType), imageKind(expectedType));
        emit logged(message + QLatin1Char('\n'));
        QMessageBox::warning(this, windowTitle(), message);
    }
    const QString sizeWarning = firmwareSizeWarning(imageType, payload.size());
    if (!sizeWarning.isEmpty()) {
        emit logged(sizeWarning + QLatin1Char('\n'));
        QMessageBox::warning(this, windowTitle(), sizeWarning);
    }
    QFileInfo outInfo(outPath);
    if (!outInfo.dir().exists() && !outInfo.dir().mkpath(QStringLiteral("."))) {
        reportFailure(tr("Cannot write BIN: %1").arg(outPath));
        return;
    }
    QFile output(outPath);
    if (!output.open(QIODevice::WriteOnly) || output.write(payload) != payload.size()) {
        reportFailure(tr("Cannot write BIN: %1").arg(outPath));
        return;
    }
    output.close();
    const QString kind = imageKind(imageType);
    QString report;
    report += windowTitle() + QLatin1Char('\n');
    report += tr("Input: %1").arg(QDir::toNativeSeparators(fwmpPath)) + QLatin1Char('\n');
    report += tr("Output: %1").arg(QDir::toNativeSeparators(outPath)) + QLatin1Char('\n');
    report += tr("Image type: %1 (%2)").arg(imageType).arg(kind) + QLatin1Char('\n');
    report += boardType == 0 ? tr("Board type: 0 (CCU)")
                             : tr("Board type: %1").arg(boardType);
    report += QLatin1Char('\n');
    report += tr("Target component: %1 (%2)").arg(target).arg(imageKind(target)) + QLatin1Char('\n');
    report += tr("Payload: %1 bytes").arg(payload.size()) + QLatin1Char('\n');
    report += tr("Payload SHA-256: %1").arg(QString::fromLatin1(actualHash.toHex())) + QLatin1Char('\n');
    report += tr("Total: %1 bytes").arg(file.size()) + QLatin1Char('\n');
    emit logged(report);

    QSettings settings;
    settings.beginGroup(QStringLiteral("ProgramDownload"));
    settings.setValue(m_inKey, QDir::toNativeSeparators(fwmpPath));
    settings.setValue(m_outKey, QDir::toNativeSeparators(outPath));
    settings.endGroup();
}

namespace {

const quint32 kImageMagic = 0x96f3b83d;
const quint16 kTlvInfoMagic = 0x6907;
const quint16 kTlvProtMagic = 0x6908;
const quint16 kTlvKeyHash = 0x01;
const quint16 kTlvPubKey = 0x02;
const quint16 kTlvSha256 = 0x10;
const quint16 kTlvEcdsa = 0x22;
const quint16 kTlvSecCnt = 0x50;

struct TlvRecord {
    quint16 type = 0;
    QByteArray data;
};

struct ImageInfo {
    bool magicOk = false;
    QString version;
    quint16 headerSize = 0;
    quint32 imageSize = 0;
    bool hasSecCnt = false;
    quint32 secCnt = 0;
    QByteArray sha256;
    QByteArray keyHash;
    QByteArray publicKey;  // TLV 0x02。没有 KEYHASH 时，用它的 SHA-256 做对比
    bool hasEcdsa = false;
};

struct PackageReport {
    QString title;
    QString path;
    QString packState;
    QString packVersion;
    QString image;
    QString payloadLength;
    QString packHash;
    QString signState;
    QString mcuVersion;
    QString headerSize;
    QString imageSize;
    QString secCnt;
    QString sha256;
    QString keyHash;
    QString ecdsa;
};

QString blankValue()
{
    return QStringLiteral("\u2014");
}

bool readU8(const QByteArray &bytes, int offset, quint8 *out)
{
    if (offset < 0 || offset >= bytes.size())
        return false;
    *out = quint8(bytes.at(offset));
    return true;
}

bool readU16(const QByteArray &bytes, int offset, quint16 *out)
{
    quint8 lo = 0;
    quint8 hi = 0;
    if (!readU8(bytes, offset, &lo) || !readU8(bytes, offset + 1, &hi))
        return false;
    *out = quint16(lo) | (quint16(hi) << 8);
    return true;
}

bool readU32(const QByteArray &bytes, int offset, quint32 *out)
{
    quint16 lo = 0;
    quint16 hi = 0;
    if (!readU16(bytes, offset, &lo) || !readU16(bytes, offset + 2, &hi))
        return false;
    *out = quint32(lo) | (quint32(hi) << 16);
    return true;
}

int hexNibble(char ch)
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    return -1;
}

struct HexChunk {
    quint32 addr = 0;
    QByteArray data;
};

bool collectIntelHex(const QByteArray &text, QVector<HexChunk> *chunks, quint32 *minAddr, quint32 *maxAddr)
{
    quint32 upper = 0;
    bool any = false;
    quint32 lowest = 0;
    quint32 highest = 0;
    const QList<QByteArray> lines = text.split('\n');
    for (QByteArray line : lines) {
        line = line.trimmed();
        if (line.isEmpty() || line.at(0) != ':')
            continue;
        QByteArray raw;
        unsigned sum = 0;
        for (int i = 1; i + 1 < line.size(); i += 2) {
            const int hi = hexNibble(line.at(i));
            const int lo = hexNibble(line.at(i + 1));
            if (hi < 0 || lo < 0)
                return false;
            const quint8 byte = quint8((hi << 4) | lo);
            raw.append(char(byte));
            sum += byte;
        }
        if (raw.size() < 5 || (sum & 0xff) != 0)
            return false;
        const int count = quint8(raw.at(0));
        if (raw.size() != count + 5)
            return false;
        const quint16 addr16 = (quint16(quint8(raw.at(1))) << 8) | quint8(raw.at(2));
        const quint8 type = quint8(raw.at(3));
        const QByteArray data = raw.mid(4, count);
        if (type == 0x00) {
            if (data.isEmpty())
                continue;
            const quint32 start = upper + addr16;
            const quint32 end = start + quint32(data.size() - 1);
            if (!any || start < lowest)
                lowest = start;
            if (!any || end > highest)
                highest = end;
            any = true;
            HexChunk chunk;
            chunk.addr = start;
            chunk.data = data;
            chunks->append(chunk);
        } else if (type == 0x01) {
            break;
        } else if (type == 0x02) {
            if (data.size() != 2)
                return false;
            upper = (quint32(quint8(data.at(0))) << 8 | quint8(data.at(1))) << 4;
        } else if (type == 0x04) {
            if (data.size() != 2)
                return false;
            upper = (quint32(quint8(data.at(0))) << 8 | quint8(data.at(1))) << 16;
        } else if (type == 0x03 || type == 0x05) {
            continue;
        } else {
            return false;
        }
    }
    if (!any)
        return false;
    *minAddr = lowest;
    *maxAddr = highest;
    return true;
}

bool decodeIntelHex(const QByteArray &text, QByteArray *out)  // 内存里收成 bin，不写文件
{
    QVector<HexChunk> chunks;
    quint32 minAddr = 0;
    quint32 maxAddr = 0;
    if (!collectIntelHex(text, &chunks, &minAddr, &maxAddr))
        return false;
    const quint64 span = quint64(maxAddr) - minAddr + 1;
    if (span == 0 || span > quint64(16) * 1024 * 1024)
        return false;
    QByteArray binary(int(span), char(0xff));
    for (const HexChunk &chunk : chunks) {
        const int at = int(chunk.addr - minAddr);
        if (at < 0 || at + chunk.data.size() > binary.size())
            return false;
        binary.replace(at, chunk.data.size(), chunk.data);
    }
    *out = binary;
    return true;
}

bool readTlvs(const QByteArray &bytes, int offset, quint16 expectMagic, QVector<TlvRecord> *records, int *next)
{
    quint16 magic = 0;
    quint16 total = 0;
    if (!readU16(bytes, offset, &magic) || magic != expectMagic)
        return false;
    if (!readU16(bytes, offset + 2, &total) || total < 4)
        return false;
    if (quint64(offset) + total > quint64(bytes.size()))
        return false;
    const int end = offset + int(total);
    int cursor = offset + 4;
    while (cursor < end) {
        quint16 type = 0;
        quint16 length = 0;
        if (cursor + 4 > end || !readU16(bytes, cursor, &type) || !readU16(bytes, cursor + 2, &length))
            return false;
        cursor += 4;
        if (cursor + int(length) > end)
            return false;
        TlvRecord record;
        record.type = type;
        record.data = bytes.mid(cursor, length);
        records->append(record);
        cursor += int(length);
    }
    *next = end;
    return true;
}

ImageInfo parseMcuBoot(const QByteArray &bytes)
{
    ImageInfo info;
    quint32 magic = 0;
    quint32 loadAddr = 0;
    quint16 headerSize = 0;
    quint16 protectedSize = 0;
    quint32 imageSize = 0;
    quint32 flags = 0;
    quint8 major = 0;
    quint8 minor = 0;
    quint16 revision = 0;
    quint32 build = 0;
    if (!readU32(bytes, 0, &magic) || magic != kImageMagic)
        return info;
    if (!readU32(bytes, 4, &loadAddr) || !readU16(bytes, 8, &headerSize) || !readU16(bytes, 10, &protectedSize)
        || !readU32(bytes, 12, &imageSize) || !readU32(bytes, 16, &flags)
        || !readU8(bytes, 20, &major) || !readU8(bytes, 21, &minor)
        || !readU16(bytes, 22, &revision) || !readU32(bytes, 24, &build))
        return info;
    Q_UNUSED(loadAddr)
    Q_UNUSED(protectedSize)
    Q_UNUSED(flags)
    if (headerSize < 32)
        return info;
    info.magicOk = true;
    info.version = QStringLiteral("%1.%2.%3+%4").arg(major).arg(minor).arg(revision).arg(build);
    info.headerSize = headerSize;
    info.imageSize = imageSize;

    const quint64 tlvOffset = quint64(headerSize) + imageSize;
    if (tlvOffset > quint64(bytes.size()))
        return info;
    int offset = int(tlvOffset);
    quint16 areaMagic = 0;
    if (!readU16(bytes, offset, &areaMagic))
        return info;
    if (areaMagic == kTlvProtMagic) {
        QVector<TlvRecord> protectedRecords;
        int next = offset;
        if (!readTlvs(bytes, offset, kTlvProtMagic, &protectedRecords, &next))
            return info;
        for (const TlvRecord &record : protectedRecords) {
            if (record.type == kTlvSecCnt && record.data.size() >= 1 && record.data.size() <= 4) {
                quint32 value = 0;
                for (int i = 0; i < record.data.size(); ++i)
                    value |= quint32(quint8(record.data.at(i))) << (8 * i);
                info.hasSecCnt = true;
                info.secCnt = value;
            }
        }
        offset = next;
        if (!readU16(bytes, offset, &areaMagic))
            return info;
    }
    if (areaMagic != kTlvInfoMagic)
        return info;
    QVector<TlvRecord> records;
    int next = offset;
    if (!readTlvs(bytes, offset, kTlvInfoMagic, &records, &next))
        return info;
    for (const TlvRecord &record : records) {
        if (record.type == kTlvSha256)
            info.sha256 = record.data;
        else if (record.type == kTlvKeyHash)
            info.keyHash = record.data;
        else if (record.type == kTlvPubKey)
            info.publicKey = record.data;
        else if (record.type == kTlvEcdsa)
            info.hasEcdsa = true;
    }
    return info;
}

QByteArray loadImageBytes(const QString &path, const QByteArray &file)  // hex 先转成 bin，后面的对比都用这份
{
    const bool hexName = path.endsWith(QLatin1String(".hex"), Qt::CaseInsensitive);
    const QByteArray trimmed = file.trimmed();
    const bool looksHex = !trimmed.isEmpty() && trimmed.at(0) == ':';
    if (!hexName && !looksHex)
        return file;
    QByteArray decoded;
    if (!decodeIntelHex(file, &decoded))
        return QByteArray();
    return decoded;
}

PackageReport inspectOne(const QString &path)  // 只以 ReadOnly 打开
{
    PackageReport report;
    report.path = path;
    report.title = QFileInfo(path).fileName();
    const QString blank = blankValue();
    report.packState = QObject::tr("Not packed");
    report.packVersion = blank;
    report.image = blank;
    report.payloadLength = blank;
    report.packHash = blank;
    report.signState = QObject::tr("Not signed");
    report.mcuVersion = blank;
    report.headerSize = blank;
    report.imageSize = blank;
    report.secCnt = blank;
    report.sha256 = blank;
    report.keyHash = blank;
    report.ecdsa = QObject::tr("ECDSA absent");

    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) {
        report.packState = QObject::tr("Cannot read");
        return report;
    }
    const QByteArray file = input.readAll();
    input.close();
    const QByteArray bytes = loadImageBytes(path, file);

    QByteArray image;
    if (bytes.size() >= 4 && bytes.startsWith("FWMP")) {
        report.packState = QObject::tr("Packed");
        quint8 version = 0;
        quint16 headerSize = 0;
        quint8 imageType = 0;
        quint32 payloadSize = 0;
        const bool headerRead = bytes.size() >= 64
            && readU8(bytes, 4, &version)
            && readU16(bytes, 5, &headerSize)
            && readU8(bytes, 8, &imageType)
            && readU32(bytes, 20, &payloadSize);
        if (headerRead) {
            report.packVersion = QString::number(version);
            report.image = imageKind(imageType);
            report.payloadLength = QString::number(payloadSize);
        }
        const QByteArray storedHash = bytes.mid(24, 32);
        const bool lengthOk = headerRead && version == 1 && headerSize == 64 && storedHash.size() == 32
            && quint64(headerSize) + payloadSize == quint64(bytes.size());
        if (!lengthOk) {
            report.packState = QObject::tr("Invalid header");
        } else {
            image = bytes.mid(int(headerSize), int(payloadSize));
            const bool hashOk = QCryptographicHash::hash(image, QCryptographicHash::Sha256) == storedHash;
            report.packHash = hashOk ? QObject::tr("SHA-256 matches") : QObject::tr("SHA-256 does not match");
        }
    } else {
        image = bytes;
    }

    const ImageInfo info = parseMcuBoot(image);
    if (!info.magicOk)
        return report;
    report.mcuVersion = info.version;
    report.headerSize = QString::number(info.headerSize);
    report.imageSize = QString::number(info.imageSize);
    if (info.hasSecCnt)
        report.secCnt = QString::number(info.secCnt);
    if (!info.sha256.isEmpty())
        report.sha256 = QString::fromLatin1(info.sha256.toHex());
    if (!info.keyHash.isEmpty())
        report.keyHash = QString::fromLatin1(info.keyHash.toHex());
    else if (!info.publicKey.isEmpty())
        report.keyHash = QString::fromLatin1(QCryptographicHash::hash(info.publicKey, QCryptographicHash::Sha256).toHex());
    report.ecdsa = info.hasEcdsa ? QObject::tr("ECDSA present") : QObject::tr("ECDSA absent");
    report.signState = info.hasEcdsa ? QObject::tr("Signed") : QObject::tr("Not signed");
    return report;
}

enum CompareRow {
    RowPackSection,
    RowPackState,
    RowPackVersion,
    RowImage,
    RowPayload,
    RowPackHash,
    RowSignSection,
    RowSignState,
    RowMcuVersion,
    RowHeaderSize,
    RowImageSize,
    RowSecCnt,
    RowSha256,
    RowKeyHash,
    RowEcdsa,
    RowCount
};

bool compareRowIsSection(int row)
{
    return row == RowPackSection || row == RowSignSection;
}

QString compareRowName(int row)
{
    switch (row) {
    case RowPackSection: return QObject::tr("Package information");
    case RowPackState: return QObject::tr("Pack status");
    case RowPackVersion: return QObject::tr("Pack version");
    case RowImage: return QObject::tr("Image side");
    case RowPayload: return QObject::tr("Payload length");
    case RowPackHash: return QObject::tr("SHA-256");
    case RowSignSection: return QObject::tr("Signature information");
    case RowSignState: return QObject::tr("Signature");
    case RowMcuVersion: return QObject::tr("MCUboot version");
    case RowHeaderSize: return QObject::tr("Header size");
    case RowImageSize: return QObject::tr("Image size");
    case RowSecCnt: return QObject::tr("Security counter");
    case RowSha256: return QObject::tr("Image SHA-256");
    case RowKeyHash: return QObject::tr("Public key hash");
    case RowEcdsa: return QObject::tr("ECDSA signature");
    default: return QString();
    }
}

QString compareRowValue(const PackageReport &report, int row)
{
    switch (row) {
    case RowPackState: return report.packState;
    case RowPackVersion: return report.packVersion;
    case RowImage: return report.image;
    case RowPayload: return report.payloadLength;
    case RowPackHash: return report.packHash;
    case RowSignState: return report.signState;
    case RowMcuVersion: return report.mcuVersion;
    case RowHeaderSize: return report.headerSize;
    case RowImageSize: return report.imageSize;
    case RowSecCnt: return report.secCnt;
    case RowSha256: return report.sha256;
    case RowKeyHash: return report.keyHash;
    case RowEcdsa: return report.ecdsa;
    default: return QString();
    }
}

QString columnTitle(const QString &path, const QStringList &paths)
{
    const QFileInfo info(path);
    int same = 0;
    for (const QString &other : paths) {
        if (QFileInfo(other).fileName().compare(info.fileName(), Qt::CaseInsensitive) == 0)
            ++same;
    }
    if (same > 1)
        return info.dir().dirName() + QLatin1Char('/') + info.fileName();
    return info.fileName();
}

} // namespace

PackageInspectDialog::PackageInspectDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowFlags(Qt::Window
                   | Qt::WindowTitleHint
                   | Qt::WindowSystemMenuHint
                   | Qt::WindowMinimizeButtonHint
                   | Qt::WindowMaximizeButtonHint
                   | Qt::WindowCloseButtonHint);
    resize(980, 640);
    auto *layout = new QVBoxLayout(this);
    auto *buttons = new QHBoxLayout();
    m_add = new QPushButton(this);
    m_remove = new QPushButton(this);
    m_compare = new QPushButton(this);
    buttons->addWidget(m_add);
    buttons->addWidget(m_remove);
    buttons->addWidget(m_compare);
    buttons->addStretch();
    layout->addLayout(buttons);

    m_files = new QListWidget(this);
    m_files->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_files->setMaximumHeight(140);
    layout->addWidget(m_files);

    m_table = new QTableWidget(this);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setWordWrap(false);
    m_table->setTextElideMode(Qt::ElideNone);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    layout->addWidget(m_table, 1);

    m_note = new QLabel(this);
    m_note->setWordWrap(true);
    layout->addWidget(m_note);

    connect(m_add, &QPushButton::clicked, this, &PackageInspectDialog::addFiles);
    connect(m_remove, &QPushButton::clicked, this, &PackageInspectDialog::removeFiles);
    connect(m_compare, &QPushButton::clicked, this, &PackageInspectDialog::compare);
    retranslateUi();
}

void PackageInspectDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QDialog::changeEvent(event);
}

void PackageInspectDialog::retranslateUi()
{
    setWindowTitle(tr("Inspect package"));
    m_add->setText(tr("Add files"));
    m_remove->setText(tr("Remove selected"));
    m_compare->setText(tr("Update"));
    if (m_table->columnCount() > 0)
        compare();
    else
        m_note->setText(tr("Read only. The file was not modified."));
}

void PackageInspectDialog::addFiles()
{
    QString start = QFileInfo(SettingsWidget::firmwareFilePath(QStringLiteral("firmware.bin"))).absolutePath();
    if (m_files->count() > 0) {
        const QString last = m_files->item(m_files->count() - 1)->text();
        const QString dir = QFileInfo(last).absolutePath();
        if (!dir.isEmpty())
            start = dir;
    }
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Inspect package"), start,
        tr("Firmware (*.bin *.fwmp *.hex);;All files (*)"));
    bool added = false;
    for (const QString &file : files) {
        const QString native = QDir::toNativeSeparators(file);
        bool exists = false;
        for (int i = 0; i < m_files->count(); ++i) {
            if (m_files->item(i)->text().compare(native, Qt::CaseInsensitive) == 0) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            m_files->addItem(native);
            added = true;
        }
    }
    if (added)
        compare();
}

void PackageInspectDialog::removeFiles()
{
    const QList<QListWidgetItem *> selected = m_files->selectedItems();
    if (selected.isEmpty())
        return;
    for (QListWidgetItem *item : selected)
        delete item;
    compare();
}

void PackageInspectDialog::compare()
{
    QStringList paths;
    for (int i = 0; i < m_files->count(); ++i)
        paths << m_files->item(i)->text();
    if (paths.isEmpty()) {
        m_table->clear();
        m_table->setRowCount(0);
        m_table->setColumnCount(0);
        m_note->setText(tr("Add a firmware file first.") + QLatin1Char('\n')
                        + tr("Read only. The file was not modified."));
        return;
    }

    QVector<PackageReport> reports;
    reports.reserve(paths.size());
    for (const QString &path : paths)
        reports.append(inspectOne(path));

    m_table->clear();
    m_table->setColumnCount(reports.size());
    m_table->setRowCount(RowCount);
    for (int column = 0; column < reports.size(); ++column) {
        auto *header = new QTableWidgetItem(columnTitle(reports.at(column).path, paths));
        header->setToolTip(QDir::toNativeSeparators(reports.at(column).path));
        m_table->setHorizontalHeaderItem(column, header);
    }
    const QColor sectionColor(236, 239, 241);
    const QColor diffColor(255, 224, 130);
    for (int row = 0; row < RowCount; ++row) {
        auto *name = new QTableWidgetItem(compareRowName(row));
        if (compareRowIsSection(row)) {
            QFont font = name->font();
            font.setBold(true);
            name->setFont(font);
        }
        m_table->setVerticalHeaderItem(row, name);
        bool differ = false;
        if (!compareRowIsSection(row) && reports.size() > 1) {
            const QString first = compareRowValue(reports.at(0), row);
            for (int column = 1; column < reports.size(); ++column) {
                if (compareRowValue(reports.at(column), row) != first) {
                    differ = true;
                    break;
                }
            }
        }
        for (int column = 0; column < reports.size(); ++column) {
            auto *cell = new QTableWidgetItem(compareRowIsSection(row) ? QString() : compareRowValue(reports.at(column), row));
            cell->setFlags(Qt::ItemIsEnabled);
            if (compareRowIsSection(row))
                cell->setBackground(QBrush(sectionColor));
            else if (differ)
                cell->setBackground(QBrush(diffColor));
            m_table->setItem(row, column, cell);
        }
    }
    m_note->setText(tr("Read only. The file was not modified."));
}
