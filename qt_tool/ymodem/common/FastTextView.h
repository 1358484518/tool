#ifndef FASTTEXTVIEW_H
#define FASTTEXTVIEW_H

#include <QWidget>
#include <QByteArray>
#include <QString>
#include <QVector>

class QPlainTextEdit;
class QScrollBar;
class QTimer;

/**
 * 接收窗口，串口页和网络页共用，所以放在 common，两个页面不再互相引用。
 * 原始字节和时间戳分开留在环形缓冲里，编辑框只放当前这一屏。
 * 十六进制和时间戳只改变画法。来数据后最多每 16 毫秒画一次。
 */
class FastTextView : public QWidget
{
    Q_OBJECT
public:
    explicit FastTextView(QWidget *parent = nullptr);  // 建只读编辑框和按字节偏移的滚动条

    bool isEmpty() const;                             // 缓冲里还没有字节
    const QByteArray &getData() const;                // 按当前勾选把缓冲格式化成文本，给保存日志用
    void setLineWrap(bool wrap);                      // 是否按窗口宽度换行
    void setFollowBottom(bool follow);                // 新数据来时是否贴着底部
    void setLocal8Bit(bool local8Bit);                // 文本模式用系统本地编码解释原始字节

public slots:
    void setHexDisplay(bool on);                      // 原始数据改按十六进制画，时间戳仍是文字
    void setShowTimestamp(bool on);                   // 显示或藏起已经记下的时间戳
    void addTimestamp(const QString &text);           // 追加时间戳文字，不参与十六进制转换
    void addText(const QString &text);                // 追加方向、地址、状态这类文字
    void addData(QByteArray data);                    // 追加收到的原始字节
    void clear();                                     // 清空缓冲和屏幕
    void scrollToTop();                               // 滚到数据开头
    void scrollToBottom();                            // 滚到最新数据并跟随

protected:
    void resizeEvent(QResizeEvent *e) override;       // 窗口变了重新算一页该画多少
    void showEvent(QShowEvent *e) override;           // 第一次显示时画当前页
    bool eventFilter(QObject *watched, QEvent *event) override;  // 滚轮按字节步进

private slots:
    void onScroll(int byteOffset);                    // 滚动条单位是字节偏移，不是行号
    void flushPendingPaint();                         // 定时器触发，真正把当前页画到编辑框

private:
    int calcReadSize() const;                         // 当前页大约要读多少字节
    void refresh();                                   // 按滚动位置切出一页写进编辑框
    void schedulePaint();                             // 合并多次 addData，最多每帧画一次
    bool isAtBottom() const;                          // 是否贴着底部，决定要不要自动跟随
    int columnCount() const;                          // 当前字体下一屏大约多少列
    int rowCount() const;                             // 当前字体下一屏大约多少行
    void invalidateShown();                           // 上次画到编辑框的窗口作废
    QString formatWindow(int start, int length) const;  // 按当前勾选把这一段画成文字或十六进制
    int lineStartAt(int pos) const;                   // 往前收到这一行的开头，避免从半个字切开
    int kindAt(int logical) const;                    // 这个字节是时间戳、文字还是原始数据
    int pieceIndexAt(int logical) const;              // 逻辑下标落在哪一段
    void appendPiece(const QByteArray &bytes, quint8 kind);  // 写入一段并记下种类
    void shiftPieces(int drop);                       // 丢掉旧字节后，各段起点一起前移
    void ensureRing();                                // 环形缓冲固定 8MB，只分配一次
    int appendBytes(const QByteArray &data);          // 写入环形缓冲，返回丢掉的旧字节数
    void noteDropped(int drop);                       // 丢掉旧字节后，滚动位置和已画位置一起前移
    char logByte(int logical) const;                  // 逻辑下标读一个字节，0 是当前最旧的字节
    QByteArray logSlice(int start, int length) const; // 拷出一段，跨越环尾时拼成连续内存

    QPlainTextEdit *m_edit = nullptr;
    QScrollBar *m_scroll = nullptr;
    QTimer *m_paintTimer = nullptr;
    QByteArray m_ring;                                // 固定容量，写满后覆盖最旧的字节
    int m_head = 0;                                   // 最旧字节在 m_ring 里的位置
    int m_count = 0;                                  // 当前有效字节数
    mutable QByteArray m_linear;                      // getData 时放格式化结果，保存日志才用
    struct Piece {
        int start = 0;
        int length = 0;
        quint8 kind = 0;
    };
    QVector<Piece> m_pieces;                          // 和时间戳、文字、原始数据一一对应
    int m_pieceBegin = 0;                             // 还有效的第一段，丢掉的旧段不立刻挪走
    bool m_inited = false;
    bool m_followBottom = true;
    bool m_hexDisplay = false;
    bool m_showTimestamp = false;
    bool m_local8Bit = false;
    int m_wheelStep = 0;
    int m_shownStart = -1;                            // 编辑框里这段的逻辑起点
    int m_shownLength = 0;
    int m_shownWidth = -1;
    bool m_shownWrap = true;
    bool m_shownHex = false;
    bool m_shownStamp = false;
    bool m_shownLocal = false;
    bool m_tailShown = false;                         // 编辑框里是最新一屏，新数据可以接着往下补
};

#endif // FASTTEXTVIEW_H
