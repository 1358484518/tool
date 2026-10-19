#include "DaObk.h"

#include <QCryptographicHash>
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

const quint32 kDestAddr = 0x0FFD0100;  // HDPL1 里 DA 配置的地址，和现有 DA_Config.obk 一致
const quint32 kDataSize = 0x60;        // 32 字节完整性哈希 + 64 字节载荷
const int kPermWidth = 16;             // 权限掩码宽度。权限只占低 4 字节
const int kReservedWidth = 16;         // 载荷末尾保留区

void appendU32(QByteArray *out, quint32 value)  // 小端写入 4 字节，和 Python struct.pack("<I") 相同
{
    out->append(static_cast<char>(value & 0xFF));
    out->append(static_cast<char>((value >> 8) & 0xFF));
    out->append(static_cast<char>((value >> 16) & 0xFF));
    out->append(static_cast<char>((value >> 24) & 0xFF));
}

QByteArray pemBody(const QByteArray &text)  // 去掉 PEM 头尾，只留 Base64
{
    QByteArray body;
    const QList<QByteArray> lines = text.split('\n');
    for (QByteArray line : lines) {
        line = line.trimmed();
        if (line.endsWith('\r'))
            line.chop(1);
        if (line.isEmpty() || line.startsWith("-----"))
            continue;
        body += line;
    }
    return body;
}

QByteArray uncompressedPoint(const QByteArray &der)  // 取出 0x04 || X || Y，共 65 字节
{
    const QByteArray mark = QByteArray::fromHex("03420004");
    const int at = der.indexOf(mark);
    QByteArray point;
    if (at >= 0)
        point = der.mid(at + 3, 65);
    else {
        const int fallback = der.lastIndexOf('\x04');
        if (fallback >= 0)
            point = der.mid(fallback, 65);
    }
    if (point.size() != 65 || static_cast<unsigned char>(point.at(0)) != 0x04)
        return QByteArray();
    return point;
}

} // namespace

QByteArray DaObk::fromPublicKey(const QString &pemPath, quint32 permission, quint32 encrypt, Error *error)
{
    if (error)
        *error = Error::Ok;
    QFile file(pemPath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = Error::Read;
        return QByteArray();
    }
    const QByteArray body = pemBody(file.readAll());
    if (body.isEmpty()) {
        if (error)
            *error = Error::Pem;
        return QByteArray();
    }
    const QByteArray point = uncompressedPoint(QByteArray::fromBase64(body));
    if (point.isEmpty()) {
        if (error)
            *error = Error::Point;
        return QByteArray();
    }
    // 丢掉未压缩点开头的 0x04，对 X||Y 做 SHA-256。芯片按这个结果核对根公钥。
    const QByteArray pubHash = QCryptographicHash::hash(point.mid(1), QCryptographicHash::Sha256);
    QByteArray perm;
    appendU32(&perm, permission);
    perm.append(QByteArray(kPermWidth - 4, '\0'));
    const QByteArray payload = pubHash + perm + QByteArray(kReservedWidth, '\0');
    const QByteArray integrity = QCryptographicHash::hash(payload, QCryptographicHash::Sha256);
    QByteArray blob;
    appendU32(&blob, kDestAddr);
    appendU32(&blob, kDataSize);
    appendU32(&blob, encrypt);
    blob += integrity;
    blob += payload;
    return blob;
}

