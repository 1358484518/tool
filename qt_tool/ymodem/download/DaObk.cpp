#include "DaObk.h"

#include <QCryptographicHash>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QSettings>
#include <QStyle>
#include <QStyleOption>
#include <QVBoxLayout>

/**
 * 权限表里的竖排文字。逆时针转 90 度，从下往上读，和 TPC 的格子一样。
 */
class VerticalText : public QLabel
{
public:
    explicit VerticalText(QWidget *parent = nullptr);  // 格子里的竖排标签

    QSize sizeHint() const override;           // 旋转后宽高对调
    QSize minimumSizeHint() const override;    // 跟 sizeHint 一样，避免按横排文字把列撑宽

protected:
    void paintEvent(QPaintEvent *event) override;  // 先画表格底和边，再转文字
};

VerticalText::VerticalText(QWidget *parent)
    : QLabel(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setAlignment(Qt::AlignCenter);
}

QSize VerticalText::sizeHint() const
{
    const QFontMetrics metrics(font());
    const QRect bounds = metrics.boundingRect(text());
    return QSize(bounds.height() + 8, qMax(bounds.width() + 10, 72));
}

QSize VerticalText::minimumSizeHint() const
{
    return sizeHint();
}

void VerticalText::paintEvent(QPaintEvent *)
{
    QStyleOption option;
    option.initFrom(this);
    QPainter painter(this);
    style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setPen(option.palette.color(QPalette::WindowText));
    painter.translate(width() / 2.0, height() / 2.0);
    painter.rotate(-90);
    painter.drawText(QRectF(-height() / 2.0, -width() / 2.0, height(), width()),
                     Qt::AlignCenter, text());
}

namespace {

const quint32 kDestAddr = 0x0FFD0100;  // HDPL1 里 DA 配置的地址，和现有 DA_Config.obk 一致
const quint32 kDataSize = 0x60;        // 32 字节完整性哈希 + 64 字节载荷
const int kPermWidth = 16;             // 权限掩码宽度。权限只占低 4 字节
const int kReservedWidth = 16;         // 载荷末尾保留区
// AN6008 4.1.4：H5 只用 bit14、bit12、bit6–4、bit2–0。全开就是 ST 默认 0x00005077。
const quint32 kFunctionalMask = 0x00005077;

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
    m_permValue = new QLabel(this);
    m_permValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont valueFont = m_permValue->font();
    valueFont.setBold(true);
    m_permValue->setFont(valueFont);
    m_encLabel = new QLabel(this);
    optRow->addWidget(m_permLabel);
    optRow->addWidget(m_permValue);
    optRow->addStretch(1);
    optRow->addWidget(m_encLabel);
    layout->addLayout(optRow);

    auto *table = new QFrame(this);
    table->setObjectName(QStringLiteral("permTable"));
    table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *grid = new QGridLayout(table);
    grid->setSpacing(0);
    grid->setContentsMargins(0, 0, 0, 0);
    m_descHeader = new QLabel(table);
    m_descHeader->setAlignment(Qt::AlignCenter);
    m_descHeader->setProperty("role", "header");
    m_maskHeader = new QLabel(table);
    m_maskHeader->setAlignment(Qt::AlignCenter);
    m_maskHeader->setProperty("role", "header");
    m_coreLabel = new VerticalText(table);
    m_coreLabel->setProperty("role", "vtext");
    grid->addWidget(m_descHeader, 0, 0);
    grid->addWidget(m_maskHeader, 0, 1, 1, 16);
    // 左侧竖排名称跨过位号、分组、名称和按钮，和 TPC 那一列一样。
    grid->addWidget(m_coreLabel, 1, 0, 4, 1);
    grid->setColumnMinimumWidth(0, 88);
    for (int group = 0; group < 4; ++group) {
        m_groupLabel[group] = new QLabel(table);
        m_groupLabel[group]->setAlignment(Qt::AlignCenter);
        m_groupLabel[group]->setProperty("role", "group");
        grid->addWidget(m_groupLabel[group], 2, 1 + group * 4, 1, 4);
    }
    for (int bit = 15; bit >= 0; --bit) {
        const int col = 16 - bit;
        auto *index = new QLabel(QString::number(bit), table);
        index->setAlignment(Qt::AlignCenter);
        grid->addWidget(index, 1, col);
        grid->setColumnStretch(col, 1);
        grid->setColumnMinimumWidth(col, 40);

        m_bitName[bit] = new VerticalText(table);
        m_bitName[bit]->setProperty("role", "vtext");
        grid->addWidget(m_bitName[bit], 3, col);

        m_bit[bit] = new QPushButton(table);
        // 保留位不能点，避免写出 AN6008 没有定义的权限。
        const bool functional = (kFunctionalMask & (quint32(1) << bit)) != 0;
        m_bit[bit]->setCheckable(functional);
        m_bit[bit]->setEnabled(functional);
        m_bit[bit]->setText(QStringLiteral("0"));
        m_bit[bit]->setMinimumHeight(26);
        m_bit[bit]->setFocusPolicy(Qt::NoFocus);
        m_bit[bit]->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        if (functional)
            m_bit[bit]->setCursor(Qt::PointingHandCursor);
        grid->addWidget(m_bit[bit], 4, col);
        if (functional)
            connect(m_bit[bit], &QPushButton::toggled, this, &DaObkDialog::onBitToggled);
    }
    table->setStyleSheet(QStringLiteral(
        "QFrame#permTable { border-top: 1px solid #7a7a7a; border-left: 1px solid #7a7a7a; background: white; }"
        "QFrame#permTable QLabel { border-right: 1px solid #7a7a7a; border-bottom: 1px solid #7a7a7a;"
        " background: white; padding: 2px; }"
        "QFrame#permTable QLabel[role=\"header\"] { padding: 4px; }"
        "QFrame#permTable QLabel[role=\"group\"] { background: #d7e6f5; font-weight: bold; padding: 4px; }"
        "QFrame#permTable QLabel[role=\"vtext\"] { padding: 0px; }"
        "QFrame#permTable QPushButton { border-right: 1px solid #7a7a7a; border-bottom: 1px solid #7a7a7a;"
        " border-top: 0; border-left: 0; border-radius: 0; background: white; color: #222; font-weight: bold; padding: 3px 0; }"
        "QFrame#permTable QPushButton:checked { background: #1a365d; color: white; }"
        "QFrame#permTable QPushButton:disabled { background: #c8c8c8; color: #333; }"
        "QFrame#permTable QPushButton:hover { background: #e7eef6; }"
        "QFrame#permTable QPushButton:checked:hover { background: #1a365d; }"
        "QFrame#permTable QPushButton:disabled:hover { background: #c8c8c8; }"));
    layout->addWidget(table);

