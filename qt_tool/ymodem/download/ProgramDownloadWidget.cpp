#include "ProgramDownloadWidget.h"

#include "DaObk.h"
#include "FwmpPack.h"

#include <QButtonGroup>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSettings>
#include <QSizePolicy>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>

namespace {

const char kH573Dir[] = "windows-tfm-tools-h573";  // OBK 缺省公钥和输出仍从这里找
const int kLogLines = 2000;  // 日志框最多留这么多行，再写就丢掉最旧的

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

QString acsfParent()  // 含有 ACSF 的那一层。exe 旁边有 ACSF 就用它，da 也在这一层。没有才用编译时的源码目录
{
    const QString inside = QStringLiteral("G0/G0-test/windows-tfm-tools/flash_stm32h573.bat");
    const QString marker = QStringLiteral("ACSF/") + inside;
    const QString besideExe = QDir(appDir()).absoluteFilePath(marker);
    if (QFileInfo::exists(besideExe))
        return appDir();
#ifdef ACSF_DIR
    const QString compiled = QDir::cleanPath(QString::fromUtf8(ACSF_DIR));
    if (QFileInfo::exists(QDir(compiled).filePath(inside)))
        return QFileInfo(compiled).dir().absolutePath();
#endif
    return QString();
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

QString packagedCopy(const QString &path)  // 旧绝对路径里的 ACSF 或 da 后半段，exe 旁边有同一份就改用那份
{
    const QString norm = QDir::fromNativeSeparators(path);
    const QStringList markers = {QStringLiteral("/ACSF/"), QStringLiteral("/da/")};
    for (const QString &marker : markers) {
        const int at = norm.indexOf(marker, 0, Qt::CaseInsensitive);
        if (at < 0)
            continue;
        const QString local = existingFile(appDir(), norm.mid(at + 1));
        if (!local.isEmpty())
            return local;
    }
    return QString();
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
    const QString acsf = acsfParent();
    if (pathInside(acsf, absolute))
        return QDir::fromNativeSeparators(QDir(acsf).relativeFilePath(absolute));
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
        const QString fromAcsf = existingFile(acsfParent(), trimmed);
        if (!fromAcsf.isEmpty())
            return fromAcsf;
        const QString copied = packagedCopy(trimmed);
        if (!copied.isEmpty())
            return copied;
        return QDir::toNativeSeparators(QDir::cleanPath(QDir(appDir()).filePath(trimmed)));
    }
    const QString copied = packagedCopy(trimmed);
    if (!copied.isEmpty())
        return copied;
    if (QFileInfo::exists(trimmed))
        return QFileInfo(trimmed).absoluteFilePath();
    const QString norm = QDir::fromNativeSeparators(trimmed);
    const QString marker = QStringLiteral("/windows-tfm-tools/");
    const int at = norm.indexOf(marker);
    if (at >= 0) {
        const QString recovered = resolvedScriptPath(norm.mid(at + marker.size()));
        if (QFileInfo::exists(recovered))
            return recovered;
    }
    return QDir::toNativeSeparators(trimmed);
}

QString dialogStartPath(const QString &shown)  // 文件框从输入框当前文字打开。找不到时不要退回程序目录
{
    const QString text = shown.trimmed();
    if (text.isEmpty())
        return QString();
    const QString path = QDir::fromNativeSeparators(text);
    if (QDir::isAbsolutePath(path))
        return QFileInfo(path).absoluteFilePath();
    const QString fromAcsf = existingFile(acsfParent(), path);
    if (!fromAcsf.isEmpty())
        return fromAcsf;
    if (!acsfParent().isEmpty())
        return QDir(acsfParent()).absoluteFilePath(path);
    return QFileInfo(path).absoluteFilePath();
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

QString downloadTempDir()  // 程序目录下的 tmp。每次下载先清空再放入这次的三个文件
{
    return QDir(appDir()).filePath(QStringLiteral("tmp"));
}

void removeDownloadTemp()
{
    QDir dir(downloadTempDir());
    if (dir.exists())
        dir.removeRecursively();
}

bool prepareDownloadTemp(QString *error)
{
    removeDownloadTemp();
    if (QDir().mkpath(downloadTempDir()))
        return true;
    if (error) {
        *error = QObject::tr("Cannot prepare temp directory: %1")
                     .arg(QDir::toNativeSeparators(downloadTempDir()));
    }
    return false;
}

bool copyIntoTemp(const QString &source, const QString &destName, QString *error)
{
    const QString dest = QDir(downloadTempDir()).filePath(destName);
    QFile::remove(dest);
    if (QFile::copy(source, dest))
        return true;
    if (error)
        *error = QObject::tr("Cannot copy file: %1").arg(QDir::toNativeSeparators(source));
    return false;
}

QString imageSideName(quint8 imageType)
{
    if (imageType == 0)
        return QStringLiteral("NS");
    if (imageType == 1)
        return QStringLiteral("S");
    return QString::number(imageType);
}

bool stageBoot(const QString &shown, QString *error, QString *log)
{
    const QString hex = resolvedScriptPath(shown);
    if (!QFileInfo::exists(hex)
            || QFileInfo(hex).suffix().compare(QLatin1String("hex"), Qt::CaseInsensitive) != 0) {
        if (error)
            *error = QObject::tr("Boot program must be a HEX file: %1").arg(shown);
        return false;
    }
    if (!copyIntoTemp(hex, QStringLiteral("bl2.hex"), error))
        return false;
    if (log)
        *log = QObject::tr("Copied %1 to %2").arg(QDir::toNativeSeparators(hex), QStringLiteral("bl2.hex"));
    return true;
}

bool stageImage(const QString &shown, const QString &destName, quint8 expectedType,
                QString *error, QString *log)
{
    const QString src = resolvedScriptPath(shown);
    if (!QFileInfo::exists(src)) {
        if (error)
            *error = QObject::tr("Cannot find file: %1").arg(shown);
        return false;
    }
    const QString suffix = QFileInfo(src).suffix();
    if (suffix.compare(QLatin1String("bin"), Qt::CaseInsensitive) == 0) {
        if (!copyIntoTemp(src, destName, error))
            return false;
        if (log)
            *log = QObject::tr("Copied %1 to %2").arg(QDir::toNativeSeparators(src), destName);
        return true;
    }
    if (suffix.compare(QLatin1String("fwmp"), Qt::CaseInsensitive) == 0) {
        FwmpPayload payload;
        if (!readFwmpPayload(src, &payload, error))
            return false;
        if (payload.imageType != expectedType) {
            if (error) {
                *error = QObject::tr("This FWMP is %1, expected %2")
                             .arg(imageSideName(payload.imageType), imageSideName(expectedType));
            }
            return false;
        }
        const QString dest = QDir(downloadTempDir()).filePath(destName);
        QFile output(dest);
        if (!output.open(QIODevice::WriteOnly) || output.write(payload.bytes) != payload.bytes.size()) {
            if (error)
                *error = QObject::tr("Cannot write BIN: %1").arg(QDir::toNativeSeparators(dest));
            return false;
        }
        output.close();
        QString note = QObject::tr("Unpacked %1 to %2").arg(QDir::toNativeSeparators(src), destName);
        const qint64 expectedBytes = expectedType == 1 ? qint64(512) * 1024 : qint64(1024) * 1024;
        if (payload.bytes.size() != expectedBytes) {
            note += QLatin1Char('\n');
            note += expectedType == 1
                    ? QObject::tr("S firmware is %1 bytes, expected 512 KB").arg(payload.bytes.size())
                    : QObject::tr("NS firmware is %1 bytes, expected 1 MB").arg(payload.bytes.size());
        }
        if (log)
            *log = note;
        return true;
    }
    if (error)
        *error = QObject::tr("Select a BIN or FWMP file: %1").arg(shown);
    return false;
}

void showPath(QLineEdit *edit, const QString &path)  // 把路径写进框，光标停在末尾
{
    const QString shown = textForPathEdit(path);
    if (edit->text() == shown)
        return;
    edit->setText(shown);
    edit->setCursorPosition(shown.size());
}

QString daFile(const QString &relative)  // 生成 OBK 时的缺省公钥和输出，对话框里还能改
{
    const QString stored = QString::fromLatin1(kH573Dir) + QLatin1Char('/')
                           + QStringLiteral("windows-tfm-tools/") + relative;
    return resolvedScriptPath(stored);
}

} // namespace

ProgramDownloadWidget::ProgramDownloadWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
    loadSettings();
    eachRow([this](ImageRow *row) {
        connect(row->chooseBoot, &QPushButton::clicked, this, [this, row]() {
            chooseImage(row->boot, tr("Select Boot"), tr("HEX Files (*.hex)"),
                        QStringList() << QStringLiteral("hex"));
        });
        connect(row->chooseSecure, &QPushButton::clicked, this, [this, row]() {
            chooseImage(row->secure, tr("Select Secure Program"), tr("BIN and FWMP (*.bin *.fwmp)"),
                        QStringList() << QStringLiteral("bin") << QStringLiteral("fwmp"));
        });
        connect(row->chooseApp, &QPushButton::clicked, this, [this, row]() {
            chooseImage(row->app, tr("Select Application"), tr("BIN and FWMP (*.bin *.fwmp)"),
                        QStringList() << QStringLiteral("bin") << QStringLiteral("fwmp"));
        });
        connect(row->download, &QPushButton::clicked, this, [this, row]() { runDownload(row); });
        QLineEdit *edits[] = {row->boot, row->secure, row->app};
        for (QLineEdit *edit : edits) {
            if (!edit)
                continue;
            connect(edit, &QLineEdit::editingFinished, this, [this, edit]() {
                showPath(edit, edit->text());
                saveSettings();
            });
        }
    });
    connect(m_chooseKey, &QPushButton::clicked, this, [this]() {
        chooseImage(m_key, tr("Select Public Key"),
                    tr("PEM Files (*.pem)"),
                    QStringList() << QStringLiteral("pem"));
    });
    connect(m_daClose, &QPushButton::clicked, this, &ProgramDownloadWidget::runDaClose);
    connect(m_choosePriv, &QPushButton::clicked, this, [this]() {
        chooseImage(m_priv, tr("Select Private Key"),
                    tr("PEM Files (*.pem)"),
                    QStringList() << QStringLiteral("pem"));
    });
    connect(m_chooseCert, &QPushButton::clicked, this, [this]() {
        chooseImage(m_cert, tr("Select Certificate Chain"),
                    tr("B64 Files (*.b64)"),
                    QStringList() << QStringLiteral("b64"));
    });
    connect(m_leaf, &QRadioButton::toggled, this, [this](bool checked) {
        if (!checked)
            return;
        showDaMaterial(true);
        saveSettings();
    });
    connect(m_intermediate, &QRadioButton::toggled, this, [this](bool checked) {
        if (!checked)
            return;
        showDaMaterial(false);
        saveSettings();
    });
    connect(m_regress, &QPushButton::clicked, this, &ProgramDownloadWidget::runDaRegression);
    QLineEdit *daEdits[] = {m_key, m_priv, m_cert};
    for (QLineEdit *edit : daEdits) {
        connect(edit, &QLineEdit::editingFinished, this, [this, edit]() {
            showPath(edit, edit->text());
            saveSettings();
        });
    }
    connect(m_changeCube, &QPushButton::clicked, this, &ProgramDownloadWidget::chooseCubeProg);
    connect(m_defaultCube, &QPushButton::clicked, this, &ProgramDownloadWidget::useDefaultCube);
    connect(m_cubeProg, &QLineEdit::editingFinished, this, [this]() {
        showPath(m_cubeProg, m_cubeProg->text());
        saveSettings();
    });
    connect(m_genObk, &QPushButton::clicked, this, &ProgramDownloadWidget::generateDaObk);
    connect(m_packS, &QPushButton::clicked, this, &ProgramDownloadWidget::packSecureFirmware);
    connect(m_packNs, &QPushButton::clicked, this, &ProgramDownloadWidget::packNonSecureFirmware);
    connect(m_unpackS, &QPushButton::clicked, this, &ProgramDownloadWidget::unpackSecureFirmware);
    connect(m_unpackNs, &QPushButton::clicked, this, &ProgramDownloadWidget::unpackNonSecureFirmware);
    connect(m_clearLog, &QPushButton::clicked, m_log, &QPlainTextEdit::clear);
}

