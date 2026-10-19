#include "FirmwareWidget.h"
#include "EvlinkLoginDialog.h"
#include "FileHashWidget.h"
#include "FirmwarePackWidget.h"
#include "FirmwarePassword.h"
#include "SettingsWidget.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSettings>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QStyleOptionButton>
#include <QTableView>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

namespace {

const char kDefaultPage[] = "https://chargers.evselink.top/firmwares";  // 网站上的固件页
const int kPageSize = 20;  // 和网站固件页默认每页条数一致

struct FirmwarePage
{
    bool ok = false;             // JSON 能解析，且业务码成功
    QString error;               // 失败时给界面看的说明
    int pageNo = 1;              // 这一页的页码
    int pages = 1;               // 总页数
    int total = 0;               // 固件总数
    QVector<FirmwareEntry> items;  // 这一页的行
};

bool contentRangeStart(const QByteArray &header, qint64 *start, qint64 *total)  // 解析 Content-Range 的起始字节和总长
{
    const QByteArray prefix("bytes ");
    if (!header.startsWith(prefix))
        return false;
    const int dash = header.indexOf('-', prefix.size());
    const int slash = header.indexOf('/', dash);
    if (dash < 0 || slash < 0)
        return false;
    bool okStart = false;
    bool okTotal = false;
    *start = header.mid(prefix.size(), dash - prefix.size()).toLongLong(&okStart);
    *total = header.mid(slash + 1).toLongLong(&okTotal);
    return okStart && okTotal && *start >= 0 && *total > *start;
}

QString formatSize(qint64 bytes)  // 字节数换成 B/KB/MB
{
    if (bytes < 1024)
        return QString::number(bytes) + QStringLiteral(" B");
    if (bytes < 1024 * 1024)
        return QString::number(bytes / 1024.0, 'f', 1) + QStringLiteral(" KB");
    return QString::number(bytes / 1024.0 / 1024.0, 'f', 2) + QStringLiteral(" MB");
}

QString formatTime(const QString &raw)  // 接口的 ISO 时间换成本地时间
{
    QDateTime time = QDateTime::fromString(raw, Qt::ISODateWithMs);
    if (!time.isValid())
        time = QDateTime::fromString(raw, Qt::ISODate);
    if (!time.isValid())
        return raw;
    return time.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

QString firstString(const QJsonObject &object, const QStringList &keys)  // 按键名顺序取第一个非空字符串
{
    for (const QString &key : keys) {
        const QString text = object.value(key).toString().trimmed();
        if (!text.isEmpty())
            return text;
    }
    return QString();
}

QString urlInValue(const QJsonValue &value, int depth)  // 在对象里找 http 链接或站内路径
{
    if (depth > 4 || value.isNull() || value.isUndefined())
        return QString();
    if (value.isString()) {
        const QString text = value.toString().trimmed();
        if (text.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
            || text.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)
            || text.startsWith(QLatin1Char('/')))
            return text;
        return QString();
    }
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        const QString direct = firstString(object, {QStringLiteral("url"), QStringLiteral("downloadUrl"),
                                                     QStringLiteral("download_url"), QStringLiteral("fileUrl"),
                                                     QStringLiteral("file_url"), QStringLiteral("location"),
                                                     QStringLiteral("path"), QStringLiteral("link")});
        if (!direct.isEmpty())
            return direct;
        for (auto it = object.begin(); it != object.end(); ++it) {
            const QString nested = urlInValue(it.value(), depth + 1);
            if (!nested.isEmpty())
                return nested;
        }
    }
    return QString();
}

QJsonArray listArray(const QJsonObject &root)  // 网站把列表放在 data.data，不在 data 本身
{
    const QJsonValue data = root.value(QStringLiteral("data"));
    if (data.isArray())
        return data.toArray();
    if (data.isObject()) {
        const QJsonObject bag = data.toObject();
        if (bag.value(QStringLiteral("data")).isArray())
            return bag.value(QStringLiteral("data")).toArray();
        const QStringList keys = {QStringLiteral("list"), QStringLiteral("records"), QStringLiteral("items"),
                                  QStringLiteral("rows"), QStringLiteral("firmwares"), QStringLiteral("content")};
        for (const QString &key : keys) {
            if (bag.value(key).isArray())
                return bag.value(key).toArray();
        }
    }
    return QJsonArray();
}

FirmwarePage parseFirmwarePage(const QByteArray &body, const QUrl &origin)  // 把一页固件 JSON 收成表格行
{
    FirmwarePage page;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        page.error = QString::fromUtf8(body.left(180));
        return page;
    }
    const QJsonObject root = doc.object();
    const QString code = root.value(QStringLiteral("code")).toString();
    if (!code.isEmpty() && code != QLatin1String("0000")) {
        page.error = root.value(QStringLiteral("message")).toString();
        if (page.error.isEmpty())
            page.error = code;
        return page;
    }
    const QJsonObject bag = root.value(QStringLiteral("data")).toObject();
    page.pageNo = bag.value(QStringLiteral("pageNo")).toInt(1);
    page.pages = qMax(1, bag.value(QStringLiteral("pages")).toInt(1));
    page.total = bag.value(QStringLiteral("total")).toInt();
    const QJsonArray rows = listArray(root);
    for (const QJsonValue &value : rows) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        FirmwareEntry entry;
        entry.name = firstString(object, {QStringLiteral("filename"), QStringLiteral("fileName"),
                                           QStringLiteral("name")});
        entry.version = firstString(object, {QStringLiteral("version")});
        entry.id = object.value(QStringLiteral("id")).toInt();
        const qint64 bytes = object.value(QStringLiteral("fileSize")).toVariant().toLongLong();
        entry.bytes = bytes;
        if (bytes > 0)
            entry.size = formatSize(bytes);
        entry.created = formatTime(object.value(QStringLiteral("createdAt")).toString());
        QString link = firstString(object, {QStringLiteral("fileUrl"), QStringLiteral("file_url")});
        if (link.isEmpty())
            link = urlInValue(object, 0);
        if (!link.isEmpty()) {
            const QUrl resolved = origin.resolved(QUrl(link));
            entry.url = resolved.isValid() ? resolved.toString() : link;
        }
        if (entry.name.isEmpty())
            entry.name = QFileInfo(QUrl(entry.url).path()).fileName();
        if (entry.name.isEmpty() && entry.url.isEmpty())
            continue;
        page.items.append(entry);
    }
    if (page.total <= 0)
        page.total = page.items.size();
    page.ok = true;
    return page;
}

