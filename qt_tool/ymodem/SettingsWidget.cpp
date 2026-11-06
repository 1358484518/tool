#include "SettingsWidget.h"
#include "AppLanguage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QVBoxLayout>

SettingsWidget::SettingsWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
    loadSettings();
    // 读完再接线，避免恢复下拉时把还没填上的日志目录写空。
    connect(m_startupCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { saveSettings(); });
    connect(m_languageCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { applyLanguage(); });
    connect(m_logDirEdit, &QLineEdit::editingFinished, this, [this]() { saveSettings(); });
    connect(m_firmwareDirEdit, &QLineEdit::editingFinished, this, [this]() { saveSettings(); });
    const auto onTabBox = [this](bool) {
        syncStartupCombo();
        saveSettings();
        emit tabVisibilityChanged();
    };
    connect(m_showSerial, &QCheckBox::toggled, this, onTabBox);
    connect(m_showNetwork, &QCheckBox::toggled, this, onTabBox);
    connect(m_showFtp, &QCheckBox::toggled, this, onTabBox);
    connect(m_showFirmware, &QCheckBox::toggled, this, onTabBox);
}

SettingsWidget::~SettingsWidget()
{
    saveSettings();
}

int SettingsWidget::startupTab() const
{
    const int tab = m_startupCombo->currentData().toInt();
    return tab < 0 ? 0 : tab;
}

bool SettingsWidget::showSerial() const
{
    return m_showSerial->isChecked();
}

bool SettingsWidget::showNetwork() const
{
    return m_showNetwork->isChecked();
}

bool SettingsWidget::showFtp() const
{
    return m_showFtp->isChecked();
}

bool SettingsWidget::showFirmware() const
{
    return m_showFirmware->isChecked();
}

QString SettingsWidget::logDirectory()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("App"));
    QString dir = settings.value(QStringLiteral("logDir")).toString().trimmed();
    settings.endGroup();
    if (dir.isEmpty())
        dir = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("log"));
    QDir().mkpath(dir);
    return QDir(dir).absolutePath();
}

QString SettingsWidget::logFilePath(const QString &fileName)
{
    return QDir(logDirectory()).filePath(fileName);
}

QString SettingsWidget::firmwareFilePath(const QString &fileName)
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("App"));
    const QString dir = settings.value(QStringLiteral("firmwareDir")).toString();
    settings.endGroup();
    // 没填或目录不存在时仍落到系统下载目录，避免保存框没有起始位置。
    if (dir.isEmpty() || !QDir(dir).exists())
        return QDir(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).filePath(fileName);
    return QDir(dir).filePath(fileName);
}

