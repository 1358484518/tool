#include "ProgramDownloadWidget.h"

#include "SettingsWidget.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QTextCodec>
#include <QTextCursor>
#include <QVBoxLayout>

namespace {

const char kH573Dir[] = "windows-tfm-tools-h573";   // 目录名带连字符
const char kH5f4Dir[] = "windows-tfm-toolsh5f4";    // 目录名没有连字符
const char kScriptName[] = "da_tfm_close.bat";
const char kEraseScript[] = "ROT_Provisioning/DA/regression.bat";  // 两个芯片的擦除脚本同名同相对路径
const int kTextBytes = 4 * 1024 * 1024;  // 日志框最多留 4MB，按 QString 的 UTF-16 存储算

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

QString toolsRoot()  // windows-tfm-tools 目录。编译进的路径优先，否则从程序目录和当前目录往上找
{
#ifdef TFM_TOOLS_DIR
    const QString baked = QDir(QString::fromUtf8(TFM_TOOLS_DIR)).absolutePath();
    if (QDir(baked).exists())
        return baked;
#endif
    const QStringList starts = {QCoreApplication::applicationDirPath(), QDir::currentPath()};
    for (const QString &start : starts) {
        QDir dir(start);
        for (int i = 0; i < 8; ++i) {
            const QString candidate = dir.absoluteFilePath(QStringLiteral("windows-tfm-tools"));
            if (QDir(candidate).exists())
                return QDir(candidate).absolutePath();
            if (!dir.cdUp())
                break;
        }
    }
#ifdef TFM_TOOLS_DIR
    return QDir(QString::fromUtf8(TFM_TOOLS_DIR)).absolutePath();
#else
    return QString();
#endif
}

QString storedScriptPath(const QString &path)  // 落在工具目录里就存相对路径，目录外的文件仍存绝对路径
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty())
        return QString();
    const QString absolute = QFileInfo(trimmed).absoluteFilePath();
    const QString root = toolsRoot();
    if (root.isEmpty())
        return QDir::toNativeSeparators(absolute);
    const QString relative = QDir(root).relativeFilePath(absolute);
    if (QDir::isAbsolutePath(relative) || relative.startsWith(QLatin1String("..")))
        return QDir::toNativeSeparators(absolute);
    return QDir::fromNativeSeparators(relative);
}

QString resolvedScriptPath(const QString &stored)  // 相对路径按工具目录拼回全路径；旧绝对路径失效时按 windows-tfm-tools 后的一段再找
{
    const QString trimmed = stored.trimmed();
    if (trimmed.isEmpty())
        return QString();
    if (!QDir::isAbsolutePath(trimmed)) {
        const QString root = toolsRoot();
        if (root.isEmpty())
            return trimmed;
        const QString full = QDir(root).filePath(trimmed);
        return QFileInfo::exists(full)
                   ? QFileInfo(full).absoluteFilePath()
                   : QDir::toNativeSeparators(QDir::cleanPath(full));
    }
    if (QFileInfo::exists(trimmed))
        return QFileInfo(trimmed).absoluteFilePath();
    const QString norm = QDir::fromNativeSeparators(trimmed);
    const QString marker = QStringLiteral("/windows-tfm-tools/");
    const int at = norm.indexOf(marker);
    if (at >= 0)
        return resolvedScriptPath(norm.mid(at + marker.size()));
    return QDir::toNativeSeparators(trimmed);
}

QString decodeConsole(const QByteArray &bytes)  // cmd 按系统 ANSI（中文环境是 GBK）吐字节
{
    QTextCodec *codec = QTextCodec::codecForLocale();
    if (!codec)
        codec = QTextCodec::codecForName("GBK");
    QString text = codec ? codec->toUnicode(bytes) : QString::fromLocal8Bit(bytes);
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    text.remove(QLatin1Char('\r'));
    return text;
}

} // namespace

ProgramDownloadWidget::ProgramDownloadWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
    loadSettings();
    ChipPanel *panels[] = {&m_h573, &m_h5f4};
    for (ChipPanel *chip : panels) {
        connect(chip->changePath, &QPushButton::clicked, this, [this, chip]() { chooseScriptPath(chip->path); });
        connect(chip->changeErasePath, &QPushButton::clicked, this, [this, chip]() { chooseScriptPath(chip->erasePath); });
        connect(chip->download, &QPushButton::clicked, this, [this, chip]() { runScript(chip, chip->path->text()); });
        connect(chip->erase, &QPushButton::clicked, this, [this, chip]() { runScript(chip, chip->erasePath->text()); });
        connect(chip->saveLog, &QPushButton::clicked, this, [this, chip]() { saveLog(chip); });
        connect(chip->autoSave, &QCheckBox::toggled, this, [this, chip](bool on) {
            setAutoSave(chip, on);
            saveSettings();
        });
        if (chip->autoSave->isChecked())
            openAutoSave(chip);
    }
}