ProgramDownloadWidget::~ProgramDownloadWidget()
{
    saveSettings();
    if (m_process) {
        m_process->blockSignals(true);
        m_process->kill();
        m_process->waitForFinished(1000);
    }
    removeDownloadTemp();
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
    m_packS = new QPushButton(this);
    m_packNs = new QPushButton(this);
    m_unpackS = new QPushButton(this);
    m_unpackNs = new QPushButton(this);
    obkRow->addWidget(m_genObk);
    obkRow->addWidget(m_packS);
    obkRow->addWidget(m_unpackS);
    obkRow->addWidget(m_packNs);
    obkRow->addWidget(m_unpackNs);
    obkRow->addStretch();
    root->addLayout(obkRow);

    configureProduct(&m_products[0], QStringLiteral("G0"), QStringLiteral("g0"),
                     QStringLiteral("G0"), QStringLiteral("flash_stm32h573"));
    configureProduct(&m_products[1], QStringLiteral("RAT"), QStringLiteral("rat"),
                     QStringLiteral("RAT"), QStringLiteral("flash_stm32h573"));
    configureProduct(&m_products[2], QStringLiteral("RD0047"), QStringLiteral("rd0047"),
                     QStringLiteral("RD0047"), QStringLiteral("flash_stm32h573"));
    configureProduct(&m_products[3], QStringLiteral("SECC"), QStringLiteral("secc"),
                     QStringLiteral("SECC"), QStringLiteral("flash_stm32h5f4"));

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *host = new QWidget(scroll);
    host->setStyleSheet(QStringLiteral(
        "QPushButton#drawerHeader {"
        " text-align: left; padding: 8px 12px; font-weight: 600;"
        " background: #f4f7fb; border: 1px solid #c5d0de; border-radius: 6px;"
        "}"
        "QPushButton#drawerHeader:checked {"
        " background: #e7eef8; border-bottom-left-radius: 0; border-bottom-right-radius: 0; border-bottom: none;"
        "}"
        "QWidget#drawerBody {"
        " background: #ffffff; border: 1px solid #c5d0de; border-top: none;"
        " border-bottom-left-radius: 6px; border-bottom-right-radius: 6px;"
        "}"));
    auto *drawers = new QVBoxLayout(host);
    drawers->setContentsMargins(0, 0, 0, 0);
    drawers->setSpacing(6);
    scroll->setWidget(host);
    for (int i = 0; i < 4; ++i) {
        QWidget *body = addDrawer(drawers, &m_products[i].header, m_products[i].name, i == 0);
        m_products[i].body = body;
        ImageRow *rows[] = {&m_products[i].test, &m_products[i].debug, &m_products[i].release};
        for (ImageRow *row : rows) {
            buildImageRow(row, body);
            auto *rule = new QFrame(body);
            rule->setFrameShape(QFrame::HLine);
            rule->setFrameShadow(QFrame::Sunken);
            static_cast<QVBoxLayout *>(body->layout())->addWidget(rule);
        }
    }
    drawers->addStretch();
    root->addWidget(scroll, 1);

    m_daTitle = new QLabel(this);
    m_daTitle->setObjectName(QStringLiteral("daTitle"));
    m_daTitle->setStyleSheet(QStringLiteral(
        "QLabel#daTitle {"
        " padding: 8px 12px; font-weight: 600;"
        " background: #e7eef8; border: 1px solid #c5d0de; border-bottom: none;"
        " border-top-left-radius: 6px; border-top-right-radius: 6px;"
        "}"));
    m_daBody = new QWidget(this);
    m_daBody->setObjectName(QStringLiteral("drawerBody"));
    m_daBody->setStyleSheet(QStringLiteral(
        "QWidget#drawerBody {"
        " background: #ffffff; border: 1px solid #c5d0de; border-top: none;"
        " border-bottom-left-radius: 6px; border-bottom-right-radius: 6px;"
        "}"));
    auto *daLayout = new QVBoxLayout(m_daBody);
    daLayout->setContentsMargins(10, 8, 10, 10);
    daLayout->setSpacing(8);
    buildDaRow(m_daBody);
    root->addWidget(m_daTitle);
    root->addWidget(m_daBody);

    auto *logBar = new QHBoxLayout();
    m_clearLog = new QPushButton(this);
    logBar->addWidget(m_clearLog);
    logBar->addStretch();
    root->addLayout(logBar);

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(kLogLines);
    m_log->setMinimumHeight(160);
    m_log->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    root->addWidget(m_log, 1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_statusLabel);
    retranslateUi();
}

