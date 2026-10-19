#include "ProgramDownloadWidget.h"

#include "DaObk.h"
#include "SettingsWidget.h"

#include <QCheckBox>
#include <QComboBox>
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
#include <QProcessEnvironment>
#include <QPushButton>
#include <QRegularExpression>
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

QString appDir()  // 程序所在目录。整包移动时相对路径都相对这里
{
    return QDir(QCoreApplication::applicationDirPath()).absolutePath();
}

QString toolsRoot()  // windows-tfm-tools。程序旁边的那份优先，避免整包搬走后仍回到编译时的目录
{
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

bool pathInside(const QString &root, const QString &absolute)  // absolute 是否落在 root 里面，用来决定能不能存相对路径
{
    if (root.isEmpty() || absolute.isEmpty())
        return false;
    const QString relative = QDir(root).relativeFilePath(absolute);
    return !QDir::isAbsolutePath(relative) && !relative.startsWith(QLatin1String(".."));
}

QString existingFile(const QString &root, const QString &relative)  // root 下这个相对路径存在就返回绝对路径
{
    if (root.isEmpty())
        return QString();
    const QString full = QDir(root).filePath(relative);
    return QFileInfo::exists(full) ? QFileInfo(full).absoluteFilePath() : QString();
}

QString storedScriptPath(const QString &path)  // 程序目录里存相对程序的路径；否则工具目录里存相对工具目录的路径
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty())
        return QString();
    if (!QDir::isAbsolutePath(trimmed))
        return QDir::fromNativeSeparators(QDir::cleanPath(trimmed));
    const QString absolute = QFileInfo(trimmed).absoluteFilePath();
    if (pathInside(appDir(), absolute))
        return QDir::fromNativeSeparators(QDir(appDir()).relativeFilePath(absolute));
    const QString root = toolsRoot();
    if (pathInside(root, absolute))
        return QDir::fromNativeSeparators(QDir(root).relativeFilePath(absolute));
    return QDir::toNativeSeparators(absolute);
}

