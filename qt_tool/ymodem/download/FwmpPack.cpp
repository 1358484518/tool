#include "FwmpPack.h"

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
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
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