void ProgramDownloadWidget::configureProduct(ProductPane *pane, const QString &name, const QString &prefix,
                                               const QString &folder, const QString &script)
{
    pane->name = name;
    auto fill = [&](ImageRow *row, const char *kind, const char *variant, const QString &arg, bool release) {
        row->bootKey = prefix + QLatin1String(kind) + QLatin1String("Boot");
        row->secureKey = prefix + QLatin1String(kind) + QLatin1String("Secure");
        row->appKey = prefix + QLatin1String(kind) + QLatin1String("App");
        row->scriptArg = arg;
        const QString base = QStringLiteral("ACSF/%1/%1-%2/windows-tfm-tools/").arg(folder, QLatin1String(variant));
        row->defaultScript = base + script + (release ? QStringLiteral("_release.bat") : QStringLiteral(".bat"));
        row->defaultBoot = base + QLatin1String(variant) + QStringLiteral("/bl2.hex");
        row->defaultSecure = base + QLatin1String(variant) + QStringLiteral("/tfm_s_signed.bin");
        row->defaultApp = base + QLatin1String(variant) + QStringLiteral("/tfm_ns_signed.bin");
    };
    fill(&pane->test, "Test", "test", QStringLiteral("test"), false);
    fill(&pane->debug, "Debug", "prod", QStringLiteral("prod"), false);
    fill(&pane->release, "Release", "release", QString(), true);
}

QWidget *ProgramDownloadWidget::addDrawer(QVBoxLayout *host, QPushButton **headerOut, const QString &title, bool open)
{
    QWidget *owner = host->parentWidget();
    auto *header = new QPushButton(owner);
    header->setCheckable(true);
    header->setObjectName(QStringLiteral("drawerHeader"));
    header->setCursor(Qt::PointingHandCursor);
    header->setProperty("drawerTitle", title);
    header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *body = new QWidget(owner);
    body->setObjectName(QStringLiteral("drawerBody"));
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(10, 8, 10, 10);
    layout->setSpacing(8);
    host->addWidget(header);
    host->addWidget(body);
    connect(header, &QPushButton::toggled, this, [header, body](bool checked) {
        body->setVisible(checked);
        const QString text = header->property("drawerTitle").toString();
        header->setText((checked ? QStringLiteral("▼  ") : QStringLiteral("▶  ")) + text);
    });
    header->setChecked(open);
    body->setVisible(header->isChecked());
    header->setText((header->isChecked() ? QStringLiteral("▼  ") : QStringLiteral("▶  ")) + title);
    *headerOut = header;
    return body;
}