QByteArray tokenFromReply(QNetworkReply *reply, const QByteArray &body)  // 登录令牌优先用响应头
{
    const QByteArray header = reply->rawHeader("Authorization");
    if (!header.trimmed().isEmpty())
        return header.trimmed();
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject())
        return QByteArray();
    const QJsonObject root = doc.object();
    QJsonValue token = root.value(QStringLiteral("token"));
    if (!token.isString() && root.value(QStringLiteral("data")).isObject())
        token = root.value(QStringLiteral("data")).toObject().value(QStringLiteral("token"));
    if (!token.isString() && root.value(QStringLiteral("data")).isString())
        token = root.value(QStringLiteral("data"));
    QString text = token.toString().trimmed();
    if (text.isEmpty())
        return QByteArray();
    // 头里的值原样回传。JSON 里如果只给了裸令牌，网站请求时要带 Bearer。
    if (!text.contains(QLatin1Char(' ')))
        text.prepend(QStringLiteral("Bearer "));
    return text.toUtf8();
}

/**
 * 下载列和删除列各画一个按钮。点下去交给固件页下载或删除。
 */
class FirmwareDownloadDelegate : public QStyledItemDelegate
{
public:
    explicit FirmwareDownloadDelegate(FirmwareWidget *owner, QObject *parent = nullptr)  // 记住谁来处理下载和删除
        : QStyledItemDelegate(parent)
        , m_owner(owner)
    {
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override  // 画这一格的按钮
    {
        const QString label = index.data(Qt::DisplayRole).toString();
        if (label.isEmpty())
            return;
        QStyleOptionButton button;
        button.rect = option.rect.adjusted(6, 4, -6, -4);
        button.text = label;
        button.state = QStyle::State_Enabled;
        if (m_pressed == index)
            button.state |= QStyle::State_Sunken;
        QApplication::style()->drawControl(QStyle::CE_PushButton, &button, painter);
    }

    bool editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option,
                     const QModelIndex &index) override  // 松开鼠标时下载或删除这一行
    {
        Q_UNUSED(model)
        if (index.data(Qt::DisplayRole).toString().isEmpty())
            return false;
        const QRect rect = option.rect.adjusted(6, 4, -6, -4);
        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (rect.contains(mouse->pos())) {
                m_pressed = index;
                return true;
            }
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            const bool hit = m_pressed == index && rect.contains(mouse->pos());
            m_pressed = QModelIndex();
            if (hit && m_owner) {
                if (index.column() == 6)
                    m_owner->deleteRow(index.row());
                else
                    m_owner->downloadRow(index.row());
            }
            return true;
        }
        return false;
    }

private:
    FirmwareWidget *m_owner = nullptr;  // 固件页，负责真正下载和删除
    QModelIndex m_pressed;              // 按下但还没松开的那一格
};

} // namespace

/**
 * 上传固件对话框，字段和网站上的上传框一致。
 * 签名证书可以不选。充电桩型号可以有多行，空行不提交。
 */
class FirmwareUploadDialog : public QDialog
{
public:
    explicit FirmwareUploadDialog(QWidget *parent);  // 建上传表单，先放一行空的型号

    void setCertificates(const QList<QPair<int, QString>> &certs);  // 用 id 和名称填证书下拉
    QString filePath() const;                                        // 要上传的本地文件
    QString version() const;                                         // 固件版本
    QString certificateId() const;                                   // 选中的证书 id，没选就是空
    QJsonArray models() const;                                       // 非空的厂商/型号，交给接口的 models 字段

protected:
    void changeEvent(QEvent *event) override;  // 语言切换时刷新框里的文字

private:
    void retranslateUi();                          // 按当前语言重写框内文字
    void addModelRow();                            // 再加一行厂商和型号
    void browseFile();                             // 选要上传的文件
    void tryAccept();                              // 文件和版本都有才关掉对话框

    QLabel *m_fileLabel = nullptr;        // 请选择文件
    QLabel *m_versionLabel = nullptr;     // 固件版本
    QLabel *m_certLabel = nullptr;        // 签名证书，可选
    QLabel *m_modelLabel = nullptr;       // 充电桩型号
    QLabel *m_hint = nullptr;             // 缺文件或版本时的提示
    QLineEdit *m_fileEdit = nullptr;      // 本地文件路径
    QLineEdit *m_versionEdit = nullptr;   // 版本号
    QComboBox *m_certCombo = nullptr;     // 第一项是不选证书
    QPushButton *m_browseBtn = nullptr;   // 选择文件
    QPushButton *m_addBtn = nullptr;      // 增加一行型号
    QPushButton *m_uploadBtn = nullptr;   // 确认上传
    QPushButton *m_cancelBtn = nullptr;   // 关闭对话框
    QVBoxLayout *m_modelRows = nullptr;   // 厂商、型号各一行
    /** 上传框里的一行充电桩型号。厂商和型号都空则不提交。 */
    struct ModelRow
    {
        QLineEdit *vendor = nullptr;    // 厂商
        QLineEdit *model = nullptr;     // 型号
        QPushButton *remove = nullptr;  // 去掉这一行
    };
    QVector<ModelRow> m_rows;  // 和 m_modelRows 里的行一一对应
};

FirmwareUploadDialog::FirmwareUploadDialog(QWidget *parent)
    : QDialog(parent)
{
    setModal(true);
    setMinimumWidth(460);
    auto *root = new QVBoxLayout(this);
    root->setSpacing(8);

    auto *fileRow = new QHBoxLayout();
    m_fileLabel = new QLabel(this);
    m_fileEdit = new QLineEdit(this);
    m_browseBtn = new QPushButton(this);
    fileRow->addWidget(m_fileEdit, 1);
    fileRow->addWidget(m_browseBtn);

    m_versionLabel = new QLabel(this);
    m_versionEdit = new QLineEdit(this);
    m_certLabel = new QLabel(this);
    m_certCombo = new QComboBox(this);
    m_modelLabel = new QLabel(this);
    m_addBtn = new QPushButton(this);
    auto *modelHead = new QHBoxLayout();
    modelHead->addWidget(m_modelLabel);
    modelHead->addStretch();
    modelHead->addWidget(m_addBtn);
    m_modelRows = new QVBoxLayout();
    m_modelRows->setSpacing(4);

    m_hint = new QLabel(this);
    m_hint->setWordWrap(true);
    auto *buttons = new QHBoxLayout();
    buttons->addStretch();
    m_cancelBtn = new QPushButton(this);
    m_uploadBtn = new QPushButton(this);
    m_uploadBtn->setDefault(true);
    buttons->addWidget(m_cancelBtn);
    buttons->addWidget(m_uploadBtn);

    root->addWidget(m_fileLabel);
    root->addLayout(fileRow);
    root->addWidget(m_versionLabel);
    root->addWidget(m_versionEdit);
    root->addWidget(m_certLabel);
    root->addWidget(m_certCombo);
    root->addLayout(modelHead);
    root->addLayout(m_modelRows);
    root->addWidget(m_hint);
    root->addLayout(buttons);

    connect(m_browseBtn, &QPushButton::clicked, this, [this]() { browseFile(); });
    connect(m_addBtn, &QPushButton::clicked, this, [this]() { addModelRow(); });
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_uploadBtn, &QPushButton::clicked, this, [this]() { tryAccept(); });
    addModelRow();
    retranslateUi();
}

