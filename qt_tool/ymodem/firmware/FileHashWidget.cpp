#include "FileHashWidget.h"
#include "AppLanguage.h"
#include "SettingsWidget.h"

#include <QApplication>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDragEnterEvent>
#include <QEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSizePolicy>
#include <QTextCursor>
#include <QTextStream>
#include <QUrl>
#include <QVBoxLayout>

namespace {

const int kTextBytes = 1024 * 1024;  // 文本框最多留 1MB，按 QString 的 UTF-16 存储算

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

const quint32 *crc32Table()  // ZIP/Hash 1.04 用的 CRC32 表，多项式 0xEDB88320
{
    static quint32 table[256];
    static bool ready = false;
    if (!ready) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 value = i;
            for (int bit = 0; bit < 8; ++bit)
                value = (value & 1u) ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
            table[i] = value;
        }
        ready = true;
    }
    return table;
}

QString modifiedText(const QDateTime &time)  // 中文跟 Hash 窗口一样：2026年10月18日, 12:33:28
{
    const QDateTime local = time.toLocalTime();
    if (AppLanguage::language() == QLatin1String("zh_CN"))
        return local.toString(QStringLiteral("yyyy年M月d日, HH:mm:ss"));
    return local.toString(QStringLiteral("yyyy-MM-dd, HH:mm:ss"));
}

} // namespace

FileHashWidget::FileHashWidget(QWidget *parent)
    : QGroupBox(parent)
{
    setAcceptDrops(true);
    auto *root = new QVBoxLayout(this);
    root->setSpacing(4);
    root->setContentsMargins(6, 12, 6, 6);

    m_report = new QPlainTextEdit(this);
    m_report->setReadOnly(true);
    m_report->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_report->setMinimumHeight(72);
    m_report->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // 分组高度跟右侧组包走，文本把按钮上面的空位占满。
    root->addWidget(m_report, 1);

    auto *buttons = new QHBoxLayout();
    buttons->setSpacing(6);
    m_browseBtn = new QPushButton(this);
    m_clearBtn = new QPushButton(this);
    m_copyBtn = new QPushButton(this);
    m_saveBtn = new QPushButton(this);
    buttons->addWidget(m_browseBtn);
    buttons->addWidget(m_clearBtn);
    buttons->addWidget(m_copyBtn);
    buttons->addWidget(m_saveBtn);
    buttons->addStretch();
    root->addLayout(buttons);

    connect(m_browseBtn, &QPushButton::clicked, this, &FileHashWidget::browseFile);
    connect(m_clearBtn, &QPushButton::clicked, this, &FileHashWidget::clearResult);
    connect(m_copyBtn, &QPushButton::clicked, this, &FileHashWidget::copyResult);
    connect(m_saveBtn, &QPushButton::clicked, this, &FileHashWidget::saveResult);
    retranslateUi();
}

bool FileHashWidget::hashFile(const QString &path)
{
    return hashFiles(QStringList() << path);
}

