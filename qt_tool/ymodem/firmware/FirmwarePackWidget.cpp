#include "FirmwarePackWidget.h"
#include "SettingsWidget.h"

#include <QComboBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTextCursor>

/**
 * 固件校验和升级包规则。对照 debug 里 FWGenerator 对 ASHS-32-V1.01.bin 的 11 组输出。
 * 换项目不会改变校验文件，只改变升级头第 3 字节。CRC 只覆盖固件原文，不算头。
 *
 * CRC-16/MODBUS：
 * 多项式 0x8005，按位反射后每次右移用 0xA001。初值 0xFFFF，算完不再异或。
 * 样本长度 388366，即 0x0005ED0E，CRC 为 0x88EF。
 *
 * 校验文件，文件名是去掉末尾 .bin 后再接校验后缀。共 10 字节，不含固件正文，字段都是小端：
 * 偏移 0   uint16  固定 0xABCD，字节 CD AB
 * 偏移 2   uint16  固定 0x1234，字节 34 12
 * 偏移 4   uint32  固件字节数，样本为 0E ED 05 00
 * 偏移 8   uint16  CRC16，样本为 EF 88
 *
 * 升级文件，文件名是去掉末尾 .bin 后再接固件后缀。先写 10 字节头，再原样接上固件：
 * 偏移 0   uint16  固定 0xFFFF，字节 FF FF
 * 偏移 2   uint8   项目号，取值见下面每一项
 * 偏移 3   uint8   固定 0x01，11 个项目都是这个值
 * 偏移 4   uint32  固件字节数，大端，样本为 00 05 ED 0E
 * 偏移 8   uint16  CRC16，大端，样本为 88 EF
 * 偏移 10        固件原始字节，一个不改
 *
 * 输出写在源文件同一目录。校验文件和升级文件都先去掉原名末尾的 .bin，再接各自的后缀。
 * 默认时 ASHS-32-V1.01.bin 得到 ASHS-32-V1.01-CHECK.bin 和 ASHS-32-V1.01-UPDATE.bin。
 * 项目号就是下拉框下标，直接写入升级头偏移 2。
 */
namespace {

const int kTextBytes = 1024 * 1024;  // 打包信息最多留 1MB，按 QString 的 UTF-16 存储算

QString keepNewestText(const QString &text)  // 超过 1MB 时丢掉前面的旧内容，尽量从整行切开
{
    const int maxChars = kTextBytes / static_cast<int>(sizeof(QChar));
    if (text.size() <= maxChars)
        return text;
    int cut = text.size() - maxChars;
    const int newline = text.indexOf(QLatin1Char('\n'), cut);
    if (newline >= 0 && text.size() - (newline + 1) <= maxChars)
        cut = newline + 1;
    return text.mid(cut);
}

const char kCheckSuffix[] = "-CHECK.bin";    // 校验文件默认后缀，界面可改
const char kFirmwareSuffix[] = "-UPDATE.bin";  // 升级固件默认后缀，界面可改

const char *const kProjects[] = {
    "A0(WD)",          // 0x00  升级头第 3 字节为 00
    "93CD1xx",     // 0x01  升级头第 3 字节为 01
    "93GH1xx",     // 0x02  升级头第 3 字节为 02
    "88071",       // 0x03  升级头第 3 字节为 03
    "88071EVSCP",  // 0x04  升级头第 3 字节为 04
    "WK",          // 0x05  升级头第 3 字节为 05
    "UL48A",       // 0x06  升级头第 3 字节为 06
    "7(RD0047,G0)",           // 0x07  升级头第 3 字节为 07
    "8",           // 0x08  升级头第 3 字节为 08
    "9",           // 0x09  升级头第 3 字节为 09
    "10"           // 0x0A  升级头第 3 字节为 0A
};

quint16 crc16Modbus(const QByteArray &data)  // 只对固件原文做 CRC-16/MODBUS，规则见本文件开头说明
{
    quint16 crc = 0xFFFF;
    for (int i = 0; i < data.size(); ++i) {
        crc ^= static_cast<quint8>(data.at(i));
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 1u) ? static_cast<quint16>((crc >> 1) ^ 0xA001) : static_cast<quint16>(crc >> 1);
    }
    return crc;
}

void append16(QByteArray *out, quint16 value, bool little)  // 按指定端序追加两个字节
{
    if (little) {
        out->append(static_cast<char>(value & 0xFF));
        out->append(static_cast<char>((value >> 8) & 0xFF));
    } else {
        out->append(static_cast<char>((value >> 8) & 0xFF));
        out->append(static_cast<char>(value & 0xFF));
    }
}

void append32(QByteArray *out, quint32 value, bool little)  // 按指定端序追加四个字节
{
    if (little) {
        out->append(static_cast<char>(value & 0xFF));
        out->append(static_cast<char>((value >> 8) & 0xFF));
        out->append(static_cast<char>((value >> 16) & 0xFF));
        out->append(static_cast<char>((value >> 24) & 0xFF));
    } else {
        out->append(static_cast<char>((value >> 24) & 0xFF));
        out->append(static_cast<char>((value >> 16) & 0xFF));
        out->append(static_cast<char>((value >> 8) & 0xFF));
        out->append(static_cast<char>(value & 0xFF));
    }
}

} // namespace