void FirmwareUploadDialog::setCertificates(const QList<QPair<int, QString>> &certs)
{
    const QString current = m_certCombo->currentData().toString();
    m_certCombo->clear();
    m_certCombo->addItem(tr("None"), QString());
    for (const QPair<int, QString> &cert : certs) {
        const QString name = cert.second.isEmpty() ? QString::number(cert.first) : cert.second;
        m_certCombo->addItem(name, QString::number(cert.first));
    }
    const int index = m_certCombo->findData(current);
    if (index >= 0)
        m_certCombo->setCurrentIndex(index);
}

QString FirmwareUploadDialog::filePath() const
{
    return m_fileEdit->text().trimmed();
}

QString FirmwareUploadDialog::version() const
{
    return m_versionEdit->text().trimmed();
}

QString FirmwareUploadDialog::certificateId() const
{
    return m_certCombo->currentData().toString();
}

QJsonArray FirmwareUploadDialog::models() const
{
    QJsonArray rows;
    for (const ModelRow &row : m_rows) {
        const QString vendor = row.vendor->text().trimmed();
        const QString model = row.model->text().trimmed();
        if (vendor.isEmpty() && model.isEmpty())
            continue;
        QJsonObject item;
        item.insert(QStringLiteral("vendor"), vendor);
        item.insert(QStringLiteral("model"), model);
        rows.append(item);
    }
    return rows;
}

void FirmwareUploadDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QDialog::changeEvent(event);
}

void FirmwareUploadDialog::retranslateUi()
{
    setWindowTitle(tr("Upload Firmware"));
    m_fileLabel->setText(tr("Please choose a file"));
    m_browseBtn->setText(tr("Browse"));
    m_versionLabel->setText(tr("Firmware Version:"));
    m_certLabel->setText(tr("Signing Certificate (Optional):"));
    m_modelLabel->setText(tr("Charge Point Models"));
    m_addBtn->setText(tr("Add"));
    m_cancelBtn->setText(tr("Cancel"));
    m_uploadBtn->setText(tr("Upload"));
    if (m_certCombo->count() > 0 && m_certCombo->itemData(0).toString().isEmpty())
        m_certCombo->setItemText(0, tr("None"));
    for (const ModelRow &row : m_rows) {
        row.vendor->setPlaceholderText(tr("Vendor"));
        row.model->setPlaceholderText(tr("Model"));
        row.remove->setText(tr("Del"));
    }
}

void FirmwareUploadDialog::addModelRow()
{
    auto *line = new QWidget(this);
    auto *row = new QHBoxLayout(line);
    row->setContentsMargins(0, 0, 0, 0);
    ModelRow item;
    item.vendor = new QLineEdit(line);
    item.model = new QLineEdit(line);
    item.remove = new QPushButton(line);
    item.vendor->setPlaceholderText(tr("Vendor"));
    item.model->setPlaceholderText(tr("Model"));
    item.remove->setText(tr("Del"));
    row->addWidget(item.vendor, 1);
    row->addWidget(item.model, 1);
    row->addWidget(item.remove);
    m_modelRows->addWidget(line);
    m_rows.append(item);
    connect(item.remove, &QPushButton::clicked, this, [this, line]() {
        if (m_rows.size() <= 1) {
            m_rows.first().vendor->clear();
            m_rows.first().model->clear();
            return;
        }
        for (int i = 0; i < m_rows.size(); ++i) {
            if (m_rows.at(i).vendor->parentWidget() != line)
                continue;
            m_rows.removeAt(i);
            line->deleteLater();
            break;
        }
    });
}

void FirmwareUploadDialog::browseFile()
{
    const QString current = m_fileEdit->text().trimmed();
    const QString start = current.isEmpty()
                              ? QFileInfo(SettingsWidget::firmwareFilePath(QStringLiteral("firmware.bin"))).absolutePath()
                              : current;
    const QString path = QFileDialog::getOpenFileName(this, tr("Please choose a file"), start, tr("All Files (*)"));
    if (!path.isEmpty())
        m_fileEdit->setText(path);
}

void FirmwareUploadDialog::tryAccept()
{
    if (m_fileEdit->text().trimmed().isEmpty() || !QFileInfo::exists(m_fileEdit->text().trimmed())) {
        m_hint->setText(tr("Please choose a file"));
        return;
    }
    if (m_versionEdit->text().trimmed().isEmpty()) {
        m_hint->setText(tr("Firmware version is required"));
        return;
    }
    m_hint->clear();
    accept();
}

FirmwareListModel::FirmwareListModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int FirmwareListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_items.size();
}

int FirmwareListModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return 7;
}

QVariant FirmwareListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return QVariant();
    if (role != Qt::DisplayRole && role != Qt::ToolTipRole)
        return QVariant();
    const FirmwareEntry &entry = m_items.at(index.row());
    switch (index.column()) {
    case 0: return entry.name;
    case 1: return entry.version;
    case 2: return entry.size;
    case 3: return entry.created;
    case 4: return entry.url;
    case 5:
        if (role == Qt::DisplayRole && !entry.url.isEmpty())
            return tr("Download");
        break;
    case 6:
        if (role == Qt::DisplayRole && entry.id > 0)
            return tr("Delete");
        break;
    default:
        break;
    }
    return QVariant();
}

QVariant FirmwareListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QVariant();
    switch (section) {
    case 0: return tr("Filename");
    case 1: return tr("Version");
    case 2: return tr("Size");
    case 3: return tr("Created");
    case 4: return tr("Link");
    case 5: return tr("Download");
    case 6: return tr("Delete");
    default: return QVariant();
    }
}