void ProgramDownloadWidget::eachRow(const std::function<void(ImageRow *)> &fn)
{
    for (ProductPane &pane : m_products) {
        fn(&pane.test);
        fn(&pane.debug);
        fn(&pane.release);
    }
}

void ProgramDownloadWidget::refreshDrawers()
{
    auto paint = [](QPushButton *header) {
        if (!header)
            return;
        const QString title = header->property("drawerTitle").toString();
        header->setText((header->isChecked() ? QStringLiteral("▼  ") : QStringLiteral("▶  ")) + title);
    };
    for (ProductPane &pane : m_products)
        paint(pane.header);
}

void ProgramDownloadWidget::buildImageRow(ImageRow *row, QWidget *parent)
{
    auto *line = new QHBoxLayout();
    line->setSpacing(8);
    row->title = new QLabel(parent);
    line->addWidget(row->title);

    auto addRule = [parent, line]() {
        auto *rule = new QFrame(parent);
        rule->setFrameShape(QFrame::VLine);
        rule->setFrameShadow(QFrame::Sunken);
        line->addWidget(rule);
    };
    auto addPath = [parent, line](QLabel *&label, QLineEdit *&edit, QPushButton *&button) {
        label = new QLabel(parent);
        edit = new QLineEdit(parent);
        button = new QPushButton(parent);
        line->addWidget(label);
        line->addWidget(edit, 1);
        line->addWidget(button);
    };
    addRule();
    addPath(row->bootLabel, row->boot, row->chooseBoot);
    addRule();
    addPath(row->secureLabel, row->secure, row->chooseSecure);
    addRule();
    addPath(row->appLabel, row->app, row->chooseApp);

    row->download = new QPushButton(parent);
    line->addWidget(row->download);
    static_cast<QVBoxLayout *>(parent->layout())->addLayout(line);
}

void ProgramDownloadWidget::buildDaRow(QWidget *parent)
{
    auto *groupLayout = static_cast<QVBoxLayout *>(parent->layout());
    auto *keyLine = new QHBoxLayout();
    keyLine->setSpacing(8);
    m_keyLabel = new QLabel(parent);
    m_key = new QLineEdit(parent);
    m_chooseKey = new QPushButton(parent);
    m_daClose = new QPushButton(parent);
    keyLine->addWidget(m_keyLabel);
    keyLine->addWidget(m_key, 1);
    keyLine->addWidget(m_chooseKey);
    keyLine->addWidget(m_daClose);
    groupLayout->addLayout(keyLine);

    auto *rule = new QFrame(parent);
    rule->setFrameShape(QFrame::HLine);
    rule->setFrameShadow(QFrame::Sunken);
    groupLayout->addWidget(rule);

    auto *regLine = new QHBoxLayout();
    regLine->setSpacing(8);
    m_levelGroup = new QButtonGroup(this);
    m_leaf = new QRadioButton(parent);
    m_intermediate = new QRadioButton(parent);
    m_levelGroup->addButton(m_leaf);
    m_levelGroup->addButton(m_intermediate);
    m_leaf->setChecked(true);
    m_privLabel = new QLabel(parent);
    m_priv = new QLineEdit(parent);
    m_choosePriv = new QPushButton(parent);
    m_certLabel = new QLabel(parent);
    m_cert = new QLineEdit(parent);
    m_chooseCert = new QPushButton(parent);
    m_regress = new QPushButton(parent);
    regLine->addWidget(m_leaf);
    regLine->addWidget(m_intermediate);
    regLine->addWidget(m_privLabel);
    regLine->addWidget(m_priv, 1);
    regLine->addWidget(m_choosePriv);
    regLine->addWidget(m_certLabel);
    regLine->addWidget(m_cert, 1);
    regLine->addWidget(m_chooseCert);
    regLine->addWidget(m_regress);
    groupLayout->addLayout(regLine);
}

void ProgramDownloadWidget::showDaMaterial(bool leaf)
{
    const QString dir = QStringLiteral(
        "C:/Users/13584/Desktop/stm32h5xx/STM32CubeH5-1.7.0/Projects/STM32H573I-DK/ROT_Provisioning/DA");
    if (leaf) {
        showPath(m_priv, QDir(dir).filePath(QStringLiteral("Keys/key_3_leaf.pem")));
        showPath(m_cert, QDir(dir).filePath(QStringLiteral("Certificates/cert_leaf_chain.b64")));
    } else {
        showPath(m_priv, QDir(dir).filePath(QStringLiteral("Keys/key_2_intermediate.pem")));
        showPath(m_cert, QDir(dir).filePath(QStringLiteral("Certificates/cert_intermediate_chain.b64")));
    }
}

QString ProgramDownloadWidget::pickExistingFile(QLineEdit *edit, const QString &title, const QString &filter)
{
    // 每次都读输入框当前文字。框里改过路径，下一次就从新路径打开。
    const QFileInfo info(dialogStartPath(edit->text()));
    QString dir = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    while (!dir.isEmpty() && !QDir(dir).exists()) {
        const QString parent = QFileInfo(dir).dir().absolutePath();
        if (parent == dir)
            break;
        dir = parent;
    }
    if (!QDir(dir).exists())
        dir = appDir();
    // 系统文件框会记住上次打开的目录，不跟着输入框走。这里用 Qt 自己的文件框。
    QFileDialog dialog(this, title, dir, filter);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setDirectory(dir);
    if (!info.isDir() && !info.fileName().isEmpty() && QDir(info.absolutePath()).exists())
        dialog.selectFile(info.fileName());
    if (dialog.exec() != QDialog::Accepted)
        return QString();
    const QStringList files = dialog.selectedFiles();
    return files.isEmpty() ? QString() : files.first();
}

void ProgramDownloadWidget::appendLog(const QString &text)
{
    if (!m_log || text.isEmpty())
        return;
    QString chunk = text;
    chunk.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    chunk.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    m_log->moveCursor(QTextCursor::End);
    m_log->insertPlainText(chunk);
    m_log->ensureCursorVisible();
}

