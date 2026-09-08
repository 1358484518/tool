#ifndef FASTTEXTVIEW_H
#define FASTTEXTVIEW_H

#include <QWidget>
#include <QByteArray>

class QPlainTextEdit;
class QScrollBar;

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
    void onScroll(int byteOffset);

private:
    int calcReadSize() const;
    void refresh();
    void trimIfNeeded();

    QPlainTextEdit *m_edit = nullptr;
    QScrollBar *m_scroll = nullptr;
    QByteArray m_data;
    bool m_inited = false;
    ViewMode m_viewMode = TextMode;
    int m_wheelStep = 0;
};

#endif // FASTTEXTVIEW_H