QString resolvedScriptPath(const QString &stored)  // 相对路径先按程序目录找，再按旁边的工具目录找；旧绝对路径失效时按标记再找
{
    const QString trimmed = stored.trimmed();
    if (trimmed.isEmpty())
        return QString();
    if (!QDir::isAbsolutePath(trimmed)) {
        const QString fromApp = existingFile(appDir(), trimmed);
        if (!fromApp.isEmpty())
            return fromApp;
        // 以前存的是相对 windows-tfm-tools 的路径。工具目录整份拷到程序旁边时仍能对上。
        const QString fromCopied = existingFile(QDir(appDir()).filePath(QStringLiteral("windows-tfm-tools")),
                                                 trimmed);
        if (!fromCopied.isEmpty())
            return fromCopied;
        const QString fromTools = existingFile(toolsRoot(), trimmed);
        if (!fromTools.isEmpty())
            return fromTools;
        return QDir::toNativeSeparators(QDir::cleanPath(QDir(appDir()).filePath(trimmed)));
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

QString textForPathEdit(const QString &path)  // 框里：程序目录内显示相对路径，外面的文件仍显示绝对路径
{
    const QString resolved = resolvedScriptPath(path);
    if (resolved.isEmpty())
        return QString();
    const QString absolute = QFileInfo(resolved).absoluteFilePath();
    if (pathInside(appDir(), absolute))
        return QDir::toNativeSeparators(QDir(appDir()).relativeFilePath(absolute));
    return QDir::toNativeSeparators(absolute);
}

void showPath(QLineEdit *edit, const QString &path)  // 把路径写进框，光标停在末尾
{
    const QString shown = textForPathEdit(path);
    if (edit->text() == shown)
        return;
    edit->setText(shown);
    edit->setCursorPosition(shown.size());
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
    fillBootChoices();
    ChipPanel *panels[] = {&m_h573, &m_h5f4};
    for (ChipPanel *chip : panels) {
        connect(chip->changePath, &QPushButton::clicked, this, [this, chip]() { chooseScriptPath(chip->path); });
        connect(chip->changeErasePath, &QPushButton::clicked, this, [this, chip]() { chooseScriptPath(chip->erasePath); });
        connect(chip->changeApp, &QPushButton::clicked, this, [this, chip]() { chooseAppPath(chip->appPath); });
        connect(chip->defaultApp, &QPushButton::clicked, this, [this, chip]() { useDefaultApp(chip->appPath); });
        if (chip->bootPath) {
            connect(chip->changeBoot, &QPushButton::clicked, this, [this, chip]() { chooseBootPath(chip->bootPath); });
            connect(chip->defaultBoot, &QPushButton::clicked, this, [this, chip]() { useDefaultApp(chip->bootPath); });
            connect(chip->bootPath, &QLineEdit::editingFinished, this, [this, chip]() {
                showPath(chip->bootPath, chip->bootPath->text());
                saveSettings();
                fillBootChoices();
            });
            connect(chip->bootChoice, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, &ProgramDownloadWidget::applyBootChoice);
        }
        connect(chip->download, &QPushButton::clicked, this, [this, chip]() { runScript(chip, chip->path->text()); });
        connect(chip->erase, &QPushButton::clicked, this, [this, chip]() {
            if (!confirmEraseReset())
                return;
            runScript(chip, chip->erasePath->text());
        });
        connect(chip->saveLog, &QPushButton::clicked, this, [this, chip]() { saveLog(chip); });
        connect(chip->clearLog, &QPushButton::clicked, this, [this, chip]() { clearLog(chip); });
        QLineEdit *edits[] = {chip->path, chip->erasePath, chip->appPath};
        for (QLineEdit *edit : edits) {
            connect(edit, &QLineEdit::editingFinished, this, [this, edit]() {
                // 离开输入框时收成可搬走的写法，再落进注册表。
                showPath(edit, edit->text());
                saveSettings();
                if (edit == m_h573.path)
                    fillBootChoices();
            });
        }
        connect(chip->autoSave, &QCheckBox::toggled, this, [this, chip](bool on) {
            setAutoSave(chip, on);
            saveSettings();
        });
        if (chip->autoSave->isChecked())
            openAutoSave(chip);
    }
    connect(m_changeCube, &QPushButton::clicked, this, &ProgramDownloadWidget::chooseCubeProg);
    connect(m_defaultCube, &QPushButton::clicked, this, [this]() { useDefaultApp(m_cubeProg); });
    connect(m_cubeProg, &QLineEdit::editingFinished, this, [this]() {
        showPath(m_cubeProg, m_cubeProg->text());
        saveSettings();
    });
    connect(m_genObk, &QPushButton::clicked, this, &ProgramDownloadWidget::generateDaObk);
}

ProgramDownloadWidget::~ProgramDownloadWidget()
{
    saveSettings();
    ChipPanel *panels[] = {&m_h573, &m_h5f4};
    for (ChipPanel *panel : panels) {
        if (!panel->process)
            continue;
        // 析构过程中不再刷日志。先断信号，再砍掉整棵进程树。
        panel->process->blockSignals(true);
        stopProcessTree(panel);
        panel->process->kill();
        panel->process->waitForFinished(1000);
    }
}

void ProgramDownloadWidget::setupUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    auto *cubeRow = new QHBoxLayout();
    cubeRow->setSpacing(8);
    m_cubeLabel = new QLabel(this);
    m_cubeProg = new QLineEdit(this);
    m_changeCube = new QPushButton(this);
    m_defaultCube = new QPushButton(this);
    cubeRow->addWidget(m_cubeLabel);
    cubeRow->addWidget(m_cubeProg, 1);
    cubeRow->addWidget(m_changeCube);
    cubeRow->addWidget(m_defaultCube);
    root->addLayout(cubeRow);

    auto *obkRow = new QHBoxLayout();
    m_genObk = new QPushButton(this);
    obkRow->addWidget(m_genObk);
    obkRow->addStretch();
    root->addLayout(obkRow);

    buildChip(&m_h573, QStringLiteral("STM32H573(G0/RD0047/RAT)"),
              scriptFile(QString::fromLatin1(kH573Dir), QString::fromLatin1(kScriptName)),
              scriptFile(QString::fromLatin1(kH573Dir), QString::fromLatin1(kEraseScript)),
              QStringLiteral("h573_download_auto.txt"), true);
    buildChip(&m_h5f4, QStringLiteral("STM32H5F4(PLC)"),
              scriptFile(QString::fromLatin1(kH5f4Dir), QString::fromLatin1(kScriptName)),
              scriptFile(QString::fromLatin1(kH5f4Dir), QString::fromLatin1(kEraseScript)),
              QStringLiteral("h5f4_download_auto.txt"), false);
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
                                       const QString &erasePath, const QString &autoFileName, bool withBoot)
{
    panel->autoFileName = autoFileName;
    panel->group = new QGroupBox(title, this);
    auto *layout = new QVBoxLayout(panel->group);
    layout->setContentsMargins(8, 14, 8, 8);
    layout->setSpacing(6);

    auto *appRow = new QHBoxLayout();
    appRow->setSpacing(8);
    panel->appLabel = new QLabel(panel->group);
    panel->appPath = new QLineEdit(panel->group);
    panel->changeApp = new QPushButton(panel->group);
    panel->defaultApp = new QPushButton(panel->group);
    appRow->addWidget(panel->appLabel);
    appRow->addWidget(panel->appPath, 1);
    appRow->addWidget(panel->changeApp);
    appRow->addWidget(panel->defaultApp);
    layout->addLayout(appRow);

    if (withBoot) {
        auto *bootRow = new QHBoxLayout();
        bootRow->setSpacing(8);
        panel->bootLabel = new QLabel(panel->group);
        panel->bootChoice = new QComboBox(panel->group);
        panel->bootChoice->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        panel->bootPath = new QLineEdit(panel->group);
        panel->changeBoot = new QPushButton(panel->group);
        panel->defaultBoot = new QPushButton(panel->group);
        bootRow->addWidget(panel->bootLabel);
        bootRow->addWidget(panel->bootChoice);
        bootRow->addWidget(panel->bootPath, 1);
        bootRow->addWidget(panel->changeBoot);
        bootRow->addWidget(panel->defaultBoot);
        layout->addLayout(bootRow);
    }

    // 下载、擦除整排放在镜像路径下面，先填路径再执行。
    auto *row = new QHBoxLayout();
    row->setSpacing(8);
    panel->pathLabel = new QLabel(panel->group);
    panel->path = new QLineEdit(panel->group);
    showPath(panel->path, scriptPath);
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
    showPath(panel->erasePath, erasePath);
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
    panel->clearLog = new QPushButton(panel->group);
    panel->autoSave = new QCheckBox(panel->group);
    panel->autoSave->setChecked(true);  // 没读到设置时默认勾选
    tools->addWidget(panel->saveLog);
    tools->addWidget(panel->clearLog);
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
    const QString current = resolvedScriptPath(edit->text());
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Change Path"), current.isEmpty() ? appDir() : current,
        tr("Batch Files (*.bat);;All Files (*)"));
    if (path.isEmpty())
        return;
    showPath(edit, path);
    saveSettings();
    if (edit == m_h573.path)
        fillBootChoices();
}