void ProgramDownloadWidget::pollScriptLog()
{
    if (m_scriptLogFile.isEmpty()) {
        QDir dir(m_scriptLogDir);
        const QFileInfoList files = dir.entryInfoList(
            QStringList() << m_logGlob,
            QDir::Files, QDir::Time | QDir::Reversed);
        for (const QFileInfo &info : files) {
            if (info.lastModified().toMSecsSinceEpoch() + 2000 < m_runStartedMs)
                continue;
            m_scriptLogFile = info.absoluteFilePath();
            m_scriptLogOffset = 0;
            break;
        }
    }
    if (m_scriptLogFile.isEmpty())
        return;
    QFile file(m_scriptLogFile);
    if (!file.open(QIODevice::ReadOnly))
        return;
    if (file.size() < m_scriptLogOffset)
        m_scriptLogOffset = 0;
    if (!file.seek(m_scriptLogOffset))
        return;
    const QByteArray chunk = file.readAll();
    m_scriptLogOffset = file.pos();
    if (chunk.isEmpty())
        return;
    const QString text = QString::fromLocal8Bit(chunk);
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1String("[FAIL]")) || trimmed.startsWith(QLatin1String("Failed:")))
            m_scriptFailed = true;
        if (trimmed.startsWith(QLatin1String("Failed:")))
            m_failStep = trimmed.mid(int(QStringLiteral("Failed:").size())).trimmed();
    }
    appendLog(text);
}

void ProgramDownloadWidget::setDownloadRunning(bool running)
{
    eachRow([running](ImageRow *row) { row->download->setEnabled(!running); });
    m_key->setEnabled(!running);
    m_chooseKey->setEnabled(!running);
    m_daClose->setEnabled(!running);
    m_leaf->setEnabled(!running);
    m_intermediate->setEnabled(!running);
    m_priv->setEnabled(!running);
    m_choosePriv->setEnabled(!running);
    m_cert->setEnabled(!running);
    m_chooseCert->setEnabled(!running);
    m_regress->setEnabled(!running);
}

void ProgramDownloadWidget::showRunStatus(const QString &result, bool ok)
{
    const qint64 ms = QDateTime::currentMSecsSinceEpoch() - m_runStartedMs;
    m_statusLabel->setText(tr("%1 %2 Time %3").arg(m_runTitle, result, formatElapsed(ms)));
    m_statusLabel->setStyleSheet(ok ? QStringLiteral("QLabel { color: #2e7d32; }")
                                     : QStringLiteral("QLabel { color: #c62828; }"));
}

QString ProgramDownloadWidget::formatElapsed(qint64 ms) const
{
    if (ms < 0)
        ms = 0;
    if (ms < 60000)
        return tr("%1 s").arg(QString::number(ms / 1000.0, 'f', 1));
    const int seconds = int(ms / 1000);
    return tr("%1 min %2 s").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

void ProgramDownloadWidget::runDownload(const ImageRow *row)
{
    if (m_process)
        return;
    m_runTitle = row->title ? row->title->text() : tr("Download");
    m_boxTitle = tr("Download");
    m_sendEnter = true;
    m_logGlob = QStringLiteral("flash_*.log");
    m_runStartedMs = QDateTime::currentMSecsSinceEpoch();
    m_statusLabel->setText(tr("%1 downloading").arg(m_runTitle));
    m_statusLabel->setStyleSheet(QStringLiteral("QLabel { color: #e65100; }"));
    const QString script = existingFile(acsfParent(), row->defaultScript);
    if (script.isEmpty()) {
        const QString message = tr("Cannot find script: %1").arg(row->defaultScript);
        appendLog(message + QLatin1Char('\n'));
        showRunStatus(message, false);
        QMessageBox::warning(this, tr("Download"), message);
        return;
    }
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString cubeText = m_cubeProg->text().trimmed();
    if (!cubeText.isEmpty()) {
        const QString cli = resolvedScriptPath(cubeText);
        if (!QFileInfo::exists(cli)) {
            const QString message = tr("Cannot find CubeProgrammer: %1").arg(cubeText);
            appendLog(message + QLatin1Char('\n'));
            showRunStatus(message, false);
            QMessageBox::warning(this, tr("Download"), message);
            return;
        }
        // 脚本先查自己写的三个目录。都没有时，where 会用到这里放在 PATH 前面的目录。
        const QString binDir = QDir::toNativeSeparators(QFileInfo(cli).absolutePath());
        env.insert(QStringLiteral("PATH"), binDir + QLatin1Char(';') + env.value(QStringLiteral("PATH")));
    }
    QString stageError;
    QString bootLog;
    QString secureLog;
    QString appLog;
    if (!prepareDownloadTemp(&stageError)
            || !stageBoot(row->boot->text(), &stageError, &bootLog)
            || !stageImage(row->secure->text(), QStringLiteral("tfm_s_signed.bin"), 1, &stageError, &secureLog)
            || !stageImage(row->app->text(), QStringLiteral("tfm_ns_signed.bin"), 0, &stageError, &appLog)) {
        removeDownloadTemp();
        appendLog(stageError + QLatin1Char('\n'));
        showRunStatus(stageError, false);
        QMessageBox::warning(this, tr("Download"), stageError);
        return;
    }
    const QString tempDir = QDir::toNativeSeparators(downloadTempDir());
    appendLog(tr("Temp directory: %1").arg(tempDir) + QLatin1Char('\n'));
    appendLog(bootLog + QLatin1Char('\n'));
    appendLog(secureLog + QLatin1Char('\n'));
    appendLog(appLog + QLatin1Char('\n'));
    m_process = new QProcess(this);
    m_process->setProcessEnvironment(env);
    m_process->setWorkingDirectory(QFileInfo(script).absolutePath());
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        // 管道里是 PowerShell 的 CLIXML，不往日志框写。只把数据读走，避免管道塞满。
        if (m_process)
            m_process->readAllStandardOutput();
    });
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus status) {
                QProcess *process = m_process;
                m_process = nullptr;
                if (m_logPoll)
                    m_logPoll->stop();
                pollScriptLog();
                removeDownloadTemp();
                setDownloadRunning(false);
                QString message;
                if (m_scriptFailed)
                    message = tr("Script failed: %1").arg(m_failStep.isEmpty() ? tr("Download") : m_failStep);
                else if (status == QProcess::CrashExit || code != 0)
                    message = tr("Script finished with code %1").arg(code);
                else
                    message = tr("Script finished.");
                appendLog(message + QLatin1Char('\n'));
                const bool ok = !m_scriptFailed && status != QProcess::CrashExit && code == 0;
                showRunStatus(message, ok);
                if (!ok)
                    QMessageBox::warning(this, m_boxTitle, message);
                if (process)
                    process->deleteLater();
            });
    connect(m_process, &QProcess::errorOccurred, this, [this, script](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || !m_process)
            return;
        QProcess *process = m_process;
        m_process = nullptr;
        process->disconnect(this);
        if (m_logPoll)
            m_logPoll->stop();
        removeDownloadTemp();
        setDownloadRunning(false);
        const QString message = tr("Cannot start script: %1").arg(script);
        appendLog(message + QLatin1Char('\n'));
        showRunStatus(message, false);
        QMessageBox::warning(this, m_boxTitle, message);
        process->deleteLater();
    });
    connect(m_process, &QProcess::started, this, [this]() {
        if (!m_process || !m_sendEnter)
            return;
        // 烧录脚本结尾是 pause。先送一个回车，跑完就退出，不用改 bat。
        m_process->write("\r\n");
    });
    m_scriptLogDir = QDir(QFileInfo(script).absolutePath()).filePath(QStringLiteral("log"));
    m_scriptLogFile.clear();
    m_scriptLogOffset = 0;
    m_scriptFailed = false;
    m_failStep.clear();
    if (!m_logPoll) {
        m_logPoll = new QTimer(this);
        m_logPoll->setInterval(200);
        connect(m_logPoll, &QTimer::timeout, this, &ProgramDownloadWidget::pollScriptLog);
    }
    m_logPoll->start();
    setDownloadRunning(true);
    appendLog(tr("Script started: %1").arg(QDir::toNativeSeparators(script)) + QLatin1Char('\n'));
    QStringList args;
    args << QStringLiteral("/c") << QDir::toNativeSeparators(script);
    if (!row->scriptArg.isEmpty())
        args << row->scriptArg;
    args << QStringLiteral("--dir") << tempDir;
    m_process->start(QStringLiteral("cmd.exe"), args);
}

