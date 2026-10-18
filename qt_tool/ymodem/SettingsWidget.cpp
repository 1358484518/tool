#include "SettingsWidget.h"
#include "AppLanguage.h"

#include <QComboBox>
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

QString SettingsWidget::logFilePath(const QString &fileName)
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("App"));
    const QString dir = settings.value(QStringLiteral("logDir")).toString();
    settings.endGroup();
    if (dir.isEmpty() || !QDir(dir).exists())
        return fileName;
    return QDir(dir).filePath(fileName);
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
    group->setMaximumWidth(560);
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
    const int idx = m_startupCombo->findData(tab);
    if (idx >= 0)
        m_startupCombo->setCurrentIndex(idx);
    m_logDirEdit->setText(settings.value(QStringLiteral("logDir")).toString());
    m_firmwareDirEdit->setText(settings.value(QStringLiteral("firmwareDir")).toString());
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
    settings.endGroup();
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
    m_startupCombo->setItemText(2, tr("Firmware"));
    m_startupCombo->setItemText(3, tr("Program Download"));
    m_startupCombo->setItemText(4, tr("Settings"));
    m_languageLabel->setText(tr("Language:"));
    m_languageCombo->setItemText(0, tr("English"));
    m_languageCombo->setItemText(1, tr("Chinese"));
    m_logDirLabel->setText(tr("Log Directory:"));
    m_logDirEdit->setPlaceholderText(tr("Current directory"));
    m_browseBtn->setText(tr("Browse"));
    m_firmwareDirLabel->setText(tr("Firmware Directory:"));
    m_firmwareDirEdit->setPlaceholderText(tr("Downloads"));
    m_firmwareBrowseBtn->setText(tr("Browse"));
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
