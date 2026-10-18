#ifndef FIRMWAREWIDGET_H
#define FIRMWAREWIDGET_H

#include <QAbstractTableModel>
#include <QWidget>

class FileHashWidget;
class FirmwarePackWidget;
class FirmwareUploadDialog;
class QEvent;
class QGroupBox;
class QLabel;
class QLineEdit;
class QFile;
class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;
class QPushButton;
class QTableView;

/**
 * 固件表的一行，字段和网站 /firmwares 列表一致。
 */
struct FirmwareEntry
{
    QString name;     // 文件名
    QString version;  // 版本
    QString size;     // 文件大小，已换成 KB/MB
    qint64 bytes = 0; // 原始字节数，用来切分段下载
    QString created;  // 创建时间
    QString url;      // fileUrl，空表示这一行不能下
    int id = 0;       // 网站上的固件 id，删除时拼进 /firmwares/{id}
};

/**
 * 固件表格的数据。列是文件名、版本、大小、创建时间、链接、下载、删除。
 * 下载列和删除列只提供按钮文字，点击由委托交给 FirmwareWidget。
 */
class FirmwareListModel : public QAbstractTableModel
{
public:
    explicit FirmwareListModel(QObject *parent = nullptr);  // 空表

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;     // 固件条数
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;  // 固定七列
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;  // 单元格文字
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;  // 表头
    void setItems(const QVector<FirmwareEntry> &items);  // 换成这一次拉到的列表
    void refreshText();  // 语言切换后让表头和下载按钮重画
    QString nameAt(int row) const;  // 这一行的显示名
    QString urlAt(int row) const;   // 这一行的下载地址
    qint64 bytesAt(int row) const;  // 这一行的字节数，没有就是 0
    int idAt(int row) const;        // 这一行的固件 id，没有就是 0

private:
    QVector<FirmwareEntry> m_items;  // 当前列表
};

/**
 * 固件页。地址默认是网站上的固件页，可改。
 * 列表不读这个网页本身，而是读同一站点的 /admin-api/v1/firmwares。
 * 填了用户名就先 POST /login，再带 Authorization 去拉列表。
 * 上传走 POST /firmwares，表单字段和网站一致：file、version、models、signingCertId。
 * 下载上传在上面。下面校验和组包并排，进度和保存结果写在本页状态栏。
 */
class FirmwareWidget : public QWidget
{
    Q_OBJECT

public:
    explicit FirmwareWidget(QWidget *parent = nullptr);  // 建界面并恢复上次的地址和用户名
    ~FirmwareWidget() override;                          // 停下还没完成的请求

    void downloadRow(int row);  // 分段并行下载到设置里的固件目录，仍弹出保存对话框
    void deleteRow(int row);    // 确认后 DELETE /firmwares/{id}，成功再刷新当前页

protected:
    void changeEvent(QEvent *event) override;  // 语言切换时刷新本页文字

private:
    void setupUi();             // 上面是下载上传分组，下面校验和组包并排
    void retranslateUi();       // 按当前语言重写本页控件文字
    void fitDownloadColumn();   // 按“下载”按钮文字撑开下载列
    void loadSettings();        // 恢复地址、用户名、文件名搜索，并解密密码
    void saveSettings();        // 记下地址、用户名、文件名搜索和组包内容，密码只写密文
    void refresh();             // 从第一页重新登录并拉最新固件
    void searchByName();        // 按文件名从第一页拉列表，已登录就不再登录
    void showPage(int page);    // 翻到某一页，不再重新登录
    void beginUpload();         // 上传按钮：没登录就先登录，再打开上传框
    void openUploadDialog();    // 弹出上传框，并去拉可选的签名证书
    void startLogin();          // POST /login
    void startList();           // GET /firmwares 的当前页
    void startCertificates();   // GET /signing-certificates/valid
    void startUpload(FirmwareUploadDialog *dialog);  // 按上传框里的内容 POST /firmwares
    void startDelete(int id, const QString &name);   // DELETE /firmwares/{id}
    void onReplyFinished();     // 登录、列表、证书或上传返回后接着走
    void applyCertificates(const QByteArray &body);  // 把证书名单填进还开着的上传框
    void updatePager();         // 按当前页码刷新上一页、下一页和页码文字
    void abortReply();          // 丢掉进行中的登录、列表、证书或上传请求
    void abortDownload();       // 停掉分段下载，并删掉还没下完的文件
    void writeDownload(QNetworkReply *reply);   // 把这一段新到的数据写到对应偏移
    void noteDownloadMeta(QNetworkReply *reply);  // 服务器不认 Range 时改回单连接
    void finishDownloadPart(QNetworkReply *reply);  // 一段结束；全部结束才算保存成功
    void updateDownloadStatus();  // 状态栏显示已下和总大小
    void setBusy(bool busy);    // 请求进行中时关掉刷新和上传
    QUrl apiRoot() const;       // 由页面地址推出 /admin-api/v1
    void applyAuth(QNetworkRequest *request) const;  // 同一站点才带登录令牌
    void setStatus(const QString &text);             // 页底状态