void FirmwareListModel::setItems(const QVector<FirmwareEntry> &items)
{
    beginResetModel();
    m_items = items;
    endResetModel();
}

void FirmwareListModel::refreshText()
{
    emit headerDataChanged(Qt::Horizontal, 0, columnCount() - 1);
    if (rowCount() > 0)
        emit dataChanged(index(0, 5), index(rowCount() - 1, 6));
}

QString FirmwareListModel::nameAt(int row) const
{
    if (row < 0 || row >= m_items.size())
        return QString();
    return m_items.at(row).name;
}

QString FirmwareListModel::urlAt(int row) const
{
    if (row < 0 || row >= m_items.size())
        return QString();
    return m_items.at(row).url;
}

qint64 FirmwareListModel::bytesAt(int row) const
{
    if (row < 0 || row >= m_items.size())
        return 0;
    return m_items.at(row).bytes;
}

int FirmwareListModel::idAt(int row) const
{
    if (row < 0 || row >= m_items.size())
        return 0;
    return m_items.at(row).id;
}

FirmwareWidget::FirmwareWidget(QWidget *parent)
    : QWidget(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    setupUi();
    loadSettings();
    connect(m_refreshBtn, &QPushButton::clicked, this, &FirmwareWidget::refresh);
    connect(m_searchBtn, &QPushButton::clicked, this, &FirmwareWidget::searchByName);
    connect(m_nameEdit, &QLineEdit::returnPressed, this, &FirmwareWidget::searchByName);
    connect(m_uploadBtn, &QPushButton::clicked, this, &FirmwareWidget::beginUpload);
    connect(m_prevBtn, &QPushButton::clicked, this, [this]() { showPage(m_page - 1); });
    connect(m_nextBtn, &QPushButton::clicked, this, [this]() { showPage(m_page + 1); });
    connect(m_urlEdit, &QLineEdit::editingFinished, this, &FirmwareWidget::saveSettings);
    connect(m_userEdit, &QLineEdit::editingFinished, this, &FirmwareWidget::saveSettings);
    connect(m_passwordEdit, &QLineEdit::editingFinished, this, &FirmwareWidget::saveSettings);
    connect(m_fileHash, &FileHashWidget::failed, this, &FirmwareWidget::setStatus);
    connect(m_fileHash, &FileHashWidget::progress, this, [this](int percent) {
        setStatus(tr("Progress: %1%").arg(percent));
    });
    connect(m_fileHash, &FileHashWidget::completed, this, [this]() {
        setStatus(tr("Completed"));
    });
    connect(m_pack, &FirmwarePackWidget::message, this, &FirmwareWidget::setStatus);
}

FirmwareWidget::~FirmwareWidget()
{
    saveSettings();
    // 对话框跟着本页销毁时不要再进上传槽。
    if (m_uploadDialog)
        m_uploadDialog->disconnect(this);
    abortReply();
    abortDownload();
}

void FirmwareWidget::setupUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    m_firmwareGroup = new QGroupBox(this);
    auto *firmwareLayout = new QVBoxLayout(m_firmwareGroup);
    firmwareLayout->setSpacing(8);
    firmwareLayout->setContentsMargins(8, 14, 8, 8);

    auto *form = new QGridLayout();
    form->setHorizontalSpacing(8);
    form->setVerticalSpacing(6);
    m_urlLabel = new QLabel(this);
    m_urlEdit = new QLineEdit(this);
    m_urlEdit->setText(QString::fromLatin1(kDefaultPage));
    m_refreshBtn = new QPushButton(this);
    m_uploadBtn = new QPushButton(this);
    form->addWidget(m_urlLabel, 0, 0);
    form->addWidget(m_urlEdit, 0, 1, 1, 3);
    form->addWidget(m_refreshBtn, 0, 4);
    form->addWidget(m_uploadBtn, 0, 5);

    m_userLabel = new QLabel(this);
    m_userEdit = new QLineEdit(this);
    m_passwordLabel = new QLabel(this);
    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    form->addWidget(m_userLabel, 1, 0);
    form->addWidget(m_userEdit, 1, 1);
    form->addWidget(m_passwordLabel, 1, 2);
    form->addWidget(m_passwordEdit, 1, 3);

    m_nameLabel = new QLabel(this);
    m_nameEdit = new QLineEdit(this);
    m_searchBtn = new QPushButton(this);
    form->addWidget(m_nameLabel, 2, 0);
    form->addWidget(m_nameEdit, 2, 1, 1, 4);
    form->addWidget(m_searchBtn, 2, 5);
    form->setColumnStretch(1, 1);
    form->setColumnStretch(3, 1);
    firmwareLayout->addLayout(form);

    m_model = new FirmwareListModel(this);
    m_table = new QTableView(this);
    m_table->setModel(m_model);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setWordWrap(false);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Fixed);
    m_table->setColumnWidth(6, 72);
    auto *actions = new FirmwareDownloadDelegate(this, m_table);
    m_table->setItemDelegateForColumn(5, actions);
    m_table->setItemDelegateForColumn(6, actions);

    firmwareLayout->addWidget(m_table, 1);

    auto *pager = new QHBoxLayout();
    pager->setSpacing(8);
    m_prevBtn = new QPushButton(this);
    m_nextBtn = new QPushButton(this);
    m_pageLabel = new QLabel(this);
    m_prevBtn->setEnabled(false);
    m_nextBtn->setEnabled(false);
    pager->addWidget(m_prevBtn);
    pager->addWidget(m_pageLabel);
    pager->addWidget(m_nextBtn);
    pager->addStretch();
    firmwareLayout->addLayout(pager);
    root->addWidget(m_firmwareGroup, 1);

    m_fileHash = new FileHashWidget(this);
    m_pack = new FirmwarePackWidget(this);
    auto *tools = new QHBoxLayout();
    tools->setSpacing(6);
    tools->addWidget(m_fileHash, 1);
    tools->addWidget(m_pack, 1);
    root->addLayout(tools);

    // 状态放在页底，不要插在固件列表和校验、组包中间。
    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    root->addWidget(m_statusLabel);
    retranslateUi();
}

void FirmwareWidget::retranslateUi()
{
    m_urlLabel->setText(tr("Address:"));
    m_urlEdit->setPlaceholderText(QString::fromLatin1(kDefaultPage));
    m_refreshBtn->setText(tr("Refresh"));
    m_uploadBtn->setText(tr("Upload"));
    m_userLabel->setText(tr("Username:"));
    m_userEdit->setPlaceholderText(tr("Email"));
    m_passwordLabel->setText(tr("Password:"));
    m_nameLabel->setText(tr("Filename:"));
    m_nameEdit->setPlaceholderText(tr("Firmware name"));
    m_searchBtn->setText(tr("Search"));
    m_firmwareGroup->setTitle(tr("Firmware"));
    m_prevBtn->setText(tr("Previous"));
    m_nextBtn->setText(tr("Next"));
    updatePager();
    m_model->refreshText();
    fitDownloadColumn();
    m_table->viewport()->update();
}