FirmwarePackWidget::FirmwarePackWidget(QWidget *parent)
    : QGroupBox(parent)
{
    auto *grid = new QGridLayout(this);
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(4);
    grid->setContentsMargins(6, 12, 6, 6);

    // 打包信息放在分组顶部，和左侧校验文本对齐。操作控件留在下面。
    m_info = new QPlainTextEdit(this);
    m_info->setReadOnly(true);
    m_info->setMinimumHeight(72);
    m_info->setMaximumHeight(120);
    grid->addWidget(m_info, 0, 0, 1, 4);

    m_projectLabel = new QLabel(this);
    m_projectCombo = new QComboBox(this);
    m_projectCombo->setMaximumWidth(140);
    for (const char *name : kProjects)
        m_projectCombo->addItem(QString::fromLatin1(name));
    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setReadOnly(true);
    m_browseBtn = new QPushButton(this);
    grid->addWidget(m_projectLabel, 1, 0);
    grid->addWidget(m_projectCombo, 1, 1);
    grid->addWidget(m_pathEdit, 1, 2);
    grid->addWidget(m_browseBtn, 1, 3);

    m_checkSuffixLabel = new QLabel(this);
    m_checkSuffixEdit = new QLineEdit(this);
    m_checkSuffixEdit->setText(QString::fromLatin1(kCheckSuffix));
    m_firmwareSuffixLabel = new QLabel(this);
    m_firmwareSuffixEdit = new QLineEdit(this);
    m_firmwareSuffixEdit->setText(QString::fromLatin1(kFirmwareSuffix));
    auto *suffixes = new QHBoxLayout();
    suffixes->setSpacing(6);
    suffixes->addWidget(m_checkSuffixLabel);
    suffixes->addWidget(m_checkSuffixEdit, 1);
    suffixes->addWidget(m_firmwareSuffixLabel);
    suffixes->addWidget(m_firmwareSuffixEdit, 1);
    grid->addLayout(suffixes, 2, 0, 1, 4);

    auto *buttons = new QHBoxLayout();
    buttons->setSpacing(6);
    m_checkBtn = new QPushButton(this);
    m_updateBtn = new QPushButton(this);
    m_clearBtn = new QPushButton(this);
    buttons->addWidget(m_checkBtn);
    buttons->addWidget(m_updateBtn);
    buttons->addWidget(m_clearBtn);
    buttons->addStretch();
    grid->addLayout(buttons, 3, 0, 1, 4);
    grid->setColumnStretch(2, 1);

    connect(m_browseBtn, &QPushButton::clicked, this, &FirmwarePackWidget::browseFile);
    connect(m_checkBtn, &QPushButton::clicked, this, &FirmwarePackWidget::writeCheckFile);
    connect(m_updateBtn, &QPushButton::clicked, this, &FirmwarePackWidget::writeUpdateFile);
    connect(m_clearBtn, &QPushButton::clicked, this, &FirmwarePackWidget::clearInfo);
    loadSettings();
    connect(m_projectCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &FirmwarePackWidget::saveSettings);
    connect(m_checkSuffixEdit, &QLineEdit::editingFinished, this, &FirmwarePackWidget::saveSettings);
    connect(m_firmwareSuffixEdit, &QLineEdit::editingFinished, this, &FirmwarePackWidget::saveSettings);
    retranslateUi();
}

void FirmwarePackWidget::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QGroupBox::changeEvent(event);
}

void FirmwarePackWidget::loadSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("Firmware"));
    const int project = settings.value(QStringLiteral("packProject"), 0).toInt();
    if (project >= 0 && project < m_projectCombo->count())
        m_projectCombo->setCurrentIndex(project);
    m_pathEdit->setText(settings.value(QStringLiteral("packPath")).toString());
    if (settings.contains(QStringLiteral("packCheckSuffix")))
        m_checkSuffixEdit->setText(settings.value(QStringLiteral("packCheckSuffix")).toString());
    if (settings.contains(QStringLiteral("packFirmwareSuffix")))
        m_firmwareSuffixEdit->setText(settings.value(QStringLiteral("packFirmwareSuffix")).toString());
    m_info->setPlainText(settings.value(QStringLiteral("packInfo")).toString());
    settings.endGroup();
}

void FirmwarePackWidget::saveSettings() const
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("Firmware"));
    settings.setValue(QStringLiteral("packProject"), m_projectCombo->currentIndex());
    settings.setValue(QStringLiteral("packPath"), m_pathEdit->text());
    settings.setValue(QStringLiteral("packCheckSuffix"), m_checkSuffixEdit->text().trimmed());
    settings.setValue(QStringLiteral("packFirmwareSuffix"), m_firmwareSuffixEdit->text().trimmed());
    // 文本框本身已限在 1MB，这里再收一次，避免配置里留下被截掉的旧内容。
    settings.setValue(QStringLiteral("packInfo"), keepNewestText(m_info->toPlainText()));
    settings.endGroup();
}