void ProgramDownloadWidget::chooseAppPath(QLineEdit *edit)
{
    const QString current = resolvedScriptPath(edit->text());
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Change Path"), current.isEmpty() ? appDir() : current,
        tr("Binary Files (*.bin);;All Files (*)"));
    if (path.isEmpty())
        return;
    showPath(edit, path);
    saveSettings();
}

void ProgramDownloadWidget::chooseBootPath(QLineEdit *edit)
{
    const QString current = resolvedScriptPath(edit->text());
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Change Path"), current.isEmpty() ? appDir() : current,
        tr("Boot Files (*.hex *.bin);;All Files (*)"));
    if (path.isEmpty())
        return;
    showPath(edit, path);
    saveSettings();
    fillBootChoices();
}

void ProgramDownloadWidget::fillBootChoices()
{
    if (!m_h573.bootChoice)
        return;
    const QString script = resolvedScriptPath(m_h573.path->text());
    const QString root = QFileInfo(script).absolutePath();
    const QString bootNow = QDir::fromNativeSeparators(
        QFileInfo(resolvedScriptPath(m_h573.bootPath->text())).absoluteFilePath());
    // 目录名以 BOOT 结尾，例如 RD0047BOOT。旧写法 B00T（数字 0）也认。
    const QRegularExpression suffix(QStringLiteral("^(.*)(?:B00T|BOOT)$"),
                                     QRegularExpression::CaseInsensitiveOption);
    m_h573.bootChoice->blockSignals(true);
    m_h573.bootChoice->clear();
    int match = -1;
    const QStringList names = QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &name : names) {
        const QRegularExpressionMatch found = suffix.match(name);
        if (!found.hasMatch() || found.captured(1).isEmpty())
            continue;
        const QString folder = QDir(root).absoluteFilePath(name);
        m_h573.bootChoice->addItem(found.captured(1), folder);
        if (match < 0 && !bootNow.isEmpty()) {
            const QString prefix = QDir::fromNativeSeparators(QDir(folder).absolutePath());
            if (bootNow.compare(prefix, Qt::CaseInsensitive) == 0
                || bootNow.startsWith(prefix + QLatin1Char('/'), Qt::CaseInsensitive))
                match = m_h573.bootChoice->count() - 1;
        }
    }
    m_h573.bootChoice->setCurrentIndex(match);
    m_h573.bootChoice->blockSignals(false);
}