void FirmwareWidget::fitDownloadColumn()
{
    QStyleOptionButton button;
    button.fontMetrics = QFontMetrics(m_table->font());
    button.text = tr("Download");
    const QSize textSize = button.fontMetrics.size(Qt::TextSingleLine, button.text);
    const int buttonWidth = QApplication::style()->sizeFromContents(
        QStyle::CT_PushButton, &button, textSize, m_table).width();
    // 绘制时单元格左右各让出 6 像素。列宽取按钮和表头里较宽的那个。
    const int headerWidth = m_table->horizontalHeader()->sectionSizeHint(5);
    m_table->setColumnWidth(5, qMax(buttonWidth + 12, headerWidth));
}

void FirmwareWidget::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QWidget::changeEvent(event);
}

void FirmwareWidget::loadSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("Firmware"));
    const QString url = settings.value(QStringLiteral("url"), QString::fromLatin1(kDefaultPage)).toString();
    m_urlEdit->setText(url.isEmpty() ? QString::fromLatin1(kDefaultPage) : url);
    const QString email = settings.value(QStringLiteral("username")).toString();
    m_userEdit->setText(email);
    m_passwordEdit->setText(FirmwarePassword::decrypt(email, settings.value(QStringLiteral("password")).toString()));
    m_appliedSearch = settings.value(QStringLiteral("search")).toString().trimmed();
    m_nameEdit->setText(m_appliedSearch);
    settings.endGroup();
}

void FirmwareWidget::saveSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("Firmware"));
    const QString email = m_userEdit->text().trimmed();
    const QString password = m_passwordEdit->text();
    settings.setValue(QStringLiteral("url"), m_urlEdit->text().trimmed());
    settings.setValue(QStringLiteral("username"), email);
    settings.setValue(QStringLiteral("search"), m_nameEdit->text().trimmed());
    const QString cipher = FirmwarePassword::encrypt(email, password);
    // 没有邮箱就组不出密钥，只丢掉旧密文，不清空输入框里这次还在用的密码。
    if (cipher.isEmpty())
        settings.remove(QStringLiteral("password"));
    else
        settings.setValue(QStringLiteral("password"), cipher);
    settings.endGroup();
    if (m_pack)
        m_pack->saveSettings();
}

QUrl FirmwareWidget::apiRoot() const
{
    QUrl page = QUrl::fromUserInput(m_urlEdit->text().trimmed());
    if (!page.isValid() || page.host().isEmpty())
        return QUrl();
    QString path = page.path();
    const int admin = path.indexOf(QLatin1String("/admin-api/"));
    if (admin >= 0)
        path = path.left(admin);
    else
        path.clear();
    // 末尾带斜杠，后面 resolved("firmwares") 才会接在 v1 后面，而不是把 v1 换掉。
    page.setPath(path + QStringLiteral("/admin-api/v1/"));
    page.setQuery(QString());
    page.setFragment(QString());
    return page;
}

void FirmwareWidget::applyAuth(QNetworkRequest *request) const
{
    if (m_token.isEmpty())
        return;
    // 令牌只发给当前这个站点，避免下载外链时把登录头带出去。
    if (request->url().host().compare(apiRoot().host(), Qt::CaseInsensitive) != 0)
        return;
    request->setRawHeader("Authorization", m_token);
}

void FirmwareWidget::setStatus(const QString &text)
{
    m_statusLabel->setText(text);
}

void FirmwareWidget::abortReply()
{
    if (!m_reply)
        return;
    m_reply->disconnect(this);
    m_reply->abort();
    m_reply->deleteLater();
    m_reply = nullptr;
}

void FirmwareWidget::updatePager()
{
    m_pageLabel->setText(tr("Page %1 / %2, %3 in total").arg(m_page).arg(m_pages).arg(m_total));
    m_prevBtn->setEnabled(m_page > 1 && !m_reply);
    m_nextBtn->setEnabled(m_page < m_pages && !m_reply);
}

bool FirmwareWidget::isSessionOpen() const
{
    return m_sessionOpen;
}

bool FirmwareWidget::isGateLoginPending() const
{
    return m_gateLogin;
}

void FirmwareWidget::trySavedLogin()
{
    // 拷走的程序没有这份账号。不要匿名拉列表，两个页保持打不开。
    if (m_userEdit->text().trimmed().isEmpty() || m_passwordEdit->text().isEmpty() || !apiRoot().isValid()) {
        emit gateLoginFinished(false, QString());
        return;
    }
    abortReply();
    m_loginForUpload = false;
    m_gateLogin = true;
    setBusy(true);
    startLogin();
}

bool FirmwareWidget::promptLogin(QWidget *parent)
{
    if (m_sessionOpen)
        return true;
    EvlinkLoginDialog dialog(parent);
    dialog.setAccount(m_userEdit->text(), m_passwordEdit->text());
    connect(&dialog, &EvlinkLoginDialog::signInRequested, this, [this](const QString &email, const QString &password) {
        // 登录窗里改过的账号马上落盘，下次启动用这一份。
        m_userEdit->setText(email);
        m_passwordEdit->setText(password);
        saveSettings();
        abortReply();
        m_loginForUpload = false;
        if (!apiRoot().isValid()) {
            m_gateLogin = false;
            setStatus(tr("Invalid address"));
            emit gateLoginFinished(false, tr("Invalid address"));
            return;
        }
        m_gateLogin = true;
        setBusy(true);
        startLogin();
    });
    connect(&dialog, &QDialog::rejected, this, [this]() {
        if (!m_gateLogin)
            return;
        // 用户取消了。丢掉这次请求，不要再报一次登录失败。
        abortReply();
        m_gateLogin = false;
        m_waitingLogin = false;
        setBusy(false);
    });
    connect(this, &FirmwareWidget::gateLoginFinished, &dialog, [&dialog](bool ok, const QString &message) {
        if (!dialog.isVisible())
            return;
        if (ok)
            dialog.accept();
        else
            dialog.showFailure(message);
    });
    return dialog.exec() == QDialog::Accepted && m_sessionOpen;
}