void ProgramDownloadWidget::runDaClose()
{
    m_runTitle = tr("DA Authentication");
    m_runStartedMs = QDateTime::currentMSecsSinceEpoch();
    const QString key = resolvedScriptPath(m_key->text());
    if (!QFileInfo::exists(key)) {
        const QString message = tr("Cannot find file: %1").arg(m_key->text());
        appendLog(message + QLatin1Char('\n'));
        showRunStatus(message, false);
        QMessageBox::warning(this, tr("DA Authentication"), message);
        return;
    }
    runDaScript(QStringLiteral("da/da_close.bat"),
                QStringList() << QDir::toNativeSeparators(key),
                tr("DA Authentication"));
}

void ProgramDownloadWidget::runDaRegression()
{
    m_runTitle = tr("Run DA Regression");
    m_runStartedMs = QDateTime::currentMSecsSinceEpoch();
    const QString key = resolvedScriptPath(m_priv->text());
    const QString cert = resolvedScriptPath(m_cert->text());
    QString message;
    if (!QFileInfo::exists(key))
        message = tr("Cannot find file: %1").arg(m_priv->text());
    else if (!QFileInfo::exists(cert))
        message = tr("Cannot find file: %1").arg(m_cert->text());
    if (!message.isEmpty()) {
        appendLog(message + QLatin1Char('\n'));
        showRunStatus(message, false);
        QMessageBox::warning(this, tr("Run DA Regression"), message);
        return;
    }
    runDaScript(QStringLiteral("da/da_regression.bat"),
                QStringList() << QDir::toNativeSeparators(key) << QDir::toNativeSeparators(cert),
                tr("Run DA Regression"));
}

void ProgramDownloadWidget::runDaScript(const QString &scriptName, const QStringList &scriptArgs,
                                         const QString &title)
{
    if (m_process)
        return;
    m_runTitle = title;
    m_boxTitle = title;
    m_sendEnter = false;
    m_logGlob = QStringLiteral("da_*.log");
    m_runStartedMs = QDateTime::currentMSecsSinceEpoch();
    m_statusLabel->setText(tr("%1 running").arg(m_runTitle));
    m_statusLabel->setStyleSheet(QStringLiteral("QLabel { color: #e65100; }"));
    const QString script = existingFile(acsfParent(), scriptName);
    if (script.isEmpty()) {
        const QString message = tr("Cannot find script: %1").arg(scriptName);
        appendLog(message + QLatin1Char('\n'));
        showRunStatus(message, false);
        QMessageBox::warning(this, m_boxTitle, message);
        return;
    }
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString cubeText = m_cubeProg->text().trimmed();
    if (!cubeText.isEmpty()) {
        const QString cli = resolvedScriptPath(cubeText);
        if (!QFileInfo::exists(cli)) {
            const QString message = tr("Cannot find CubeProgrammer: %1").arg(cubeText);
            appendLog(message + QLatin1Char('\n'));
            showRunStatus(message, false);
            QMessageBox::warning(this, m_boxTitle, message);
            return;
        }
        const QString binDir = QDir::toNativeSeparators(QFileInfo(cli).absolutePath());
        env.insert(QStringLiteral("PATH"), binDir + QLatin1Char(';') + env.value(QStringLiteral("PATH")));
    }
    m_process = new QProcess(this);
    m_process->setProcessEnvironment(env);
    m_process->setWorkingDirectory(QFileInfo(script).absolutePath());
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        if (m_process)
            m_process->readAllStandardOutput();
    });
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus status) {
                QProcess *process = m_process;
                m_process = nullptr;
                if (m_logPoll)
                    m_logPoll->stop();
                pollScriptLog();
                setDownloadRunning(false);
                QString message;
                if (m_scriptFailed)
                    message = tr("Script failed: %1").arg(m_failStep.isEmpty() ? m_runTitle : m_failStep);
                else if (status == QProcess::CrashExit || code != 0)
                    message = tr("Script finished with code %1").arg(code);
                else
                    message = tr("Script finished.");
                appendLog(message + QLatin1Char('\n'));
                const bool ok = !m_scriptFailed && status != QProcess::CrashExit && code == 0;
                showRunStatus(message, ok);
                if (!ok)
                    QMessageBox::warning(this, m_boxTitle, message);
                if (process)
                    process->deleteLater();
            });
    connect(m_process, &QProcess::errorOccurred, this, [this, script](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || !m_process)
            return;
        QProcess *process = m_process;
        m_process = nullptr;
        process->disconnect(this);
        if (m_logPoll)
            m_logPoll->stop();
        setDownloadRunning(false);
        const QString message = tr("Cannot start script: %1").arg(script);
        appendLog(message + QLatin1Char('\n'));
        showRunStatus(message, false);
        QMessageBox::warning(this, m_boxTitle, message);
        process->deleteLater();
    });
    m_scriptLogDir = QDir(QFileInfo(script).absolutePath()).filePath(QStringLiteral("log"));
    m_scriptLogFile.clear();
    m_scriptLogOffset = 0;
    m_scriptFailed = false;
    m_failStep.clear();
    if (!m_logPoll) {
        m_logPoll = new QTimer(this);
        m_logPoll->setInterval(200);
        connect(m_logPoll, &QTimer::timeout, this, &ProgramDownloadWidget::pollScriptLog);
    }
    m_logPoll->start();
    setDownloadRunning(true);
    appendLog(tr("Script started: %1").arg(QDir::toNativeSeparators(script)) + QLatin1Char('\n'));
    QStringList args;
    args << QStringLiteral("/c") << QDir::toNativeSeparators(script);
    args << scriptArgs;
    m_process->start(QStringLiteral("cmd.exe"), args);
}