    m_generate = new QPushButton(this);
    layout->addWidget(m_generate, 0, Qt::AlignLeft);

    QSettings settings;
    settings.beginGroup(QStringLiteral("ProgramDownload"));
    const QString key = settings.value(QStringLiteral("daObkKey")).toString();
    const QString out = settings.value(QStringLiteral("daObkOut")).toString();
    m_key->setText(key.isEmpty() ? defaultKey : key);
    m_out->setText(out.isEmpty() ? defaultOut : out);
    bool permOk = false;
    const quint32 savedPerm = settings.value(QStringLiteral("daObkPermission"),
                                              QStringLiteral("0x00005077")).toString().toUInt(&permOk, 0);
    settings.endGroup();

    connect(m_browseKey, &QPushButton::clicked, this, &DaObkDialog::chooseKey);
    connect(m_browseOut, &QPushButton::clicked, this, &DaObkDialog::chooseOutput);
    connect(m_generate, &QPushButton::clicked, this, &DaObkDialog::generate);
    retranslateUi();
    setPermission(permOk ? savedPerm : kFunctionalMask);
    resize(960, 460);
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
    m_encLabel->setText(tr("Encrypt: 1"));
    m_descHeader->setText(tr("Description"));
    m_maskHeader->setText(tr("Permissions Mask"));
    m_coreLabel->setText(QStringLiteral("AP - M33"));
    m_groupLabel[0]->setText(tr("Regression"));
    m_groupLabel[1]->setText(tr("Non Intrusive Debug"));
    m_groupLabel[2]->setText(tr("Secure Debug"));
    m_groupLabel[3]->setText(tr("Non Secure Debug"));
    for (int bit = 0; bit < 16; ++bit) {
        QString caption;
        switch (bit) {
        case 14: caption = tr("Full"); break;
        case 13: caption = tr("> To ROT"); break;
        case 12: caption = tr("To TZ"); break;
        case 6:
        case 2: caption = tr("Level 3"); break;
        case 5:
        case 1: caption = tr("Level 2"); break;
        case 4:
        case 0: caption = tr("Level 1"); break;
        default: caption = tr("Reserved"); break;
        }
        m_bitName[bit]->setText(caption);
    }
    m_generate->setText(tr("Generate"));
}

void DaObkDialog::setPermission(quint32 value)
{
    value &= kFunctionalMask;
    for (int bit = 0; bit < 16; ++bit) {
        QPushButton *button = m_bit[bit];
        if (!button->isEnabled())
            continue;
        const bool on = (value & (quint32(1) << bit)) != 0;
        button->blockSignals(true);
        button->setChecked(on);
        button->setText(on ? QStringLiteral("1") : QStringLiteral("0"));
        button->blockSignals(false);
    }
    refreshPermissionText();
}

void DaObkDialog::refreshPermissionText()
{
    m_permValue->setText(QStringLiteral("0x%1").arg(permission(), 8, 16, QLatin1Char('0')));
}

quint32 DaObkDialog::permission() const
{
    quint32 value = 0;
    for (int bit = 0; bit < 16; ++bit) {
        if (m_bit[bit]->isChecked())
            value |= quint32(1) << bit;
    }
    return value;
}

void DaObkDialog::onBitToggled(bool checked)
{
    if (auto *button = qobject_cast<QPushButton *>(sender()))
        button->setText(checked ? QStringLiteral("1") : QStringLiteral("0"));
    refreshPermissionText();
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
    const quint32 permissionBits = permission();
    // H573 和 H5F4 都是加密型号。AN6008 表 13：烧录时由芯片加密，标记固定为 1。
    const quint32 encrypt = 1;
    const QString keyPath = m_key->text().trimmed();
    DaObk::Error error = DaObk::Error::Ok;
    const QByteArray blob = DaObk::fromPublicKey(keyPath, permissionBits, encrypt, &error);
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
    settings.setValue(QStringLiteral("daObkPermission"), m_permValue->text());
    settings.endGroup();
    QMessageBox::information(this, tr("Generate DA OBK"),
                             tr("OBK written: %1 (%2 bytes)").arg(outPath).arg(blob.size()));
}