void FirmwareWidget::refresh()
{
    m_appliedSearch = m_nameEdit->text().trimmed();
    saveSettings();
    abortReply();
    m_model->setItems(QVector<FirmwareEntry>());
    m_page = 1;
    m_pages = 1;
    m_total = 0;
    m_token.clear();
    m_waitingLogin = false;
    m_loginForUpload = false;
    updatePager();
    if (!apiRoot().isValid()) {
        setStatus(tr("Invalid address"));
        return;
    }
    setBusy(true);
    if (!m_userEdit->text().trimmed().isEmpty())
        startLogin();
    else
        startList();
}

void FirmwareWidget::searchByName()
{
    m_appliedSearch = m_nameEdit->text().trimmed();
    saveSettings();
    abortReply();
    m_model->setItems(QVector<FirmwareEntry>());
    m_page = 1;
    m_pages = 1;
    m_total = 0;
    updatePager();
    if (!apiRoot().isValid()) {
        setStatus(tr("Invalid address"));
        return;
    }
    setBusy(true);
    // 搜索只换过滤条件。令牌还在就直接拉列表，避免每次都重新登录。
    if (m_token.isEmpty() && !m_userEdit->text().trimmed().isEmpty())
        startLogin();
    else
        startList();
}

void FirmwareWidget::beginUpload()
{
    saveSettings();
    if (!apiRoot().isValid()) {
        setStatus(tr("Invalid address"));
        return;
    }
    if (m_uploadDialog) {
        m_uploadDialog->raise();
        m_uploadDialog->activateWindow();
        return;
    }
    // 列表请求会占着 m_reply。先登录时丢掉它，避免两套回复抢同一个槽。
    if (!m_userEdit->text().trimmed().isEmpty() && m_token.isEmpty()) {
        abortReply();
        m_loginForUpload = true;
        setBusy(true);
        startLogin();
        return;
    }
    openUploadDialog();
}

void FirmwareWidget::openUploadDialog()
{
    m_uploadDialog = new FirmwareUploadDialog(this);
    m_uploadDialog->setAttribute(Qt::WA_DeleteOnClose);
    // finished 比 accepted 先发出。先拿走指针再读字段，避免槽里把对话框清掉。
    connect(m_uploadDialog, &QDialog::finished, this, [this](int result) {
        FirmwareUploadDialog *dialog = m_uploadDialog;
        m_uploadDialog = nullptr;
        if (result == QDialog::Accepted && dialog)
            startUpload(dialog);
    });
    m_uploadDialog->open();
    startCertificates();
}

void FirmwareWidget::setBusy(bool busy)
{
    m_refreshBtn->setEnabled(!busy);
    m_searchBtn->setEnabled(!busy);
    m_uploadBtn->setEnabled(!busy);
}

void FirmwareWidget::showPage(int page)
{
    if (m_reply || page < 1 || page > m_pages || page == m_page)
        return;
    m_page = page;
    setBusy(true);
    updatePager();
    startList();
}

void FirmwareWidget::startLogin()
{
    QNetworkRequest request(apiRoot().resolved(QUrl(QStringLiteral("login"))));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QJsonObject body;
    body.insert(QStringLiteral("email"), m_userEdit->text().trimmed());
    body.insert(QStringLiteral("password"), m_passwordEdit->text());
    m_waitingLogin = true;
    setStatus(tr("Signing in..."));
    m_reply = m_nam->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::finished, this, &FirmwareWidget::onReplyFinished);
}

void FirmwareWidget::startList()
{
    QUrl url = apiRoot().resolved(QUrl(QStringLiteral("firmwares")));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("page_no"), QString::number(m_page));
    query.addQueryItem(QStringLiteral("page_size"), QString::number(kPageSize));
    // 和网站列表一样走 search。空着就不带，服务器返回全部。
    if (!m_appliedSearch.isEmpty())
        query.addQueryItem(QStringLiteral("search"), m_appliedSearch);
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    applyAuth(&request);
    m_waitingLogin = false;
    m_replyKind = ReplyKind::List;
    setStatus(tr("Loading firmware..."));
    m_reply = m_nam->get(request);
    connect(m_reply, &QNetworkReply::finished, this, &FirmwareWidget::onReplyFinished);
}

void FirmwareWidget::startCertificates()
{
    abortReply();
    // 证书名单只是下拉框的可选项，不要把刷新和翻页按住。
    setBusy(false);
    QNetworkRequest request(apiRoot().resolved(QUrl(QStringLiteral("signing-certificates/valid"))));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    applyAuth(&request);
    m_waitingLogin = false;
    m_replyKind = ReplyKind::Certificates;
    m_reply = m_nam->get(request);
    connect(m_reply, &QNetworkReply::finished, this, &FirmwareWidget::onReplyFinished);
}

void FirmwareWidget::startUpload(FirmwareUploadDialog *dialog)
{
    const QString path = dialog->filePath();
    const QString version = dialog->version();
    const QString certId = dialog->certificateId();
    const QJsonArray models = dialog->models();
    auto *file = new QFile(path);
    if (!file->open(QIODevice::ReadOnly)) {
        setStatus(tr("Cannot read file: %1").arg(path));
        delete file;
        return;
    }
    abortReply();
    // 网站把 models 做成 encodeURIComponent(JSON)，空数组也要提交成 %5B%5D。
    const QByteArray encodedModels = QUrl::toPercentEncoding(
        QJsonDocument(models).toJson(QJsonDocument::Compact));
    auto *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    auto addText = [multi](const char *name, const QByteArray &body) {
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QStringLiteral("form-data; name=\"%1\"").arg(QLatin1String(name))));
        part.setBody(body);
        multi->append(part);
    };
    addText("models", encodedModels);
    addText("version", version.toUtf8());
    if (!certId.isEmpty())
        addText("signingCertId", certId.toUtf8());
    QHttpPart filePart;
    m_uploadFileName = QFileInfo(path).fileName();
    const QString fileName = m_uploadFileName;
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QStringLiteral("form-data; name=\"file\"; filename=\"%1\"").arg(fileName)));
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(QStringLiteral("application/octet-stream")));
    file->setParent(multi);
    filePart.setBodyDevice(file);
    multi->append(filePart);

    QNetworkRequest request(apiRoot().resolved(QUrl(QStringLiteral("firmwares"))));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    applyAuth(&request);
    m_waitingLogin = false;
    m_replyKind = ReplyKind::Upload;
    setBusy(true);
    setStatus(tr("Uploading %1").arg(fileName));
    m_reply = m_nam->post(request, multi);
    multi->setParent(m_reply);
    connect(m_reply, &QNetworkReply::finished, this, &FirmwareWidget::onReplyFinished);
}