void FirmwarePackWidget::retranslateUi()
{
    setTitle(tr("Pack"));
    m_projectLabel->setText(tr("Project:"));
    m_checkSuffixLabel->setText(tr("Check Suffix:"));
    m_firmwareSuffixLabel->setText(tr("Firmware Suffix:"));
    m_browseBtn->setText(tr("Select File"));
    m_checkBtn->setText(tr("Generate Check File"));
    m_updateBtn->setText(tr("Generate Update File"));
    m_clearBtn->setText(tr("Clear Result"));
    m_info->setPlaceholderText(tr("Pack info"));
}

void FirmwarePackWidget::browseFile()
{
    const QString current = m_pathEdit->text().trimmed();
    const QString start = current.isEmpty()
                              ? QFileInfo(SettingsWidget::firmwareFilePath(QStringLiteral("firmware.bin"))).absolutePath()
                              : current;
    const QString path = QFileDialog::getOpenFileName(this, tr("Please choose a file"), start, tr("All Files (*)"));
    if (!path.isEmpty()) {
        m_pathEdit->setText(QDir::toNativeSeparators(path));
        saveSettings();
    }
}

QString FirmwarePackWidget::suffixText(bool update) const
{
    const QString edited = (update ? m_firmwareSuffixEdit : m_checkSuffixEdit)->text().trimmed();
    if (!edited.isEmpty())
        return edited;
    return QString::fromLatin1(update ? kFirmwareSuffix : kCheckSuffix);
}

void FirmwarePackWidget::writeCheckFile()
{
    packFile(false);
}

void FirmwarePackWidget::writeUpdateFile()
{
    packFile(true);
}

void FirmwarePackWidget::clearInfo()
{
    m_info->clear();
    saveSettings();
}

bool FirmwarePackWidget::packFile(bool update)
{
    const QString path = m_pathEdit->text().trimmed();
    if (path.isEmpty()) {
        emit message(tr("Please choose a file"));
        return false;
    }
    QFile source(path);
    if (!source.open(QIODevice::ReadOnly)) {
        emit message(tr("Cannot read file: %1").arg(path));
        return false;
    }
    const QByteArray data = source.readAll();
    source.close();
    const quint16 crc = crc16Modbus(data);
    const quint32 size = static_cast<quint32>(data.size());

    QByteArray body;
    if (update) {
        // 升级头固定 10 字节：FFFF、项目号、0x01，然后是大端的长度和 CRC，后面才是原固件。
        body.append(static_cast<char>(0xFF));
        body.append(static_cast<char>(0xFF));
        body.append(static_cast<char>(m_projectCombo->currentIndex()));
        body.append(static_cast<char>(0x01));
        append32(&body, size, false);
        append16(&body, crc, false);
        body.append(data);
    } else {
        // 校验文件只有头：ABCD、1234、小端长度、小端 CRC，不含固件正文。
        append16(&body, 0xABCD, true);
        append16(&body, 0x1234, true);
        append32(&body, size, true);
        append16(&body, crc, true);
    }

    const QFileInfo info(path);
    QString baseName = info.fileName();
    // 不要「原名.bin-CHECK.bin」或「原名.bin-UPDATE.bin」，末尾的 .bin 让给后缀。
    if (baseName.endsWith(QLatin1String(".bin"), Qt::CaseInsensitive))
        baseName.chop(4);
    const QString outPath = info.absolutePath() + QLatin1Char('/') + baseName + suffixText(update);
    QFile out(outPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit message(tr("Cannot save file: %1").arg(outPath));
        return false;
    }
    if (out.write(body) != body.size()) {
        out.close();
        out.remove();
        emit message(tr("Cannot save file: %1").arg(outPath));
        return false;
    }
    out.close();
    const QString nativeOut = QDir::toNativeSeparators(outPath);
    QString header;
    const int headerBytes = qMin(body.size(), 10);
    for (int i = 0; i < headerBytes; ++i) {
        if (i)
            header += QLatin1Char(' ');
        header += QStringLiteral("%1").arg(static_cast<quint8>(body.at(i)), 2, 16, QLatin1Char('0')).toUpper();
    }
    QString report = tr("File: %1").arg(QDir::toNativeSeparators(info.absoluteFilePath())) + QLatin1Char('\n');
    if (update)
        report += tr("Project: %1").arg(m_projectCombo->currentText()) + QLatin1Char('\n');
    report += tr("Size: %1 bytes").arg(size) + QLatin1Char('\n')
              + tr("CRC16: %1").arg(QStringLiteral("%1").arg(crc, 4, 16, QLatin1Char('0')).toUpper()) + QLatin1Char('\n')
              + tr("Output: %1").arg(nativeOut) + QLatin1Char('\n')
              + tr("Header: %1").arg(header) + QLatin1Char('\n');
    QString shown = m_info->toPlainText();
    if (!shown.isEmpty())
        shown += QLatin1Char('\n');
    m_info->setPlainText(keepNewestText(shown + report));
    m_info->moveCursor(QTextCursor::End);
    saveSettings();
    emit message(tr("Saved %1").arg(nativeOut));
    return true;
}