void ProgramDownloadWidget::applyBootChoice(int index)
{
    if (!m_h573.bootChoice || index < 0)
        return;
    const QString folder = m_h573.bootChoice->itemData(index).toString();
    QString file = QDir(folder).filePath(QStringLiteral("bl2.bin"));
    if (!QFileInfo::exists(file))
        file = QDir(folder).filePath(QStringLiteral("bl2.hex"));
    showPath(m_h573.bootPath, file);
    saveSettings();
}

void ProgramDownloadWidget::useDefaultApp(QLineEdit *edit)
{
    edit->clear();
    if (edit == m_h573.bootPath && m_h573.bootChoice) {
        m_h573.bootChoice->blockSignals(true);
        m_h573.bootChoice->setCurrentIndex(-1);
        m_h573.bootChoice->blockSignals(false);
    }
    saveSettings();
}

void ProgramDownloadWidget::generateDaObk()
{
    const QString key = scriptFile(QString::fromLatin1(kH573Dir),
                                   QStringLiteral("ROT_Provisioning/DA/Keys/key_1_root_pub.pem"));
    const QString out = scriptFile(QString::fromLatin1(kH573Dir),
                                   QStringLiteral("ROT_Provisioning/DA/Binary/DA_Config.obk"));
    DaObkDialog dialog(key, out, this);
    dialog.exec();
}

void ProgramDownloadWidget::chooseCubeProg()
{
    const QString current = resolvedScriptPath(m_cubeProg->text());
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Change Path"), current.isEmpty() ? appDir() : current,
        tr("STM32_Programmer_CLI (STM32_Programmer_CLI.exe);;All Files (*)"));
    if (path.isEmpty())
        return;
    showPath(m_cubeProg, path);
    saveSettings();
}

void ProgramDownloadWidget::setScriptRunning(ChipPanel *panel, bool running)
{
    const bool idle = !running;
    panel->download->setEnabled(idle);
    panel->erase->setEnabled(idle);
    panel->changePath->setEnabled(idle);
    panel->changeErasePath->setEnabled(idle);
    panel->changeApp->setEnabled(idle);
    panel->defaultApp->setEnabled(idle);
    if (panel->bootChoice)
        panel->bootChoice->setEnabled(idle);
    if (panel->changeBoot)
        panel->changeBoot->setEnabled(idle);
    if (panel->defaultBoot)
        panel->defaultBoot->setEnabled(idle);
    const bool cubeBusy = m_h573.process || m_h5f4.process;
    m_changeCube->setEnabled(!cubeBusy);
    m_defaultCube->setEnabled(!cubeBusy);
}

