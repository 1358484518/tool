#ifndef FASTTEXTVIEW_H
#define FASTTEXTVIEW_H
#include <QWidget>
#include <QByteArray>
class QPlainTextEdit;
class QScrollBar;
class QTimer;
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
    const QByteArray& getData();
    ViewMode viewMode() const;
//    void setBytesPerLine(int n);
public slots:
    void setViewMode(ViewMode mode);
    void addData(QByteArray data);
    void clear();
    void scrollToTop();
    void scrollToBottom();
protected:
    void resizeEvent(QResizeEvent *e) override;
    void showEvent(QShowEvent *e) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
private slots:
    void onScroll(int byteOffset);
    void onPollTimeout();
private:
    QPlainTextEdit *m_edit;
    QScrollBar     *m_scroll;
    QTimer         *m_pollTimer;
    QByteArray      m_data;
    bool            m_inited = false;
    int             m_lastScrollValue = 0;
    // 仅新增模式相关成员，原有成员不动
    ViewMode        m_viewMode = TextMode;
//    int             m_bytesPerLine = 16;

    int  calcReadSize();
    void refresh();
};
#endif