void FirmwareWidget::applyCertificates(const QByteArray &body)
{
    if (!m_uploadDialog)
        return;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return;
    const QJsonObject root = doc.object();
    const QString code = root.value(QStringLiteral("code")).toString();
    if (!code.isEmpty() && code != QLatin1String("0000"))
        return;
    QList<QPair<int, QString>> certs;
    const QJsonArray rows = listArray(root);
    for (const QJsonValue &value : rows) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        const int id = object.value(QStringLiteral("id")).toInt();
        if (id <= 0)
            continue;
        certs.append(qMakePair(id, object.value(QStringLiteral("name")).toString()));
    }
    m_uploadDialog->setCertificates(certs);
}

void FirmwareWidget::onReplyFinished()
{
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    if (!reply)
        return;
    const QByteArray body = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString error = reply->error() == QNetworkReply::NoError
                              ? QString()
                              : reply->errorString();
    const bool login = m_waitingLogin;
    const ReplyKind kind = m_replyKind;
    const QString actionName = reply->property("firmwareName").toString();
    reply->deleteLater();

    if (!error.isEmpty() || status >= 400) {
        const bool gate = login && m_gateLogin;
        m_gateLogin = false;
        m_loginForUpload = false;
        setBusy(false);
        updatePager();
        const QString detail = error.isEmpty() ? QString::number(status) : error;
        if (login)
            setStatus(tr("Sign-in failed: %1").arg(detail));
        else if (kind == ReplyKind::Upload)
            setStatus(tr("Upload failed: %1").arg(detail));
        else if (kind == ReplyKind::Delete)
            setStatus(tr("Delete failed: %1").arg(detail));
        else if (kind == ReplyKind::List)
            setStatus(tr("Load failed: %1").arg(detail));
        // 页里刷新失败不收回已经放开的页签，只让启动和登录窗这次失败。
        if (gate)
            emit gateLoginFinished(false, m_statusLabel->text());
        return;
    }

    if (login) {
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        const QString code = doc.isObject() ? doc.object().value(QStringLiteral("code")).toString() : QString();
        const QString serverMessage = doc.isObject() ? doc.object().value(QStringLiteral("message")).toString() : QString();
        m_token = tokenFromReply(reply, body);
        const bool tokenOk = !m_token.isEmpty() && (code.isEmpty() || code == QLatin1String("0000"));
        if (m_gateLogin && !tokenOk) {
            m_token.clear();
            m_gateLogin = false;
            m_loginForUpload = false;
            setBusy(false);
            updatePager();
            const QString detail = serverMessage.isEmpty() ? tr("No token") : serverMessage;
            setStatus(tr("Sign-in failed: %1").arg(detail));
            emit gateLoginFinished(false, m_statusLabel->text());
            return;
        }
        if (tokenOk)
            m_sessionOpen = true;
        if (m_gateLogin) {
            m_gateLogin = false;
            emit gateLoginFinished(true, QString());
        }
        if (m_loginForUpload) {
            m_loginForUpload = false;
            setBusy(false);
            openUploadDialog();
            return;
        }
        startList();
        return;
    }

    if (kind == ReplyKind::Certificates) {
        applyCertificates(body);
        updatePager();
        return;
    }

    if (kind == ReplyKind::Upload) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
        const QJsonObject root = doc.object();
        const QString code = root.value(QStringLiteral("code")).toString();
        if (parseError.error != QJsonParseError::NoError || (!code.isEmpty() && code != QLatin1String("0000"))) {
            setBusy(false);
            QString message = root.value(QStringLiteral("message")).toString();
            if (message.isEmpty())
                message = code.isEmpty() ? QString::fromUtf8(body.left(180)) : code;
            setStatus(tr("Upload failed: %1").arg(message));
            return;
        }
        setStatus(tr("Uploaded %1").arg(m_uploadFileName));
        m_page = 1;
        startList();
        return;
    }

    if (kind == ReplyKind::Delete) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
        const QJsonObject root = doc.object();
        const QString code = root.value(QStringLiteral("code")).toString();
        const QString name = actionName;
        if (parseError.error != QJsonParseError::NoError || (!code.isEmpty() && code != QLatin1String("0000"))) {
            setBusy(false);
            QString message = root.value(QStringLiteral("message")).toString();
            if (message.isEmpty())
                message = code.isEmpty() ? QString::fromUtf8(body.left(180)) : code;
            setStatus(tr("Delete failed: %1").arg(message));
            return;
        }
        setStatus(tr("Deleted %1").arg(name));
        startList();
        return;
    }

    const FirmwarePage page = parseFirmwarePage(body, apiRoot());
    setBusy(false);
    if (!page.ok) {
        updatePager();
        setStatus(page.error.isEmpty() ? tr("Server did not return a firmware list")
                                        : tr("Load failed: %1").arg(page.error));
        return;
    }
    m_page = page.pageNo;
    m_pages = page.pages;
    m_total = page.total;
    m_model->setItems(page.items);
    updatePager();
    if (page.items.isEmpty())
        setStatus(tr("No firmware found"));
    else
        setStatus(tr("Loaded %1 firmwares").arg(page.items.size()));
}

