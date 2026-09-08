#ifndef FASTTEXTVIEW_H
#define FASTTEXTVIEW_H

#include <QWidget>
#include <QByteArray>

class QPlainTextEdit;
class QScrollBar;
class QTimer;

/**
 * 接收窗口。原文在 m_data，屏幕只画当前这一页。
 * 来数据后在下一轮事件循环画一次（不是按秒定时刷新），同一批包只会重绘一次。
 */
class FastTextView : public QWidget
{
    Q_OBJECT
public:
    enum ViewMode {
        TextMode,
        HexMode
    };

    explicit FastTextView(QWidget *parent = nullptr);

    void setData(const QByteArray &data);
    const QByteArray &getData() const;
    ViewMode viewMode() const;
    void setLineWrap(bool wrap);

public slots:
    void setViewMode(ViewMode mode);
    void addData(QByteArray data);
    void clear();
    void scrollToTop();
    void scrollToBottom();

    int totalVisualLineCount() const;
    int emptyLineCount() const;
    int calcHexPageBytes(int fixedBytesPerLine = 0) const;
    int calcTextPageBytes() const;
    int calcTextPageBytes(const QByteArray &data) const;

protected:
    void resizeEvent(QResizeEvent *e) override;
    void showEvent(QShowEvent *e) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onScroll(int byteOffset);  // 滚动条单位是字节偏移，不是行号
    void flushPendingPaint();

private:
    int calcReadSize() const;
    void refresh();
    void trimIfNeeded();
    void schedulePaint();
    bool isAtBottom() const;

    QPlainTextEdit *m_edit = nullptr;
    QScrollBar *m_scroll = nullptr;
    QTimer *m_paintTimer = nullptr;
    QByteArray m_data;
    bool m_inited = false;
    bool m_followBottom = true;
    ViewMode m_viewMode = TextMode;
    int m_wheelStep = 0;
};

#endif // FASTTEXTVIEW_H