void ProgramDownloadWidget::chooseImage(QLineEdit *edit, const QString &title,
                                         const QString &filter, const QStringList &suffixes)
{
    const QString path = pickExistingFile(edit, title, filter);
    if (path.isEmpty())
        return;
    const QString suffix = QFileInfo(path).suffix();
    bool allowed = false;
    for (const QString &item : suffixes) {
        if (suffix.compare(item, Qt::CaseInsensitive) == 0) {
            allowed = true;
            break;
        }
    }
    if (!allowed) {
        QString message = tr("Boot program must be a HEX file: %1").arg(path);
        if (suffixes.contains(QStringLiteral("fwmp")))
            message = tr("Select a BIN or FWMP file: %1").arg(path);
        else if (suffixes.contains(QStringLiteral("pem")))
            message = tr("Select a PEM public key: %1").arg(path);
        else if (suffixes.contains(QStringLiteral("bat")))
            message = tr("Select a BAT script: %1").arg(path);
        else if (suffixes.contains(QStringLiteral("b64")))
            message = tr("Select a B64 certificate chain: %1").arg(path);
        QMessageBox::warning(this, title, message);
        return;
    }
    showPath(edit, path);
    saveSettings();
}

void ProgramDownloadWidget::showStoredOrDefault(QLineEdit *edit, const QString &saved,
                                                 const QString &relativeToAcsf)
{
    if (!saved.isEmpty()) {
        const QString resolved = resolvedScriptPath(saved);
        if (QFileInfo::exists(resolved)) {
            showPath(edit, resolved);
            return;
        }
    }
    const QString full = existingFile(acsfParent(), relativeToAcsf);
    if (!full.isEmpty())
        showPath(edit, full);
    else
        edit->setText(QDir::toNativeSeparators(relativeToAcsf));
}

void ProgramDownloadWidget::chooseCubeProg()
{
    const QString path = pickExistingFile(
        m_cubeProg, tr("Change Path"),
        tr("STM32_Programmer_CLI (STM32_Programmer_CLI.exe);;All Files (*)"));
    if (path.isEmpty())
        return;
    showPath(m_cubeProg, path);
    saveSettings();
}

void ProgramDownloadWidget::useDefaultCube()
{
    m_cubeProg->clear();
    saveSettings();
}

void ProgramDownloadWidget::generateDaObk()
{
    const QString key = daFile(QStringLiteral("ROT_Provisioning/DA/Keys/key_1_root_pub.pem"));
    const QString out = daFile(QStringLiteral("ROT_Provisioning/DA/Binary/DA_Config.obk"));
    DaObkDialog dialog(key, out, this);
    dialog.exec();
}

void ProgramDownloadWidget::packSecureFirmware()
{
    const QString relative = QStringLiteral("sign_kit/tfm_s_signed.bin");
    QString bin = existingFile(acsfParent(), relative);
    if (bin.isEmpty() && !acsfParent().isEmpty())
        bin = QDir(acsfParent()).filePath(relative);
    FwmpPackDialog dialog(FwmpPackDialog::Side::Secure, bin, this);
    connect(&dialog, &FwmpPackDialog::logged, this, [this](const QString &text) { appendLog(text); });
    dialog.exec();
}

void ProgramDownloadWidget::packNonSecureFirmware()
{
    const QString relative = QStringLiteral("sign_kit/tfm_ns_signed.bin");
    QString bin = existingFile(acsfParent(), relative);
    if (bin.isEmpty() && !acsfParent().isEmpty())
        bin = QDir(acsfParent()).filePath(relative);
    FwmpPackDialog dialog(FwmpPackDialog::Side::NonSecure, bin, this);
    connect(&dialog, &FwmpPackDialog::logged, this, [this](const QString &text) { appendLog(text); });
    dialog.exec();
}

void ProgramDownloadWidget::unpackSecureFirmware()
{
    const QString relative = QStringLiteral("sign_kit/tfm_s_signed.fwmp");
    QString fwmp = existingFile(acsfParent(), relative);
    if (fwmp.isEmpty() && !acsfParent().isEmpty())
        fwmp = QDir(acsfParent()).filePath(relative);
    FwmpUnpackDialog dialog(FwmpUnpackDialog::Side::Secure, fwmp, this);
    connect(&dialog, &FwmpUnpackDialog::logged, this, [this](const QString &text) { appendLog(text); });
    dialog.exec();
}

void ProgramDownloadWidget::unpackNonSecureFirmware()
{
    const QString relative = QStringLiteral("sign_kit/tfm_ns_signed.fwmp");
    QString fwmp = existingFile(acsfParent(), relative);
    if (fwmp.isEmpty() && !acsfParent().isEmpty())
        fwmp = QDir(acsfParent()).filePath(relative);
    FwmpUnpackDialog dialog(FwmpUnpackDialog::Side::NonSecure, fwmp, this);
    connect(&dialog, &FwmpUnpackDialog::logged, this, [this](const QString &text) { appendLog(text); });
    dialog.exec();
}