bool FileHashWidget::hashFiles(const QStringList &paths)
{
    if (m_hashing)
        return false;
    QStringList files;
    for (const QString &path : paths) {
        if (path.isEmpty() || QFileInfo(path).isDir())
            continue;
        files.append(path);
    }
    if (files.isEmpty())
        return false;

    qint64 total = 0;
    for (const QString &path : files)
        total += qMax(qint64(0), QFileInfo(path).size());
    total = qMax(total, qint64(1));

    m_hashing = true;
    const quint32 *table = crc32Table();
    // 浏览和拖放是追加，原来的结果留给清除按钮处理。
    QString report = m_report->toPlainText();
    qint64 done = 0;
    int lastPercent = -1;
    int hashed = 0;
    emit progress(0);
    for (const QString &path : files) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            if (!report.isEmpty())
                report += QLatin1Char('\n');
            report += tr("Cannot read file: %1").arg(path) + QLatin1Char('\n');
            continue;
        }
        QCryptographicHash md5(QCryptographicHash::Md5);
        QCryptographicHash sha1(QCryptographicHash::Sha1);
        quint32 crc = 0xFFFFFFFFu;
        while (!file.atEnd()) {
            const QByteArray chunk = file.read(64 * 1024);
            md5.addData(chunk);
            sha1.addData(chunk);
            for (int i = 0; i < chunk.size(); ++i) {
                const quint8 byte = static_cast<quint8>(chunk.at(i));
                crc = table[(crc ^ byte) & 0xFFu] ^ (crc >> 8);
            }
            done += chunk.size();
            const int percent = int(qMin(qint64(100), done * 100 / total));
            if (percent != lastPercent) {
                lastPercent = percent;
                emit progress(percent);
                // 排除用户输入，避免状态栏刷新时又点进一次计算。
                QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            }
        }
        crc ^= 0xFFFFFFFFu;
        const QFileInfo info(path);
        if (!report.isEmpty())
            report += QLatin1Char('\n');
        report += tr("File: %1").arg(QDir::toNativeSeparators(info.absoluteFilePath())) + QLatin1Char('\n')
                  + tr("Size: %1 bytes").arg(info.size()) + QLatin1Char('\n')
                  + tr("Modified: %1").arg(modifiedText(info.lastModified())) + QLatin1Char('\n')
                  + QStringLiteral("MD5: ") + QString::fromLatin1(md5.result().toHex()).toUpper() + QLatin1Char('\n')
                  + QStringLiteral("SHA1: ") + QString::fromLatin1(sha1.result().toHex()).toUpper() + QLatin1Char('\n')
                  + QStringLiteral("CRC32: ") + QStringLiteral("%1").arg(crc, 8, 16, QLatin1Char('0')).toUpper()
                  + QLatin1Char('\n');
        ++hashed;
    }
    m_report->setPlainText(keepNewestText(report));
    m_report->moveCursor(QTextCursor::End);
    m_hashing = false;
    if (hashed == 0) {
        emit failed(tr("Cannot read file: %1").arg(files.first()));
        return false;
    }
    // 进度按全部文件的字节合计，不在每个文件上重新从 0 开始。
    emit progress(100);
    emit completed();
    return true;
}

void FileHashWidget::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QGroupBox::changeEvent(event);
}

void FileHashWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void FileHashWidget::dropEvent(QDropEvent *event)
{
    QStringList paths;
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl &url : urls) {
        const QString path = url.toLocalFile();
        if (!path.isEmpty())
            paths.append(path);
    }
    if (!paths.isEmpty())
        hashFiles(paths);
}

void FileHashWidget::retranslateUi()
{
    setTitle(tr("Hash"));
    m_report->setPlaceholderText(tr("Drop files here"));
    m_browseBtn->setText(tr("Browse"));
    m_clearBtn->setText(tr("Clear Result"));
    m_copyBtn->setText(tr("Copy"));
    m_saveBtn->setText(tr("Save"));
}

void FileHashWidget::browseFile()
{
    const QString start = QFileInfo(SettingsWidget::firmwareFilePath(QStringLiteral("firmware.bin"))).absolutePath();
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Please choose files"), start, tr("All Files (*)"));
    if (!paths.isEmpty())
        hashFiles(paths);
}

void FileHashWidget::copyResult()
{
    if (m_report->toPlainText().isEmpty())
        return;
    QApplication::clipboard()->setText(m_report->toPlainText());
}

void FileHashWidget::saveResult()
{
    const QString text = m_report->toPlainText();
    if (text.isEmpty())
        return;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save Hash"),
        SettingsWidget::firmwareFilePath(QStringLiteral("hash.txt")),
        tr("Text Files (*.txt);;All Files (*)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit failed(tr("Cannot save file: %1").arg(path));
        return;
    }
    QTextStream out(&file);
    out.setCodec("UTF-8");
    out << text;
}

void FileHashWidget::clearResult()
{
    m_report->clear();
}