void SettingsWidget::setupUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    auto *group = new QGroupBox(this);
    m_group = group;
    group->setMaximumWidth(820);
    auto *grid = new QGridLayout(group);
    grid->setSpacing(8);
    grid->setContentsMargins(10, 16, 10, 10);

    m_startupLabel = new QLabel(group);
    grid->addWidget(m_startupLabel, 0, 0);
    m_startupCombo = new QComboBox(group);
    m_startupCombo->addItem(QString(), 0);
    m_startupCombo->addItem(QString(), 1);
    m_startupCombo->addItem(QString(), 2);
    m_startupCombo->addItem(QString(), 3);
    m_startupCombo->addItem(QString(), 4);
    m_startupCombo->addItem(QString(), 5);
    grid->addWidget(m_startupCombo, 0, 1);

    m_languageLabel = new QLabel(group);
    grid->addWidget(m_languageLabel, 1, 0);
    m_languageCombo = new QComboBox(group);
    m_languageCombo->addItem(QString(), QStringLiteral("en"));
    m_languageCombo->addItem(QString(), QStringLiteral("zh_CN"));
    grid->addWidget(m_languageCombo, 1, 1);

    m_logDirLabel = new QLabel(group);
    grid->addWidget(m_logDirLabel, 2, 0);
    auto *dirRow = new QHBoxLayout();
    dirRow->setSpacing(4);
    m_logDirEdit = new QLineEdit(group);
    dirRow->addWidget(m_logDirEdit, 1);
    m_browseBtn = new QPushButton(group);
    dirRow->addWidget(m_browseBtn);
    grid->addLayout(dirRow, 2, 1);
    connect(m_browseBtn, &QPushButton::clicked, this, &SettingsWidget::browseLogDir);

    m_firmwareDirLabel = new QLabel(group);
    grid->addWidget(m_firmwareDirLabel, 3, 0);
    auto *firmwareRow = new QHBoxLayout();
    firmwareRow->setSpacing(4);
    m_firmwareDirEdit = new QLineEdit(group);
    firmwareRow->addWidget(m_firmwareDirEdit, 1);
    m_firmwareBrowseBtn = new QPushButton(group);
    firmwareRow->addWidget(m_firmwareBrowseBtn);
    grid->addLayout(firmwareRow, 3, 1);
    connect(m_firmwareBrowseBtn, &QPushButton::clicked, this, &SettingsWidget::browseFirmwareDir);

    m_tabsLabel = new QLabel(group);
    grid->addWidget(m_tabsLabel, 4, 0);
    auto *tabsRow = new QHBoxLayout();
    tabsRow->setSpacing(12);
    m_showSerial = new QCheckBox(group);
    m_showNetwork = new QCheckBox(group);
    m_showFtp = new QCheckBox(group);
    m_showFirmware = new QCheckBox(group);
    m_showSerial->setChecked(false);
    m_showNetwork->setChecked(false);
    m_showFtp->setChecked(false);
    m_showFirmware->setChecked(false);
    tabsRow->addWidget(m_showSerial);
    tabsRow->addWidget(m_showNetwork);
    tabsRow->addWidget(m_showFtp);
    tabsRow->addWidget(m_showFirmware);
    tabsRow->addStretch();
    grid->addLayout(tabsRow, 4, 1);

    root->addWidget(group, 0, Qt::AlignTop | Qt::AlignLeft);
    root->addStretch();
    retranslateUi();
}

void SettingsWidget::loadSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("App"));
    int tab = settings.value(QStringLiteral("startupTab"), 0).toInt();
    // 程序下载从序号 4 挪到 3，设置从 3 挪到 4。已保存的选择按页面含义改一次，避免下次再对调。
    if (!settings.value(QStringLiteral("startupTabMoved"), false).toBool()) {
        if (tab == 3)
            tab = 4;
        else if (tab == 4)
            tab = 3;
        settings.setValue(QStringLiteral("startupTab"), tab);
        settings.setValue(QStringLiteral("startupTabMoved"), true);
    }
    // FTP 插到序号 2，固件以及后面的页各加 1。已保存的启动页按这个改一次。
    if (!settings.value(QStringLiteral("startupTabFtp"), false).toBool()) {
        if (tab >= 2)
            ++tab;
        settings.setValue(QStringLiteral("startupTab"), tab);
        settings.setValue(QStringLiteral("startupTabFtp"), true);
    }
    const int idx = m_startupCombo->findData(tab);
    if (idx >= 0)
        m_startupCombo->setCurrentIndex(idx);
    m_logDirEdit->setText(settings.value(QStringLiteral("logDir")).toString());
    m_firmwareDirEdit->setText(settings.value(QStringLiteral("firmwareDir")).toString());
    m_showSerial->setChecked(settings.value(QStringLiteral("showSerial"), false).toBool());
    m_showNetwork->setChecked(settings.value(QStringLiteral("showNetwork"), false).toBool());
    m_showFtp->setChecked(settings.value(QStringLiteral("showFtp"), false).toBool());
    m_showFirmware->setChecked(settings.value(QStringLiteral("showFirmware"), false).toBool());
    syncStartupCombo();
    const QString language = settings.value(QStringLiteral("language"), QStringLiteral("en")).toString();
    const int langIdx = m_languageCombo->findData(language);
    if (langIdx >= 0)
        m_languageCombo->setCurrentIndex(langIdx);
    settings.endGroup();
}