void ProgramDownloadWidget::loadSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("ProgramDownload"));
    const QString saved = settings.value(QStringLiteral("cubeProgPath")).toString();
    if (!saved.isEmpty())
        showPath(m_cubeProg, saved);
    eachRow([&](ImageRow *row) {
        showStoredOrDefault(row->boot, settings.value(row->bootKey).toString(), row->defaultBoot);
        showStoredOrDefault(row->secure, settings.value(row->secureKey).toString(), row->defaultSecure);
        QString savedApp = settings.value(row->appKey).toString();
        const QString oldHex = QString(row->defaultApp).replace(QLatin1String(".bin"), QLatin1String(".hex"));
        if (QDir::fromNativeSeparators(savedApp).endsWith(oldHex))
            savedApp.clear();
        showStoredOrDefault(row->app, savedApp, row->defaultApp);
    });
    const QString savedKey = settings.value(QStringLiteral("g0ReleaseKey")).toString();
    if (!savedKey.isEmpty())
        showPath(m_key, savedKey);
    else
        showPath(m_key, daFile(QStringLiteral("ROT_Provisioning/DA/Keys/key_1_root_pub.pem")));
    const bool leaf = settings.value(QStringLiteral("g0DaLevel"), QStringLiteral("leaf")).toString()
            != QLatin1String("intermediate");
    m_leaf->blockSignals(true);
    m_intermediate->blockSignals(true);
    m_leaf->setChecked(leaf);
    m_intermediate->setChecked(!leaf);
    m_leaf->blockSignals(false);
    m_intermediate->blockSignals(false);
    const QString savedPriv = settings.value(QStringLiteral("g0DaPriv")).toString();
    const QString savedCert = settings.value(QStringLiteral("g0DaCert")).toString();
    if (!savedPriv.isEmpty() && !savedCert.isEmpty()) {
        showPath(m_priv, savedPriv);
        showPath(m_cert, savedCert);
    } else {
        showDaMaterial(leaf);
    }
    settings.endGroup();
    saveSettings();
}

void ProgramDownloadWidget::saveSettings() const
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("ProgramDownload"));
    settings.setValue(QStringLiteral("cubeProgPath"), storedScriptPath(m_cubeProg->text()));
    for (const ProductPane &pane : m_products) {
        const ImageRow *rows[] = {&pane.test, &pane.debug, &pane.release};
        for (const ImageRow *row : rows) {
            settings.setValue(row->bootKey, storedScriptPath(row->boot->text()));
            settings.setValue(row->secureKey, storedScriptPath(row->secure->text()));
            settings.setValue(row->appKey, storedScriptPath(row->app->text()));
        }
    }
    settings.setValue(QStringLiteral("g0ReleaseKey"), storedScriptPath(m_key->text()));
    settings.setValue(QStringLiteral("g0DaLevel"),
                      m_leaf->isChecked() ? QStringLiteral("leaf") : QStringLiteral("intermediate"));
    settings.setValue(QStringLiteral("g0DaPriv"), storedScriptPath(m_priv->text()));
    settings.setValue(QStringLiteral("g0DaCert"), storedScriptPath(m_cert->text()));
    settings.endGroup();
}

void ProgramDownloadWidget::retranslateUi()
{
    m_cubeLabel->setText(tr("STM32CubeProgrammer:"));
    m_cubeProg->setPlaceholderText(tr("Default programmer search"));
    m_changeCube->setText(tr("Change Path"));
    m_defaultCube->setText(tr("Use Default"));
    m_genObk->setText(tr("Generate DA OBK"));
    m_packS->setText(tr("Pack S upgrade firmware"));
    m_packNs->setText(tr("Pack NS upgrade firmware"));
    m_unpackS->setText(tr("Unpack S upgrade firmware"));
    m_unpackNs->setText(tr("Unpack NS upgrade firmware"));
    for (ProductPane &pane : m_products) {
        pane.header->setProperty("drawerTitle", pane.name);
        pane.test.title->setText(tr("%1 test program:").arg(pane.name));
        pane.debug.title->setText(tr("%1 debug program:").arg(pane.name));
        pane.release.title->setText(tr("%1 release program:").arg(pane.name));
        pane.test.download->setToolTip(tr("This program can be debugged. The test program opens the serial port on both the secure and non-secure sides and shows the test run. Note: ITS and PS are erased."));
        pane.debug.download->setToolTip(tr("This program turns debug on and turns off non-secure side tests."));
        pane.release.download->setToolTip(tr("This program turns debug off and turns DA authentication on. Erase the program before programming again."));
    }
    m_daTitle->setText(tr("DA"));
    refreshDrawers();
    m_clearLog->setText(tr("Clear Log"));
    m_log->setPlaceholderText(tr("Script output"));
    eachRow([this](ImageRow *row) {
        row->bootLabel->setText(tr("Boot program:"));
        row->secureLabel->setText(tr("Secure program:"));
        row->appLabel->setText(tr("Application:"));
        row->chooseBoot->setText(tr("Select Boot"));
        row->chooseBoot->setToolTip(tr("Select a .hex file only.\nIt is copied as bl2.hex when downloading. The script programs this hex, and the addresses are inside the file."));
        row->chooseSecure->setText(tr("Select Secure Program"));
        row->chooseSecure->setToolTip(tr("Select a .bin or a packed .fwmp file.\nA .bin file must be 512 KB.\nA .fwmp file is the packed firmware and must be 524352 bytes (512 KB plus a 64-byte header). It is unpacked when downloading."));
        row->chooseApp->setText(tr("Select Application"));
        row->chooseApp->setToolTip(tr("Select a .bin or a packed .fwmp file.\nA .bin file must be 1 MB.\nA .fwmp file is the packed firmware and must be 1048640 bytes (1 MB plus a 64-byte header). It is unpacked when downloading."));
        row->download->setText(tr("Download Program"));
    });
    m_keyLabel->setText(tr("Public key:"));
    m_chooseKey->setText(tr("Select Public Key"));
    m_chooseKey->setToolTip(tr("Select a PEM public key (.pem). It is used to generate the DA OBK."));
    m_daClose->setText(tr("DA Authentication"));
    m_daClose->setToolTip(tr("Generate an OBK from this public key, write it, and set the product state to CLOSED. If the chip is already PROVISIONING, finish that close."));
    m_leaf->setText(tr("Leaf"));
    m_intermediate->setText(tr("Intermediate"));
    m_privLabel->setText(tr("Private key:"));
    m_choosePriv->setText(tr("Select Private Key"));
    m_certLabel->setText(tr("Certificate chain:"));
    m_chooseCert->setText(tr("Select Certificate Chain"));
    m_chooseCert->setToolTip(tr("Select a certificate chain file (.b64). The private key must match the last certificate."));
    m_regress->setText(tr("Run DA Regression"));
    m_regress->setToolTip(tr("Full regression back to Open. If the chip is PROVISIONING and the certificate regression fails, recover with the password OBK."));
}

void ProgramDownloadWidget::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QWidget::changeEvent(event);
}