ProgramDownloadWidget::~ProgramDownloadWidget()
{
    saveSettings();
}

void ProgramDownloadWidget::setupUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    buildChip(&m_h573, QStringLiteral("STM32H573(G0/RD0047/RAT)"),
              scriptFile(QString::fromLatin1(kH573Dir), QString::fromLatin1(kScriptName)),
              scriptFile(QString::fromLatin1(kH573Dir), QString::fromLatin1(kEraseScript)),
              QStringLiteral("h573_download_auto.txt"));
    buildChip(&m_h5f4, QStringLiteral("STM32H5F4(PLC)"),
              scriptFile(QString::fromLatin1(kH5f4Dir), QString::fromLatin1(kScriptName)),
              scriptFile(QString::fromLatin1(kH5f4Dir), QString::fromLatin1(kEraseScript)),
              QStringLiteral("h5f4_download_auto.txt"));
    root->addWidget(m_h573.group, 1);
    root->addWidget(m_h5f4.group, 1);
    retranslateUi();
}

QString ProgramDownloadWidget::scriptFile(const QString &chipDir, const QString &scriptRelative) const
{
    const QString relative = chipDir + QLatin1Char('/')
                             + QStringLiteral("windows-tfm-tools/")
                             + scriptRelative;
    return resolvedScriptPath(relative);
}

void ProgramDownloadWidget::buildChip(ChipPanel *panel, const QString &title, const QString &scriptPath,
                                       const QString &erasePath, const QString &autoFileName)
{
    panel->autoFileName = autoFileName;
    panel->group = new QGroupBox(title, this);
    auto *layout = new QVBoxLayout(panel->group);
    layout->setContentsMargins(8, 14, 8, 8);
    layout->setSpacing(6);

    auto *row = new QHBoxLayout();
    row->setSpacing(8);
    panel->pathLabel = new QLabel(panel->group);
    panel->path = new QLineEdit(panel->group);
    panel->path->setReadOnly(true);
    panel->path->setText(QDir::toNativeSeparators(scriptPath));
    panel->path->setCursorPosition(panel->path->text().size());
    panel->changePath = new QPushButton(panel->group);
    panel->download = new QPushButton(panel->group);
    row->addWidget(panel->pathLabel);
    row->addWidget(panel->path, 1);
    row->addWidget(panel->changePath);
    row->addWidget(panel->download);
    layout->addLayout(row);

    auto *eraseRow = new QHBoxLayout();
    eraseRow->setSpacing(8);
    panel->erasePathLabel = new QLabel(panel->group);
    panel->erasePath = new QLineEdit(panel->group);
    panel->erasePath->setReadOnly(true);
    panel->erasePath->setText(QDir::toNativeSeparators(erasePath));
    panel->erasePath->setCursorPosition(panel->erasePath->text().size());
    panel->changeErasePath = new QPushButton(panel->group);
    panel->erase = new QPushButton(panel->group);
    eraseRow->addWidget(panel->erasePathLabel);
    eraseRow->addWidget(panel->erasePath, 1);
    eraseRow->addWidget(panel->changeErasePath);
    eraseRow->addWidget(panel->erase);
    layout->addLayout(eraseRow);

    panel->logLabel = new QLabel(panel->group);
    layout->addWidget(panel->logLabel);

    auto *tools = new QHBoxLayout();
    tools->setSpacing(8);
    panel->saveLog = new QPushButton(panel->group);
    panel->autoSave = new QCheckBox(panel->group);
    panel->autoSave->setChecked(true);  // 没读到设置时默认勾选
    tools->addWidget(panel->saveLog);
    tools->addWidget(panel->autoSave);
    tools->addStretch();
    layout->addLayout(tools);

    panel->log = new QPlainTextEdit(panel->group);
    panel->log->setReadOnly(true);
    panel->log->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    panel->log->setMinimumHeight(72);
    layout->addWidget(panel->log, 1);
}

void ProgramDownloadWidget::chooseScriptPath(QLineEdit *edit)
{
    const QString current = edit->text();
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Change Path"), current,
        tr("Batch Files (*.bat);;All Files (*)"));
    if (path.isEmpty())
        return;
    edit->setText(QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()));
    edit->setCursorPosition(edit->text().size());
    saveSettings();
}