DaObkDialog::DaObkDialog(const QString &defaultKey, const QString &defaultOut, QWidget *parent)
    : QDialog(parent)
{
    auto *layout = new QVBoxLayout(this);
    auto *keyRow = new QHBoxLayout();
    m_keyLabel = new QLabel(this);
    m_key = new QLineEdit(this);
    m_browseKey = new QPushButton(this);
    keyRow->addWidget(m_keyLabel);
    keyRow->addWidget(m_key, 1);
    keyRow->addWidget(m_browseKey);
    layout->addLayout(keyRow);

    auto *outRow = new QHBoxLayout();
    m_outLabel = new QLabel(this);
    m_out = new QLineEdit(this);
    m_browseOut = new QPushButton(this);
    outRow->addWidget(m_outLabel);
    outRow->addWidget(m_out, 1);
    outRow->addWidget(m_browseOut);
    layout->addLayout(outRow);

    auto *optRow = new QHBoxLayout();
    m_permLabel = new QLabel(this);
    m_perm = new QLineEdit(this);
    m_encLabel = new QLabel(this);
    m_enc = new QLineEdit(this);
    m_enc->setMaximumWidth(80);
    optRow->addWidget(m_permLabel);
    optRow->addWidget(m_perm, 1);
    optRow->addWidget(m_encLabel);
    optRow->addWidget(m_enc);
    layout->addLayout(optRow);

    m_generate = new QPushButton(this);
    layout->addWidget(m_generate, 0, Qt::AlignLeft);

    QSettings settings;
    settings.beginGroup(QStringLiteral("ProgramDownload"));
    const QString key = settings.value(QStringLiteral("daObkKey")).toString();
    const QString out = settings.value(QStringLiteral("daObkOut")).toString();
    m_key->setText(key.isEmpty() ? defaultKey : key);
    m_out->setText(out.isEmpty() ? defaultOut : out);
    m_perm->setText(settings.value(QStringLiteral("daObkPermission"), QStringLiteral("0x00005077")).toString());
    m_enc->setText(settings.value(QStringLiteral("daObkEncrypt"), QStringLiteral("1")).toString());
    settings.endGroup();

    connect(m_browseKey, &QPushButton::clicked, this, &DaObkDialog::chooseKey);
    connect(m_browseOut, &QPushButton::clicked, this, &DaObkDialog::chooseOutput);
    connect(m_generate, &QPushButton::clicked, this, &DaObkDialog::generate);
    retranslateUi();
    resize(640, 160);
}

void DaObkDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QDialog::changeEvent(event);
}

void DaObkDialog::retranslateUi()
{
    setWindowTitle(tr("Generate DA OBK"));
    m_keyLabel->setText(tr("Root Public Key:"));
    m_browseKey->setText(tr("Change Path"));
    m_outLabel->setText(tr("OBK Output:"));
    m_browseOut->setText(tr("Change Path"));
    m_permLabel->setText(tr("Permission:"));
    m_encLabel->setText(tr("Encrypt:"));
    m_generate->setText(tr("Generate"));
}

void DaObkDialog::chooseKey()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Change Path"), m_key->text(),
        tr("PEM Files (*.pem);;All Files (*)"));
    if (!path.isEmpty())
        m_key->setText(QDir::toNativeSeparators(path));
}

void DaObkDialog::chooseOutput()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Change Path"), m_out->text(),
        tr("OBK Files (*.obk);;All Files (*)"));
    if (!path.isEmpty())
        m_out->setText(QDir::toNativeSeparators(path));
}

void DaObkDialog::generate()
{
    bool permOk = false;
    bool encOk = false;
    const quint32 permission = m_perm->text().trimmed().toUInt(&permOk, 0);
    const quint32 encrypt = m_enc->text().trimmed().toUInt(&encOk, 0);
    if (!permOk) {
        QMessageBox::warning(this, tr("Generate DA OBK"), tr("Invalid permission."));
        return;
    }
    if (!encOk) {
        QMessageBox::warning(this, tr("Generate DA OBK"), tr("Invalid encrypt flag."));
        return;
    }
    const QString keyPath = m_key->text().trimmed();
    DaObk::Error error = DaObk::Error::Ok;
    const QByteArray blob = DaObk::fromPublicKey(keyPath, permission, encrypt, &error);
    if (blob.isEmpty()) {
        QString message;
        if (error == DaObk::Error::Read)
            message = tr("Cannot read key: %1").arg(keyPath);
        else if (error == DaObk::Error::Pem)
            message = tr("PEM has no Base64: %1").arg(keyPath);
        else
            message = tr("Not a P-256 uncompressed public key");
        QMessageBox::warning(this, tr("Generate DA OBK"), message);
        return;
    }
    const QString outPath = m_out->text().trimmed();
    QFile out(outPath);
    if (!out.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("Generate DA OBK"), tr("Cannot write OBK: %1").arg(outPath));
        return;
    }
    if (out.write(blob) != blob.size()) {
        QMessageBox::warning(this, tr("Generate DA OBK"), tr("Cannot write OBK: %1").arg(outPath));
        return;
    }
    out.close();
    QSettings settings;
    settings.beginGroup(QStringLiteral("ProgramDownload"));
    settings.setValue(QStringLiteral("daObkKey"), m_key->text().trimmed());
    settings.setValue(QStringLiteral("daObkOut"), outPath);
    settings.setValue(QStringLiteral("daObkPermission"), m_perm->text().trimmed());
    settings.setValue(QStringLiteral("daObkEncrypt"), m_enc->text().trimmed());
    settings.endGroup();
    QMessageBox::information(this, tr("Generate DA OBK"),
                             tr("OBK written: %1 (%2 bytes)").arg(outPath).arg(blob.size()));
}
