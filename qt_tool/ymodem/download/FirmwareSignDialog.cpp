#include "FirmwareSignDialog.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QRegularExpression>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QTextCursor>
#include <QVBoxLayout>

namespace {

struct ProductSign {
    const char *name;
    bool h573;
    const char *devProfile;
    const char *prodProfile;
};

const ProductSign kProducts[] = {
    {"G0", true, "DEV-CCU", "G0-V2G"},
    {"RAT", true, "DEV-CCU", "RAT-V2G"},
    {"RD0047", true, "DEV-CCU", "RD0047-V2G"},
    {"RD0050", true, "DEV-CCU", "RD0047-V2G"},
    {"EAW", true, "DEV-CCU", "RD0047-V2G"},
    {"SECC", false, "DEV-SECC", "SECC-AC-1"},
};

QString appDir()
{
    return QDir(QCoreApplication::applicationDirPath()).absolutePath();
}

QString acsfParent()
{
    const QString inside = QStringLiteral("G0/G0-test/windows-tfm-tools/flash_stm32h573.bat");
    const QString marker = QStringLiteral("ACSF/") + inside;
    if (QFileInfo::exists(QDir(appDir()).absoluteFilePath(marker)))
        return appDir();
#ifdef ACSF_DIR
    const QString compiled = QDir::cleanPath(QString::fromUtf8(ACSF_DIR));
    if (QFileInfo::exists(QDir(compiled).filePath(inside)))
        return QFileInfo(compiled).dir().absolutePath();
#endif
    return QString();
}

QString kitWithSignBat(const QString &root, const QString &folder)
{
    if (root.isEmpty())
        return QString();
    const QString bat = QDir(root).filePath(folder + QStringLiteral("/sign.bat"));
    return QFileInfo::exists(bat) ? QFileInfo(bat).absolutePath() : QString();
}

QString configValue(const QString &filePath, const QString &key)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        if (line.left(eq).trimmed().compare(key, Qt::CaseInsensitive) != 0)
            continue;
        QString value = line.mid(eq + 1).trimmed();
        const int hash = value.indexOf(QLatin1Char('#'));
        if (hash >= 0)
            value = value.left(hash).trimmed();
        return value;
    }
    return QString();
}

bool imageVersionOk(const QString &version)
{
    return QRegularExpression(QStringLiteral("^\\d+\\.\\d+\\.\\d+(?:\\+\\d+)?$")).match(version).hasMatch();
}

QString signKitDirectory()
{
    const QString folder = QStringLiteral("sign_kit");
    const QString beside = kitWithSignBat(appDir(), folder);
    if (!beside.isEmpty())
        return beside;
    const QString packaged = kitWithSignBat(acsfParent(), folder);
    if (!packaged.isEmpty())
        return packaged;
    const QString fallback = QStringLiteral("C:/Users/13584/Desktop/stm32h5xx/tool/qt_tool/sign_kit/sign.bat");
    return QFileInfo::exists(fallback) ? QFileInfo(fallback).absolutePath() : QString();
}

} // namespace

FirmwareSignDialog::FirmwareSignDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowFlags(Qt::Window
                   | Qt::WindowTitleHint
                   | Qt::WindowSystemMenuHint
                   | Qt::WindowMinimizeButtonHint
                   | Qt::WindowMaximizeButtonHint
                   | Qt::WindowCloseButtonHint);
    resize(860, 560);

    auto *layout = new QVBoxLayout(this);
    auto *productRow = new QHBoxLayout();
    auto *productLabel = new QLabel(this);
    productLabel->setObjectName(QStringLiteral("productLabel"));
    m_product = new QComboBox(this);
    for (const ProductSign &product : kProducts)
        m_product->addItem(QString::fromLatin1(product.name));
    productRow->addWidget(productLabel);
    productRow->addWidget(m_product);
    productRow->addStretch();
    layout->addLayout(productRow);

    m_profile = new QLabel(this);
    m_profile->setWordWrap(true);
    layout->addWidget(m_profile);

    auto *versionRow = new QHBoxLayout();
    m_versionSLabel = new QLabel(this);
    m_versionS = new QLineEdit(this);
    m_versionS->setMaximumWidth(120);
    m_versionNLabel = new QLabel(this);
    m_versionN = new QLineEdit(this);
    m_versionN->setMaximumWidth(120);
    versionRow->addWidget(m_versionSLabel);
    versionRow->addWidget(m_versionS);
    versionRow->addWidget(m_versionNLabel);
    versionRow->addWidget(m_versionN);
    versionRow->addStretch();
    layout->addLayout(versionRow);

    auto addFileRow = [this, layout](QLabel **label, QLineEdit **edit, QPushButton **browse) {
        auto *row = new QHBoxLayout();
        *label = new QLabel(this);
        *edit = new QLineEdit(this);
        *browse = new QPushButton(this);
        row->addWidget(*label);
        row->addWidget(*edit, 1);
        row->addWidget(*browse);
        layout->addLayout(row);
    };
    addFileRow(&m_secureLabel, &m_secure, &m_secureBrowse);
    addFileRow(&m_appLabel, &m_app, &m_appBrowse);

    auto *actions = new QHBoxLayout();
    m_dev = new QPushButton(this);
    m_remote = new QPushButton(this);
    actions->addWidget(m_dev);
    actions->addWidget(m_remote);
    actions->addStretch();
    layout->addLayout(actions);

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(2000);
    layout->addWidget(m_log, 1);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    connect(m_product, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        updateProfileText();
        loadStoredVersions();
    });
    connect(m_secureBrowse, &QPushButton::clicked, this, [this]() { browse(m_secure); });
    connect(m_appBrowse, &QPushButton::clicked, this, [this]() { browse(m_app); });
    connect(m_dev, &QPushButton::clicked, this, [this]() { startSign(false); });
    connect(m_remote, &QPushButton::clicked, this, [this]() { startSign(true); });
    retranslateUi();
    loadStoredVersions();
}

void FirmwareSignDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QDialog::changeEvent(event);
}

void FirmwareSignDialog::setDownloadBusy(bool busy)
{
    m_downloadBusy = busy;
    updateButtons();
}

void FirmwareSignDialog::updateButtons()
{
    const bool enable = !m_downloadBusy && !m_signing;
    m_dev->setEnabled(enable);
    m_remote->setEnabled(enable);
}

void FirmwareSignDialog::retranslateUi()
{
    setWindowTitle(tr("Sign firmware"));
    findChild<QLabel *>(QStringLiteral("productLabel"))->setText(tr("Product selection:"));
    m_secureLabel->setText(tr("Unsigned S:"));
    m_appLabel->setText(tr("Unsigned NS:"));
    m_versionSLabel->setText(tr("S version:"));
    m_versionNLabel->setText(tr("NS version:"));
    m_secureBrowse->setText(tr("Change Path"));
    m_appBrowse->setText(tr("Change Path"));
    m_dev->setText(tr("Development sign"));
    m_remote->setText(tr("Remote sign"));
    updateProfileText();
}

void FirmwareSignDialog::updateProfileText()
{
    const int index = m_product->currentIndex();
    if (index < 0 || index >= int(sizeof(kProducts) / sizeof(kProducts[0])))
        return;
    const ProductSign &product = kProducts[index];
    const QString slotText = product.h573
        ? tr("H573 slots S 0x0C044000, NS 0x0C100000")
        : tr("H5F4 slots S 0x0C078000, NS 0x0C0F8000");
    m_profile->setText(tr("Development profile %1, remote profile %2. %3")
                           .arg(QString::fromLatin1(product.devProfile),
                                QString::fromLatin1(product.prodProfile),
                                slotText));
}

void FirmwareSignDialog::loadStoredVersions()
{
    const int index = m_product->currentIndex();
    if (index < 0 || index >= int(sizeof(kProducts) / sizeof(kProducts[0])))
        return;
    const QString kit = signKitDirectory();
    const QString chip = kProducts[index].h573 ? QStringLiteral("h573") : QStringLiteral("h5f4");
    const QString configPath = QDir(kit).filePath(QStringLiteral("config.") + chip);
    const QString secure = configValue(configPath, QStringLiteral("MCUBOOT_IMAGE_VERSION_S"));
    const QString app = configValue(configPath, QStringLiteral("MCUBOOT_IMAGE_VERSION_NS"));
    m_versionS->setText(secure);
    m_versionN->setText(app);
    if (secure.isEmpty() || app.isEmpty())
        m_status->setText(tr("Cannot read signing version from %1").arg(QDir::toNativeSeparators(configPath)));
    else if (!m_signing)
        m_status->clear();
}

void FirmwareSignDialog::browse(QLineEdit *edit)
{
    QString start = edit->text().trimmed();
    if (start.isEmpty())
        start = appDir();
    else
        start = QFileInfo(start).absolutePath();
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select unsigned firmware"), start,
        tr("Unsigned firmware (*.bin);;All files (*)"));
    if (!path.isEmpty())
        edit->setText(QDir::toNativeSeparators(path));
}

void FirmwareSignDialog::appendLog(const QString &text)
{
    m_log->moveCursor(QTextCursor::End);
    m_log->insertPlainText(text);
    m_log->moveCursor(QTextCursor::End);
}