void ProgramDownloadWidget::setScriptRunning(ChipPanel *panel, bool running)
{
    const bool idle = !running;
    panel->download->setEnabled(idle);
    panel->erase->setEnabled(idle);
    panel->changePath->setEnabled(idle);
    panel->changeErasePath->setEnabled(idle);
}

void ProgramDownloadWidget::runScript(ChipPanel *panel, const QString &path)
{
    if (panel->process)
        return;
    const bool erasing = QFileInfo(path).fileName().compare(QLatin1String("regression.bat"), Qt::CaseInsensitive) == 0;
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        QMessageBox::warning(this, erasing ? tr("Erase") : tr("Download"),
                             tr("Cannot find script: %1").arg(path));
        return;
    }
    panel->pending.clear();
    panel->endSeen = false;
    panel->process = new QProcess(this);
    panel->process->setWorkingDirectory(QFileInfo(path).absolutePath());
    panel->process->setProcessChannelMode(QProcess::MergedChannels);
    connect(panel->process, &QProcess::readyReadStandardOutput, this, [this, panel]() {
        takeScriptOutput(panel);
    });
    connect(panel->process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, panel](int code, QProcess::ExitStatus status) {
                finishScript(panel, code, status == QProcess::CrashExit);
            });
    connect(panel->process, &QProcess::errorOccurred, this, [this, panel, path, erasing](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || !panel->process)
            return;
        QMessageBox::warning(this, erasing ? tr("Erase") : tr("Download"),
                             tr("Cannot start script: %1").arg(path));
        setScriptRunning(panel, false);
        panel->process->deleteLater();
        panel->process = nullptr;
    });
    appendLog(panel, tr("Script started: %1").arg(QFileInfo(path).fileName()) + QLatin1Char('\n'));
    setScriptRunning(panel, true);
    // 必须自己拉管道，新开的控制台不会把输出送回本页。
    // 下载脚本末尾的 timeout 在管道下会立刻返回，步骤不再等人按键。
    // regression.bat 在参数不是 AUTO 时会 cmd /k，管道里没人输入，进程不会自己结束。
    QStringList args;
    args << QStringLiteral("/c") << QDir::toNativeSeparators(path);
    if (erasing)
        args << QStringLiteral("AUTO");
    panel->process->start(QStringLiteral("cmd.exe"), args);
}

void ProgramDownloadWidget::takeScriptOutput(ChipPanel *panel)
{
    if (!panel->process)
        return;
    panel->pending += panel->process->readAllStandardOutput();
    const int cut = panel->pending.lastIndexOf('\n');
    if (cut < 0)
        return;
    const QByteArray chunk = panel->pending.left(cut + 1);
    panel->pending.remove(0, cut + 1);
    const QString text = decodeConsole(chunk);
    appendLog(panel, text);
    if (panel->endSeen || !panel->process)
        return;
    // 脚本最后用 cmd /k 把窗口留着。这边没有控制台可输入，不结束就会一直占着进程。
    if (text.contains(QStringLiteral("窗口保持打开"))) {
        panel->endSeen = true;
        panel->process->kill();
    }
}

void ProgramDownloadWidget::finishScript(ChipPanel *panel, int exitCode, bool crashed)
{
    if (!panel->process)
        return;
    QProcess *process = panel->process;
    panel->process = nullptr;
    if (!panel->pending.isEmpty()) {
        appendLog(panel, decodeConsole(panel->pending));
        panel->pending.clear();
    }
    if (panel->endSeen || (!crashed && exitCode == 0))
        appendLog(panel, tr("Script finished.") + QLatin1Char('\n'));
    else if (!crashed)
        appendLog(panel, tr("Script finished with code %1").arg(exitCode) + QLatin1Char('\n'));
    else
        appendLog(panel, tr("Script stopped.") + QLatin1Char('\n'));
    panel->endSeen = false;
    setScriptRunning(panel, false);
    process->deleteLater();
}

void ProgramDownloadWidget::appendLog(ChipPanel *panel, const QString &text)
{
    if (text.isEmpty())
        return;
    QTextCursor cursor = panel->log->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text);
    panel->log->setTextCursor(cursor);
    panel->log->ensureCursorVisible();
    if (panel->log->document()->characterCount() > kTextBytes / static_cast<int>(sizeof(QChar))) {
        panel->log->setPlainText(keepNewestText(panel->log->toPlainText()));
        cursor = panel->log->textCursor();
        cursor.movePosition(QTextCursor::End);
        panel->log->setTextCursor(cursor);
    }
    if (!panel->autoSave->isChecked())
        return;
    if (!panel->autoFile && !openAutoSave(panel))
        return;
    panel->autoFile->write(text.toUtf8());
    panel->autoFile->flush();
}