bool ProgramDownloadWidget::confirmEraseReset()
{
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("Erase"));
    box.setTextFormat(Qt::RichText);
    // 回归要接上 ST-LINK 并拉复位线。红字单独写出来，避免被主题色盖掉。
    box.setText(QStringLiteral("<span style=\"color:#ff0000;\">%1</span>")
                    .arg(tr("Before erase, connect ST-LINK to the MCU and connect the reset line to NRST.\nClick OK after both are connected.")
                             .toHtmlEscaped()
                             .replace(QLatin1Char('\n'), QStringLiteral("<br>"))));
    box.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Ok);
    return box.exec() == QMessageBox::Ok;
}

void ProgramDownloadWidget::runScript(ChipPanel *panel, const QString &path)
{
    if (panel->process)
        return;
    // 框里可能是相对程序目录的路径，执行前先拼成绝对路径。
    const QString script = resolvedScriptPath(path);
    const bool erasing = QFileInfo(script).fileName().compare(QLatin1String("regression.bat"), Qt::CaseInsensitive) == 0;
    if (script.isEmpty() || !QFileInfo::exists(script)) {
        QMessageBox::warning(this, erasing ? tr("Erase") : tr("Download"),
                             tr("Cannot find script: %1").arg(path));
        return;
    }
    const QString appText = panel->appPath->text().trimmed();
    const QString app = resolvedScriptPath(appText);
    if (!erasing && !appText.isEmpty() && !QFileInfo::exists(app)) {
        QMessageBox::warning(this, tr("Download"), tr("Cannot find app: %1").arg(appText));
        return;
    }
    const QString bootText = panel->bootPath ? panel->bootPath->text().trimmed() : QString();
    const QString boot = resolvedScriptPath(bootText);
    if (!erasing && !bootText.isEmpty() && !QFileInfo::exists(boot)) {
        QMessageBox::warning(this, tr("Download"), tr("Cannot find boot: %1").arg(bootText));
        return;
    }
    const QString cubeText = m_cubeProg->text().trimmed();
    const QString cube = resolvedScriptPath(cubeText);
    if (!cubeText.isEmpty() && !QFileInfo::exists(cube)) {
        QMessageBox::warning(this, erasing ? tr("Erase") : tr("Download"),
                             tr("Cannot find CubeProgrammer: %1").arg(cubeText));
        return;
    }
    panel->pending.clear();
    panel->endSeen = false;
    panel->process = new QProcess(this);
    panel->process->setWorkingDirectory(QFileInfo(script).absolutePath());
    panel->process->setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    bool customEnv = false;
    if (!erasing && !app.isEmpty()) {
        // 第 1 个参数已经是 ST-LINK 序列号，应用程序只能走环境变量，脚本只替换 NS 镜像。
        env.insert(QStringLiteral("TFM_NS_IMAGE"), QFileInfo(app).absoluteFilePath());
        customEnv = true;
    }
    if (!erasing && !boot.isEmpty()) {
        // hex 仍用文件里的地址，bin 写到 0x0C00E000。空着先找 bl2.bin，没有再找 bl2.hex。
        env.insert(QStringLiteral("TFM_BL2_IMAGE"), QFileInfo(boot).absoluteFilePath());
        customEnv = true;
    }
    if (!cube.isEmpty()) {
        // 两个分区共用这一条。脚本优先用它，空着才走原来的查找。
        const QString exe = QFileInfo(cube).absoluteFilePath();
        env.insert(QStringLiteral("STM32_PROGRAMMER_CLI"), exe);
        const QString binDir = QDir::toNativeSeparators(QFileInfo(exe).absolutePath());
        env.insert(QStringLiteral("PATH"), binDir + QLatin1Char(';') + env.value(QStringLiteral("PATH")));
        customEnv = true;
    }
    if (customEnv)
        panel->process->setProcessEnvironment(env);
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
        panel->process->deleteLater();
        panel->process = nullptr;
        setScriptRunning(panel, false);
    });
    appendLog(panel, tr("Script started: %1").arg(QFileInfo(script).fileName()) + QLatin1Char('\n'));
    setScriptRunning(panel, true);
    // 必须自己拉管道，新开的控制台不会把输出送回本页。
    // 下载脚本末尾的 timeout 在管道下会立刻返回，步骤不再等人按键。
    // regression.bat 在参数不是 AUTO 时会 cmd /k，管道里没人输入，进程不会自己结束。
    QStringList args;
    args << QStringLiteral("/c") << QDir::toNativeSeparators(script);
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
        stopProcessTree(panel);
    }
}