void FirmwareSignDialog::startSign(bool production)
{
    if (m_signing || m_downloadBusy)
        return;
    const int index = m_product->currentIndex();
    if (index < 0 || index >= int(sizeof(kProducts) / sizeof(kProducts[0])))
        return;
    const ProductSign &product = kProducts[index];

    m_kinds.clear();
    m_paths.clear();
    m_versions.clear();
    const QString secure = m_secure->text().trimmed();
    const QString app = m_app->text().trimmed();
    if (!secure.isEmpty()) {
        m_kinds << QStringLiteral("s");
        m_paths << secure;
        m_versions << m_versionS->text().trimmed();
    }
    if (!app.isEmpty()) {
        m_kinds << QStringLiteral("ns");
        m_paths << app;
        m_versions << m_versionN->text().trimmed();
    }
    if (m_paths.isEmpty()) {
        const QString message = tr("Select an unsigned bin first.");
        m_status->setText(message);
        QMessageBox::warning(this, tr("Sign firmware"), message);
        return;
    }
    for (const QString &version : m_versions) {
        if (!version.isEmpty() && !imageVersionOk(version)) {
            const QString message = tr("Version must look like 2.3.1.");
            m_status->setText(message);
            QMessageBox::warning(this, tr("Sign firmware"), message);
            return;
        }
    }
    for (const QString &path : m_paths) {
        if (!QFileInfo::exists(path)) {
            const QString message = tr("Cannot find file: %1").arg(path);
            m_status->setText(message);
            QMessageBox::warning(this, tr("Sign firmware"), message);
            return;
        }
    }

    m_kit = signKitDirectory();
    if (m_kit.isEmpty()) {
        const QString message = tr("Cannot find sign kit: %1").arg(QStringLiteral("sign_kit"));
        appendLog(message + QLatin1Char('\n'));
        m_status->setText(message);
        QMessageBox::warning(this, tr("Sign firmware"), message);
        return;
    }

    m_production = production;
    m_chip = product.h573 ? QStringLiteral("h573") : QStringLiteral("h5f4");
    m_profileName = QString::fromLatin1(production ? product.prodProfile : product.devProfile);
    m_job = 0;
    m_signing = true;
    updateButtons();
    m_status->setText(tr("Signing %1").arg(QString::fromLatin1(product.name)));
    appendLog(tr("Sign kit: %1").arg(QDir::toNativeSeparators(m_kit)) + QLatin1Char('\n'));
    emit signingChanged(true);
    startNext();
}

void FirmwareSignDialog::startNext()
{
    if (m_process || m_job < 0 || m_job >= m_paths.size())
        return;
    const QString bat = QDir(m_kit).filePath(QStringLiteral("sign.bat"));
    const QString kind = m_kinds.at(m_job);
    const QString input = QDir::toNativeSeparators(m_paths.at(m_job));
    appendLog(tr("Signing %1 %2").arg(kind.toUpper(), input) + QLatin1Char('\n'));

    m_process = new QProcess(this);
    m_process->setWorkingDirectory(m_kit);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        if (!m_process)
            return;
        const QString text = QString::fromLocal8Bit(m_process->readAllStandardOutput());
        if (!text.isEmpty())
            appendLog(text);
    });
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus status) {
                if (!m_process)
                    return;
                const QString tail = QString::fromLocal8Bit(m_process->readAllStandardOutput());
                if (!tail.isEmpty())
                    appendLog(tail);
                QProcess *process = m_process;
                m_process = nullptr;
                process->deleteLater();
                const bool ok = status != QProcess::CrashExit && code == 0;
                if (!ok) {
                    finishSign(false, tr("Signing failed, exit code %1").arg(code));
                    return;
                }
                ++m_job;
                if (m_job < m_paths.size()) {
                    startNext();
                    return;
                }
                finishSign(true, tr("Signing finished."));
            });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || !m_process)
            return;
        QProcess *process = m_process;
        m_process = nullptr;
        process->disconnect(this);
        process->deleteLater();
        finishSign(false, tr("Cannot start script: %1").arg(QDir(m_kit).filePath(QStringLiteral("sign.bat"))));
    });

    QStringList args;
    args << QStringLiteral("/c")
         << QDir::toNativeSeparators(bat)
         << QStringLiteral("--chip")
         << m_chip
         << (m_production ? QStringLiteral("--prod") : QStringLiteral("--dev"))
         << QStringLiteral("--profile")
         << m_profileName;
    const QString version = m_versions.at(m_job);
    if (!version.isEmpty())
        args << QStringLiteral("--version") << version;
    args << kind << input;
    QStringList shown;
    for (int i = 1; i < args.size(); ++i) {
        const QString arg = args.at(i);
        if (arg.contains(QLatin1Char(' ')) || arg.contains(QLatin1Char('\t')))
            shown << QLatin1Char('"') + arg + QLatin1Char('"');
        else
            shown << arg;
    }
    appendLog(shown.join(QLatin1Char(' ')) + QLatin1Char('\n'));
    m_process->start(QStringLiteral("cmd.exe"), args);
}

void FirmwareSignDialog::finishSign(bool ok, const QString &message)
{
    m_signing = false;
    m_kinds.clear();
    m_paths.clear();
    m_versions.clear();
    appendLog(message + QLatin1Char('\n'));
    m_status->setText(message);
    emit signingChanged(false);
    updateButtons();
    if (!ok)
        QMessageBox::warning(this, tr("Sign firmware"), message);
}