void ProgramDownloadWidget::saveLog(ChipPanel *panel)
{
    const QString fileName = QFileDialog::getSaveFileName(
        this, tr("Save Log"),
        SettingsWidget::logFilePath(panel->autoFileName),
        tr("Text Files (*.txt);;All Files (*)"));
    if (fileName.isEmpty())
        return;
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Error"), tr("Failed to save file"));
        return;
    }
    file.write(panel->log->toPlainText().toUtf8());
}

void ProgramDownloadWidget::setAutoSave(ChipPanel *panel, bool enabled)
{
    if (!enabled) {
        delete panel->autoFile;
        panel->autoFile = nullptr;
        return;
    }
    openAutoSave(panel);
}

bool ProgramDownloadWidget::openAutoSave(ChipPanel *panel)
{
    const QString path = QFileInfo(SettingsWidget::logFilePath(panel->autoFileName)).absoluteFilePath();
    if (panel->autoFile && panel->autoFile->isOpen() && panel->autoFile->fileName() == path)
        return true;
    delete panel->autoFile;
    panel->autoFile = new QFile(path, this);
    if (panel->autoFile->open(QIODevice::WriteOnly | QIODevice::Append))
        return true;
    delete panel->autoFile;
    panel->autoFile = nullptr;
    panel->autoSave->blockSignals(true);
    panel->autoSave->setChecked(false);
    panel->autoSave->blockSignals(false);
    QMessageBox::warning(this, tr("Error"), tr("Cannot open auto-save file: %1").arg(path));
    return false;
}

void ProgramDownloadWidget::loadSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("ProgramDownload"));
    m_h573.autoSave->blockSignals(true);
    m_h5f4.autoSave->blockSignals(true);
    m_h573.autoSave->setChecked(settings.value(QStringLiteral("h573AutoSave"), true).toBool());
    m_h5f4.autoSave->setChecked(settings.value(QStringLiteral("h5f4AutoSave"), true).toBool());
    m_h573.autoSave->blockSignals(false);
    m_h5f4.autoSave->blockSignals(false);
    const auto useSaved = [](QLineEdit *edit, const QString &saved) {
        if (saved.isEmpty())
            return;
        edit->setText(QDir::toNativeSeparators(resolvedScriptPath(saved)));
        edit->setCursorPosition(edit->text().size());
    };
    useSaved(m_h573.path, settings.value(QStringLiteral("h573DownloadPath")).toString());
    useSaved(m_h573.erasePath, settings.value(QStringLiteral("h573ErasePath")).toString());
    useSaved(m_h5f4.path, settings.value(QStringLiteral("h5f4DownloadPath")).toString());
    useSaved(m_h5f4.erasePath, settings.value(QStringLiteral("h5f4ErasePath")).toString());
    settings.endGroup();
}

void ProgramDownloadWidget::saveSettings() const
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("ProgramDownload"));
    settings.setValue(QStringLiteral("h573AutoSave"), m_h573.autoSave->isChecked());
    settings.setValue(QStringLiteral("h5f4AutoSave"), m_h5f4.autoSave->isChecked());
    settings.setValue(QStringLiteral("h573DownloadPath"), storedScriptPath(m_h573.path->text()));
    settings.setValue(QStringLiteral("h573ErasePath"), storedScriptPath(m_h573.erasePath->text()));
    settings.setValue(QStringLiteral("h5f4DownloadPath"), storedScriptPath(m_h5f4.path->text()));
    settings.setValue(QStringLiteral("h5f4ErasePath"), storedScriptPath(m_h5f4.erasePath->text()));
    settings.endGroup();
}

void ProgramDownloadWidget::retranslateUi()
{
    m_h573.group->setTitle(QStringLiteral("STM32H573(G0/RD0047/RAT)"));
    m_h5f4.group->setTitle(QStringLiteral("STM32H5F4(PLC)"));
    const ChipPanel *panels[] = {&m_h573, &m_h5f4};
    for (const ChipPanel *panel : panels) {
        panel->pathLabel->setText(tr("Download Script Path:"));
        panel->changePath->setText(tr("Change Path"));
        panel->download->setText(tr("Run Download"));
        panel->erasePathLabel->setText(tr("Erase Script Path:"));
        panel->changeErasePath->setText(tr("Change Path"));
        panel->erase->setText(tr("Run Erase"));
        panel->logLabel->setText(tr("Log:"));
        panel->saveLog->setText(tr("Save Log"));
        panel->autoSave->setText(tr("Auto Save"));
        panel->autoSave->setToolTip(
            tr("Append new script output to the auto-save file in the log directory"));
    }
}

void ProgramDownloadWidget::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QWidget::changeEvent(event);
}