void ProgramDownloadWidget::stopProcessTree(ChipPanel *panel)
{
    QProcess *process = panel->process;
    if (!process)
        return;
    const qint64 pid = process->processId();
    if (pid > 0) {
        // TerminateProcess 只杀 cmd。编程器是 cmd 的子进程，父进程没了它还会继续占着 ST-LINK。
        QProcess killer;
        killer.start(QStringLiteral("taskkill.exe"),
                     {QStringLiteral("/F"), QStringLiteral("/T"), QStringLiteral("/PID"), QString::number(pid)});
        killer.waitForFinished(5000);
    }
    if (panel->process && panel->process->state() != QProcess::NotRunning)
        panel->process->kill();
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

void ProgramDownloadWidget::clearLog(ChipPanel *panel)
{
    // 只清窗口。自动保存文件是追加记录，清屏幕不应把它删掉。
    panel->log->clear();
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
        showPath(edit, saved);
    };
    useSaved(m_h573.path, settings.value(QStringLiteral("h573DownloadPath")).toString());
    useSaved(m_h573.erasePath, settings.value(QStringLiteral("h573ErasePath")).toString());
    useSaved(m_h5f4.path, settings.value(QStringLiteral("h5f4DownloadPath")).toString());
    useSaved(m_h5f4.erasePath, settings.value(QStringLiteral("h5f4ErasePath")).toString());
    useSaved(m_h573.appPath, settings.value(QStringLiteral("h573AppPath")).toString());
    useSaved(m_h573.bootPath, settings.value(QStringLiteral("h573BootPath")).toString());
    useSaved(m_h5f4.appPath, settings.value(QStringLiteral("h5f4AppPath")).toString());
    useSaved(m_cubeProg, settings.value(QStringLiteral("cubeProgPath")).toString());
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
    settings.setValue(QStringLiteral("h573AppPath"), storedScriptPath(m_h573.appPath->text()));
    settings.setValue(QStringLiteral("h573BootPath"), storedScriptPath(m_h573.bootPath->text()));
    settings.setValue(QStringLiteral("h5f4AppPath"), storedScriptPath(m_h5f4.appPath->text()));
    settings.setValue(QStringLiteral("cubeProgPath"), storedScriptPath(m_cubeProg->text()));
    settings.endGroup();
}

void ProgramDownloadWidget::retranslateUi()
{
    m_h573.group->setTitle(QStringLiteral("STM32H573(G0/RD0047/RAT)"));
    m_h5f4.group->setTitle(QStringLiteral("STM32H5F4(PLC)"));
    m_cubeLabel->setText(tr("STM32CubeProgrammer:"));
    m_cubeProg->setPlaceholderText(tr("Default programmer search"));
    m_changeCube->setText(tr("Change Path"));
    m_defaultCube->setText(tr("Use Default"));
    m_genObk->setText(tr("Generate DA OBK"));
    const ChipPanel *panels[] = {&m_h573, &m_h5f4};
    for (const ChipPanel *panel : panels) {
        panel->pathLabel->setText(tr("Download Script Path:"));
        panel->changePath->setText(tr("Change Path"));
        panel->download->setText(tr("Run Download"));
        panel->erasePathLabel->setText(tr("Erase Script Path:"));
        panel->changeErasePath->setText(tr("Change Path"));
        panel->erase->setText(tr("Run Erase"));
        panel->appLabel->setText(tr("App Program:"));
        panel->appPath->setPlaceholderText(tr("Default: tfm_ns_signed.bin"));
        panel->changeApp->setText(tr("Change Path"));
        panel->defaultApp->setText(tr("Use Default"));
        if (panel->bootLabel) {
            panel->bootLabel->setText(tr("Boot:"));
            panel->bootPath->setPlaceholderText(tr("Default: bl2.bin"));
            panel->changeBoot->setText(tr("Change Path"));
            panel->defaultBoot->setText(tr("Use Default"));
        }
        panel->logLabel->setText(tr("Log:"));
        panel->saveLog->setText(tr("Save Log"));
        panel->clearLog->setText(tr("Clear Log"));
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
