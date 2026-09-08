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

    explicit FastTextView(QWidget *parent = nullptr);  // 建只读编辑框和按字节偏移的滚动条

    void setData(const QByteArray &data);             // 整段替换底层缓冲并重绘当前页
    const QByteArray &getData() const;                // 取出全部原文，给保存日志用
    ViewMode viewMode() const;                        // 当前是文本还是 HEX
    void setLineWrap(bool wrap);                      // 文本模式是否自动换行

public slots:
    void setViewMode(ViewMode mode);                  // 切换文本/HEX，立刻重绘
    void addData(QByteArray data);                    // 追加收到的字节，下一轮事件循环画一次
    void clear();                                     // 清空缓冲和屏幕
    void scrollToTop();                               // 滚到数据开头
    void scrollToBottom();                            // 滚到最新数据并跟随

    int totalVisualLineCount() const;                 // 按当前模式估算总可视行数
    int emptyLineCount() const;                       // 当前页还能再塞几行空行
    int calcHexPageBytes(int fixedBytesPerLine = 0) const;  // 一屏 HEX 大约对应多少字节
    int calcTextPageBytes() const;                    // 一屏文本大约对应多少字节（用当前数据估）
    int calcTextPageBytes(const QByteArray &data) const;    // 按给定数据估一屏字节数

protected:
    void resizeEvent(QResizeEvent *e) override;       // 窗口变了重新算一页该画多少
    void showEvent(QShowEvent *e) override;           // 第一次显示时画当前页
    bool eventFilter(QObject *watched, QEvent *event) override;  // 滚轮按字节步进

private slots:
    void onScroll(int byteOffset);                    // 滚动条单位是字节偏移，不是行号
    void flushPendingPaint();                         // 定时器触发，真正把当前页画到编辑框

private:
    int calcReadSize() const;                         // 当前页要从 m_data 读多少字节
    void refresh();                                   // 按滚动位置切出一页写进编辑框
    void trimIfNeeded();                              // 缓冲太大时丢掉最前面一段
    void schedulePaint();                             // 合并多次 addData，只排一次绘制
    bool isAtBottom() const;                          // 是否贴着底部，决定要不要自动跟随

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