void SettingsWidget::saveSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("App"));
    settings.setValue(QStringLiteral("startupTab"), startupTab());
    settings.setValue(QStringLiteral("language"), m_languageCombo->currentData().toString());
    settings.setValue(QStringLiteral("logDir"), m_logDirEdit->text().trimmed());
    settings.setValue(QStringLiteral("firmwareDir"), m_firmwareDirEdit->text().trimmed());
    settings.setValue(QStringLiteral("showSerial"), m_showSerial->isChecked());
    settings.setValue(QStringLiteral("showNetwork"), m_showNetwork->isChecked());
    settings.setValue(QStringLiteral("showFtp"), m_showFtp->isChecked());
    settings.setValue(QStringLiteral("showFirmware"), m_showFirmware->isChecked());
    settings.endGroup();
}

void SettingsWidget::syncStartupCombo()
{
    auto *model = qobject_cast<QStandardItemModel *>(m_startupCombo->model());
    if (!model)
        return;
    const bool enabled[] = {
        m_showSerial->isChecked(),
        m_showNetwork->isChecked(),
        m_showFtp->isChecked(),
        m_showFirmware->isChecked(),
        true,
        true,
    };
    for (int i = 0; i < 6; ++i) {
        if (QStandardItem *item = model->item(i))
            item->setEnabled(enabled[i]);
    }
    const int current = m_startupCombo->currentIndex();
    if (current >= 0 && current < 6 && enabled[current])
        return;
    for (int i = 0; i < 6; ++i) {
        if (enabled[i]) {
            m_startupCombo->setCurrentIndex(i);
            break;
        }
    }
}

void SettingsWidget::browseLogDir()
{
    const QString current = m_logDirEdit->text().trimmed();
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Log Directory"),
        current.isEmpty() ? QDir::currentPath() : current);
    if (dir.isEmpty())
        return;
    m_logDirEdit->setText(dir);
    saveSettings();
}

void SettingsWidget::browseFirmwareDir()
{
    const QString current = m_firmwareDirEdit->text().trimmed();
    const QString start = current.isEmpty()
                              ? QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
                              : current;
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Select Firmware Directory"), start);
    if (dir.isEmpty())
        return;
    m_firmwareDirEdit->setText(dir);
    saveSettings();
}

void SettingsWidget::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QWidget::changeEvent(event);
}

void SettingsWidget::retranslateUi()
{
    const int startup = m_startupCombo->currentIndex();
    const int language = m_languageCombo->currentIndex();
    m_startupCombo->blockSignals(true);
    m_languageCombo->blockSignals(true);
    m_group->setTitle(tr("General"));
    m_startupLabel->setText(tr("Startup Page:"));
    m_startupCombo->setItemText(0, tr("Serial"));
    m_startupCombo->setItemText(1, tr("Network"));
    m_startupCombo->setItemText(2, tr("FTP / TFTP"));
    m_startupCombo->setItemText(3, tr("Firmware"));
    m_startupCombo->setItemText(4, tr("Program Download"));
    m_startupCombo->setItemText(5, tr("Settings"));
    m_languageLabel->setText(tr("Language:"));
    m_languageCombo->setItemText(0, tr("English"));
    m_languageCombo->setItemText(1, tr("Chinese"));
    m_logDirLabel->setText(tr("Log Directory:"));
    m_logDirEdit->setPlaceholderText(QDir::toNativeSeparators(
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("log"))));
    m_browseBtn->setText(tr("Browse"));
    m_firmwareDirLabel->setText(tr("Firmware Directory:"));
    m_firmwareDirEdit->setPlaceholderText(tr("Downloads"));
    m_firmwareBrowseBtn->setText(tr("Browse"));
    m_tabsLabel->setText(tr("Visible Tabs:"));
    m_showSerial->setText(tr("Serial"));
    m_showNetwork->setText(tr("Network"));
    m_showFtp->setText(tr("FTP / TFTP"));
    m_showFirmware->setText(tr("Firmware"));
    m_startupCombo->setCurrentIndex(startup);
    m_languageCombo->setCurrentIndex(language);
    m_startupCombo->blockSignals(false);
    m_languageCombo->blockSignals(false);
}

void SettingsWidget::applyLanguage()
{
    AppLanguage::setLanguage(m_languageCombo->currentData().toString());
    saveSettings();
}