void FirmwareWidget::abortDownload()
{
    const QVector<QNetworkReply *> parts = m_parts;
    m_parts.clear();
    for (QNetworkReply *reply : parts) {
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    if (!m_downloadFile)
        return;
    const QString path = m_downloadFile->fileName();
    m_downloadFile->close();
    delete m_downloadFile;
    m_downloadFile = nullptr;
    QFile::remove(path);
}

void FirmwareWidget::updateDownloadStatus()
{
    if (m_downloadTotal > 0)
        setStatus(tr("Downloading %1 (%2 / %3)")
                      .arg(m_downloadName, formatSize(m_downloadGot), formatSize(m_downloadTotal)));
    else
        setStatus(tr("Downloading %1").arg(m_downloadName));
}

void FirmwareWidget::writeDownload(QNetworkReply *reply)
{
    if (!m_downloadFile || !m_parts.contains(reply))
        return;
    const QByteArray data = reply->readAll();
    if (data.isEmpty())
        return;
    const qint64 got = reply->property("got").toLongLong();
    const qint64 offset = reply->property("offset").toLongLong();
    // 段与段谁先到都行：写在这段自己的偏移上，不追加到文件尾。
    if (!m_downloadFile->seek(offset + got) || m_downloadFile->write(data) != data.size()) {
        abortDownload();
        setStatus(tr("Cannot save file: %1").arg(m_downloadName));
        return;
    }
    reply->setProperty("got", got + data.size());
    m_downloadGot += data.size();
    updateDownloadStatus();
}

void FirmwareWidget::noteDownloadMeta(QNetworkReply *reply)
{
    if (!m_parts.contains(reply) || m_parts.size() < 2)
        return;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 0)
        return;
    if (status == 206) {
        qint64 start = -1;
        qint64 total = -1;
        // 服务器报的起点必须和我们请求的偏移一致，否则这段不能写进去。
        if (!contentRangeStart(reply->rawHeader("Content-Range"), &start, &total)
            || start != reply->property("offset").toLongLong()
            || total != m_downloadTotal) {
            abortDownload();
            setStatus(tr("Download failed: %1").arg(m_downloadName));
        }
        return;
    }
    // 这段请求拿到的是整个文件。停掉其它段，从文件头重写，避免几段叠在一起。
    const QVector<QNetworkReply *> others = m_parts;
    m_parts.clear();
    m_parts.append(reply);
    for (QNetworkReply *other : others) {
        if (other == reply)
            continue;
        other->disconnect(this);
        other->abort();
        other->deleteLater();
    }
    reply->setProperty("offset", 0);
    reply->setProperty("got", qint64(0));
    m_downloadGot = 0;
    m_downloadFile->resize(0);
}

void FirmwareWidget::finishDownloadPart(QNetworkReply *reply)
{
    if (!m_parts.contains(reply))
        return;
    m_parts.removeAll(reply);
    const QNetworkReply::NetworkError error = reply->error();
    const QString errorText = reply->errorString();
    reply->deleteLater();
    if (error == QNetworkReply::OperationCanceledError)
        return;
    if (error != QNetworkReply::NoError) {
        abortDownload();
        setStatus(tr("Download failed: %1").arg(errorText));
        return;
    }
    const qint64 expect = reply->property("length").toLongLong();
    if (expect > 0 && reply->property("got").toLongLong() != expect) {
        abortDownload();
        setStatus(tr("Download failed: %1").arg(m_downloadName));
        return;
    }
    if (!m_parts.isEmpty() || !m_downloadFile)
        return;
    if (m_downloadTotal > 0 && m_downloadGot != m_downloadTotal) {
        abortDownload();
        setStatus(tr("Download failed: %1").arg(m_downloadName));
        return;
    }
    const QString path = m_downloadFile->fileName();
    m_downloadFile->close();
    delete m_downloadFile;
    m_downloadFile = nullptr;
    setStatus(tr("Saved %1").arg(path));
    m_fileHash->hashFile(path);
}

void FirmwareWidget::deleteRow(int row)
{
    const int id = m_model->idAt(row);
    const QString name = m_model->nameAt(row);
    if (id <= 0) {
        setStatus(tr("Delete failed: %1").arg(name));
        return;
    }
    const auto answer = QMessageBox::question(
        this, tr("Delete"), tr("Delete firmware %1?").arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;
    if (m_token.isEmpty()) {
        setStatus(tr("Sign-in failed: %1").arg(name));
        return;
    }
    startDelete(id, name);
}

void FirmwareWidget::startDelete(int id, const QString &name)
{
    if (!apiRoot().isValid()) {
        setStatus(tr("Invalid address"));
        return;
    }
    // 删除和列表共用 m_reply。进行中的登录、上传或翻页先停掉，避免两个回复抢同一个槽。
    abortReply();
    QNetworkRequest request(apiRoot().resolved(QUrl(QStringLiteral("firmwares/%1").arg(id))));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    applyAuth(&request);
    m_waitingLogin = false;
    m_replyKind = ReplyKind::Delete;
    setBusy(true);
    setStatus(tr("Deleting %1").arg(name));
    m_reply = m_nam->deleteResource(request);
    m_reply->setProperty("firmwareName", name);
    connect(m_reply, &QNetworkReply::finished, this, &FirmwareWidget::onReplyFinished);
}

void FirmwareWidget::downloadRow(int row)
{
    const QString link = m_model->urlAt(row);
    if (link.isEmpty())
        return;
    QString fileName = QFileInfo(QUrl(link).path()).fileName();
    if (fileName.isEmpty())
        fileName = m_model->nameAt(row);
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save Firmware"), SettingsWidget::firmwareFilePath(fileName), tr("All Files (*)"));
    if (path.isEmpty())
        return;

    abortDownload();
    auto *file = new QFile(path);
    // 不使用 QFile 缓冲，避免 seek 前缓冲区里的数据写错位置。
    if (!file->open(QIODevice::WriteOnly | QIODevice::Unbuffered)) {
        setStatus(tr("Cannot save file: %1").arg(path));
        delete file;
        return;
    }
    const qint64 total = m_model->bytesAt(row);
    // 实测单连接大约 12 KB/s，而 nginx 接受 Range。按 64KB 一段、最多 8 段一起拉。
    const int partCount = total > 0
                              ? int(qBound(qint64(1), (total + 64 * 1024 - 1) / (64 * 1024), qint64(8)))
                              : 1;
    if (total > 0)
        file->resize(total);
    m_downloadFile = file;
    m_downloadGot = 0;
    m_downloadTotal = total;
    m_downloadName = fileName;
    const qint64 chunk = partCount > 0 ? (total + partCount - 1) / partCount : 0;
    for (int i = 0; i < partCount; ++i) {
        const qint64 begin = chunk * i;
        if (total > 0 && begin >= total)
            break;
        const qint64 end = total > 0 ? qMin(total - 1, begin + chunk - 1) : -1;
        QNetworkRequest request{QUrl(link)};
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        if (end >= begin)
            request.setRawHeader("Range", "bytes=" + QByteArray::number(begin) + "-" + QByteArray::number(end));
        applyAuth(&request);
        QNetworkReply *reply = m_nam->get(request);
        reply->setProperty("offset", begin);
        reply->setProperty("got", qint64(0));
        reply->setProperty("length", end >= begin ? end - begin + 1 : qint64(0));
        m_parts.append(reply);
        connect(reply, &QNetworkReply::metaDataChanged, this, [this, reply]() { noteDownloadMeta(reply); });
        connect(reply, &QNetworkReply::readyRead, this, [this, reply]() { writeDownload(reply); });
        connect(reply, &QNetworkReply::finished, this, [this, reply]() { finishDownloadPart(reply); });
    }
    updateDownloadStatus();
}