    QLabel *m_urlLabel = nullptr;       // 地址
    QLabel *m_userLabel = nullptr;      // 用户名
    QLabel *m_passwordLabel = nullptr;  // 密码
    QLineEdit *m_urlEdit = nullptr;     // 固件页地址，默认为网站上的 /firmwares
    QLineEdit *m_userEdit = nullptr;    // 登录邮箱，接口字段是 email
    QLineEdit *m_passwordEdit = nullptr;  // 登录密码，落盘前会加密
    QLabel *m_nameLabel = nullptr;        // 文件名搜索
    QLineEdit *m_nameEdit = nullptr;      // 传给列表接口的 search，空表示不过滤
    QPushButton *m_searchBtn = nullptr;   // 按当前文件名从第一页重新拉列表
    QPushButton *m_refreshBtn = nullptr;  // 重新拉取列表
    QPushButton *m_uploadBtn = nullptr;   // 打开上传固件对话框
    QPushButton *m_prevBtn = nullptr;     // 上一页
    QPushButton *m_nextBtn = nullptr;     // 下一页
    QLabel *m_pageLabel = nullptr;        // 第几页、共多少条
    QGroupBox *m_firmwareGroup = nullptr; // 上面的下载和上传
    QTableView *m_table = nullptr;        // 文件名、版本、大小、时间、链接、下载、删除
    FileHashWidget *m_fileHash = nullptr;         // 下面左侧的校验
    FirmwarePackWidget *m_pack = nullptr;         // 下面右侧的组包，和校验并排
    QLabel *m_statusLabel = nullptr;      // 页底状态，登录、列表、下载、校验和组包的结果
    FirmwareListModel *m_model = nullptr; // 表格数据
    QNetworkAccessManager *m_nam = nullptr;  // 登录、列表、下载共用，好带上 Cookie
    QNetworkReply *m_reply = nullptr;     // 当前的登录、列表、证书或上传请求
    QFile *m_downloadFile = nullptr;      // 正在写入的固件，几段请求共用
    QVector<QNetworkReply *> m_parts;     // 并行的 Range 下载
    qint64 m_downloadGot = 0;             // 几段加起来已经写入的字节
    qint64 m_downloadTotal = 0;           // 文件总字节，0 表示服务器还没给长度
    QString m_downloadName;               // 状态栏上的文件名
    FirmwareUploadDialog *m_uploadDialog = nullptr;  // 正在显示的上传框，关掉后清空
    bool m_waitingLogin = false;          // true 时这次回复按登录处理
    bool m_loginForUpload = false;        // 这次登录是为了上传，成功后不要去拉列表
    enum class ReplyKind { List, Certificates, Upload, Delete };
    ReplyKind m_replyKind = ReplyKind::List;  // 非登录请求回来时按哪一种处理
    QString m_uploadFileName;             // 这次正在上传的文件名，成功后写到状态栏
    int m_page = 1;                       // 正在看的页码，从 1 开始，第 1 页是最新的
    int m_pages = 1;                      // 接口返回的总页数
    int m_total = 0;                      // 接口返回的固件总数
    QString m_appliedSearch;              // 当前列表实际使用的文件名，翻页时沿用
    QByteArray m_token;                   // 登录返回的 Authorization 值
};

#endif // FIRMWAREWIDGET_H


