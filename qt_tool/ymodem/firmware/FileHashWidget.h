#ifndef FILEHASHWIDGET_H
#define FILEHASHWIDGET_H

#include <QGroupBox>
#include <QStringList>

class QPlainTextEdit;
class QPushButton;

/**
 * 文件校验控件。结果排版和 Hash 1.04 的文本区一样：文件、大小、修改时间、MD5、SHA1、CRC32。
 * 一次可以算多个文件，每段之间空一行。进度和完成不画在这里，通过信号交给外面的状态栏。
 */
class FileHashWidget : public QGroupBox
{
    Q_OBJECT

public:
    explicit FileHashWidget(QWidget *parent = nullptr);  // 建文本区和浏览、清除、复制、保存

    bool hashFile(const QString &path);             // 计算这一个文件并显示，打不开返回 false
    bool hashFiles(const QStringList &paths);       // 按顺序计算这些文件，结果追加到已有文本后面

signals:
    void progress(int percent);          // 已读百分比，0 到 100
    void completed();                     // 这一次计算结束
    void failed(const QString &message);  // 文件打不开

protected:
    void changeEvent(QEvent *event) override;              // 语言切换时刷新文字
    void dragEnterEvent(QDragEnterEvent *event) override;  // 接受拖进来的本地文件
    void dropEvent(QDropEvent *event) override;            // 放下后计算全部本地文件

private:
    void retranslateUi();  // 按当前语言重写本控件文字
    void browseFile();     // 弹出可多选的文件框
    void copyResult();     // 复制文本区
    void saveResult();     // 把文本区存成 txt
    void clearResult();    // 清掉文本区

    QPlainTextEdit *m_report = nullptr;  // Hash 1.04 那种多行结果，在分组里尽量占满，超过 1MB 丢掉旧内容
    QPushButton *m_browseBtn = nullptr;  // 选择文件
    QPushButton *m_clearBtn = nullptr;   // 清掉结果
    QPushButton *m_copyBtn = nullptr;    // 复制结果
    QPushButton *m_saveBtn = nullptr;    // 保存结果
    bool m_hashing = false;              // 正在计算，避免进度刷新时重入
};

#endif // FILEHASHWIDGET_H
