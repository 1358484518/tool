#include "FastTextView.h"
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QHBoxLayout>
#include <QFont>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <QWheelEvent>
#include <QTextBlock>
#include <QtMath>
#include <QElapsedTimer>
#include <QtDebug>


FastTextView::FastTextView(QWidget *parent) : QWidget(parent)
{
    QHBoxLayout *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0,0,0,0);
    lay->setSpacing(0);

    // 仅加等宽字体和水平滚动条，其他构造逻辑完全不动
    QFont font("Consolas", 10);
    font.setStyleHint(QFont::Monospace);
    m_edit = new QPlainTextEdit(this);
    m_edit->setReadOnly(true);
    m_edit->setFont(font);
    m_edit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
//    m_edit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
//    m_edit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_edit->setLineWrapMode(QPlainTextEdit::WidgetWidth); // 值0

    m_scroll = new QScrollBar(Qt::Vertical, this);
    lay->addWidget(m_edit, 1);
    lay->addWidget(m_scroll);
    // 10ms轮询定时器
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(15);
    connect(m_pollTimer, &QTimer::timeout, this, &FastTextView::onPollTimeout);
    m_pollTimer->start();
    m_lastScrollValue = m_scroll->value();
    // 事件过滤器监听滚轮
    m_edit->viewport()->installEventFilter(this);
}

/************************* 仅新增模式接口实现 *************************/
void FastTextView::setViewMode(ViewMode mode)
{
    if (m_viewMode == mode) return;
    m_viewMode = mode;
//    m_edit->setLineWrapMode(mode == TextMode ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
//    m_scroll->setValue(0);
    m_lastScrollValue = 0;
    refresh();
}

FastTextView::ViewMode FastTextView::viewMode() const
{
    return m_viewMode;
}

void FastTextView::addData(QByteArray data)
{
    bool first = m_data.isEmpty();
    if (data.isEmpty()) return;
    m_data.append(data);
    if (!m_inited) return;

    int readSize = calcReadSize();
    int maxOffset = m_data.size();//readSize;//qMax(0, (int)m_data.size() - readSize / 2);

    // 只更新滑块范围，不主动刷新视图
    // 刷新由 onPollTimeout 检测滑块 value 变化触发（用户手拉 / setValue 均可）
    m_scroll->blockSignals(true);
    m_scroll->setMaximum(maxOffset);   // 只扩展上限，最小值保持0不变
    m_scroll->setPageStep(readSize / 2);
    m_scroll->setSingleStep(readSize / 20);
    m_scroll->blockSignals(false);
    if(first){
        onScroll(0);
    }
}

void FastTextView::clear()
{
    m_data.clear();
    m_lastScrollValue = 0;

    if (!m_inited) return;

    m_edit->clear();
    m_scroll->setRange(0, 0);
}

//void FastTextView::setBytesPerLine(int n)
//{
//    if (n < 8) n = 8;
//    if (m_bytesPerLine == n) return;
//    m_bytesPerLine = n;
//    refresh();
//}


void FastTextView::onPollTimeout()
{
    int curVal = m_scroll->value();
    if (curVal != m_lastScrollValue) {
        m_lastScrollValue = curVal;
        onScroll(curVal);
//        qDebug()<<1<<curVal;
    }
    if(calcReadSize()>m_data.size()&&m_lastSizeValue!=m_data.size()){
        m_lastSizeValue = m_data.size();
        onScroll(curVal);
//        qDebug()<<2<<curVal;
    }
}

void FastTextView::scrollToTop()
{
    if (!m_inited || m_data.isEmpty()) return;
    m_scroll->setValue(0);
}

void FastTextView::scrollToBottom()
{
    if (!m_inited || m_data.isEmpty()) return;
    m_scroll->setValue(m_scroll->maximum());
}

int FastTextView::totalVisualLineCount() const
{
    QTextDocument *doc = m_edit->document();
    int lineH = QFontMetrics(m_edit->font()).lineSpacing();
    // 文档总高度 / 单行高度 = 自动换行后的实际显示行数
    return qCeil(doc->size().height() / lineH);
}

int FastTextView::emptyLineCount() const
{
    int count = 0;
    QTextBlock block = m_edit->document()->begin();
    while (block.isValid()) {
        if (block.text().isEmpty()) count++;
        block = block.next();
    }
    return count;
}
bool FastTextView::eventFilter(QObject *watched, QEvent *event)
{
//    if (watched == m_edit && event->type() == QEvent::Wheel)
    if (watched == m_edit->viewport() && event->type() == QEvent::Wheel)
    {
        QWheelEvent *wheel = static_cast<QWheelEvent*>(event);
        int delta = wheel->angleDelta().y();
        int step = qMin(1024,m_scroll->singleStep()); // 和滚动条单步一致，保证联动
        if(WheelStep)step = WheelStep;
        if (delta > 0) {
            m_scroll->setValue(m_scroll->value() - step/* * 3*/); // 滚一下动3步，手感合适
        } else {
            m_scroll->setValue(m_scroll->value() + step /** 3*/);
        }
        WheelStep = 0;
        // 立刻更新内容+滑块位置
        m_lastScrollValue = m_scroll->value();
        onScroll(m_lastScrollValue);
//        qDebug()<<delta<<step<<m_lastScrollValue;
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

int FastTextView::calcReadSize() {
    int h = m_edit->viewport()->height();
    return h > 0 ? h * 20 : 2400;
}
void FastTextView::refresh()
{
    if (!m_inited || m_data.isEmpty()) {
        m_edit->clear();
        m_scroll->setRange(0,0);
        return;
    }
    int readSize = calcReadSize();
    int maxOffset = m_data.size();//qMax(0, (int)m_data.size() - readSize/2);

    m_scroll->blockSignals(true);
    m_scroll->setRange(0, maxOffset);
    m_scroll->setPageStep(readSize/2);
    m_scroll->setSingleStep(readSize / 20); // 单步1/20页，滑块移动明显
    m_scroll->blockSignals(false);
    m_lastScrollValue = m_scroll->value();
    onScroll(m_lastScrollValue);
}
void FastTextView::setData(const QByteArray &data)
{
    m_data = data;
    m_scroll->setValue(0);
    m_lastScrollValue = 0;
    refresh();
}

const QByteArray &FastTextView::getData()
{
    return m_data;
}

void FastTextView::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    refresh();
}
void FastTextView::showEvent(QShowEvent *e) {
    QWidget::showEvent(e);
    if (!m_inited) {
        m_inited = true;
        refresh();
    }
}

/************************* onScroll只改setPlainText部分，其他完全不动 *************************/

void FastTextView::onScroll(int byteOffset)
{
    if (m_data.isEmpty()) {
        m_edit->clear();
        return;
    }
    int readSize = calcReadSize();
    byteOffset = qBound(0, byteOffset, (int)m_data.size()-1);
    readSize = qMin(readSize, (int)(m_data.size() - byteOffset));
    int dataoffest=byteOffset;
#define M_DATA_OFFEST 6*1024
//    int dispSize=0;
//    QElapsedTimer timer;
//    timer.start();
//    if(m_data.size()>M_DATA_OFFEST){
//        dispSize = calcTextPageBytes(m_data.mid(m_data.size() -
//                                    M_DATA_OFFEST,M_DATA_OFFEST));
//    }else{
//        dispSize = calcTextPageBytes(m_data);
//    }


//    if(byteOffset > (m_data.size() - dispSize)){//数据能填充满控件
//        byteOffset = m_data.size()- dispSize;
//    }

//    if(readSize < dispSize&&dispSize<M_DATA_OFFEST){
//        readSize = dispSize;
//    }
//    qDebug()<<timer.nsecsElapsed() / 1000.0;
//    qDebug()<<"byteOffset:"<<byteOffset<<readSize<<dispSize<<m_viewMode<<timer.nsecsElapsed() / 1000.0;
    if(byteOffset > (m_data.size() - M_DATA_OFFEST)){//数据能填充满控件
        byteOffset = m_data.size()- M_DATA_OFFEST;
    }

    if(readSize < M_DATA_OFFEST){
        readSize = M_DATA_OFFEST;
    }

    QByteArray chunk = m_data.mid(byteOffset, readSize);
    QString showText;

    if (m_viewMode == HexMode) {
//        showText.reserve(chunk.size() * 3 + chunk.size()/m_bytesPerLine + 10);
//        for (int i=0; i<chunk.size(); i++) {
//            if (i > 0 && i % m_bytesPerLine == 0) showText += '\n';
//            unsigned char c = chunk.at(i);
//            showText += QString("%1 ").arg(c, 2, 16, QChar('0')).toUpper();
//        }
        static const char hexTable[] = "0123456789ABCDEF";
        showText.reserve(chunk.size() * 3); // 精确预分配空间，避免扩容拷贝
        for (int i = 0; i < chunk.size(); i++) {
            unsigned char c = chunk.at(i);
            // 位运算直接取高低4位查表，无任何格式化/函数调用开销
            showText.append(QLatin1Char(hexTable[(c >> 4) & 0x0F]));
            showText.append(QLatin1Char(hexTable[c & 0x0F]));
            showText.append(QLatin1Char(' '));
        }
    } else {
        showText = QString::fromUtf8(chunk);
    }
    m_edit->setPlainText(showText);
    // ==================================================

    qreal progress = 0;
    if (m_scroll->maximum() > 0) {
        progress = (qreal)dataoffest / m_scroll->maximum();
        if(m_scroll->value() + 2 * M_DATA_OFFEST > m_scroll->maximum()){
            WheelStep = m_scroll->value() + 2 * M_DATA_OFFEST - m_scroll->maximum();
//            WheelStep=2;
//            qDebug()<<M_DATA_OFFEST;
        }
    }
    //避免进度条拉到底都不显示最后文本的情况
    QScrollBar *innerSb = m_edit->verticalScrollBar();
    innerSb->setValue(innerSb->maximum() * progress);
}


// 常量统一定义（仅本文件可见，k开头表示常量）
static const int kHorizontalPadding = 8;   // 左右内边距
static const int kMinBytesPerLine  = 8;   // HEX模式每行最少字节数
static const int kMaxAdjustTimes   = 5;   // 文本模式微调最大次数

// ==================== HEX模式：精确计算一页字节数 ====================
int FastTextView::calcHexPageBytes(int fixedBytesPerLine) const
{
    const QFontMetrics fontMetrics(m_edit->font());
    const int viewportHeight = m_edit->viewport()->height();
    const int lineHeight = fontMetrics.lineSpacing();
    const int visibleLineCount = viewportHeight / lineHeight;

    // 固定每行字节数：直接行数×每行字节数
    if (fixedBytesPerLine > 0) {
        return visibleLineCount * fixedBytesPerLine;
    }

    // 自适应宽度：计算当前宽度下一行能放多少个HEX字节
    const int viewportWidth = m_edit->viewport()->width() - kHorizontalPadding;
    const int hexByteWidth = fontMetrics.horizontalAdvance("FF "); // 1字节HEX占3字符宽度
    const int autoBytesPerLine = qMax(kMinBytesPerLine, viewportWidth / hexByteWidth);
    return visibleLineCount * autoBytesPerLine;
}

// ==================== 文本模式：预估+微调计算一页字节数 ====================
int FastTextView::calcTextPageBytes() const
{
    if (m_data.isEmpty()) return 0;

    const QFontMetrics fontMetrics(m_edit->font());
    const int viewportHeight = m_edit->viewport()->height();
    const int lineHeight = fontMetrics.lineSpacing();
    const int visibleLineCount = viewportHeight / lineHeight;
    const int viewportWidth = m_edit->viewport()->width() - kHorizontalPadding;

    // 第一步：按等宽字符预估一页字节数（纯ASCII下完全精准）
    const int singleCharWidth = fontMetrics.horizontalAdvance('0');
    const int charsPerLine = viewportWidth / singleCharWidth;
    int estimatedPageBytes = visibleLineCount * charsPerLine;
    if (estimatedPageBytes >= m_data.size()) {
        return m_data.size();
    }

    // 第二步：从末尾取预估字节，计算实际自动换行行数
    int startOffset = qMax(0, m_data.size() - estimatedPageBytes);
    QString pageText = QString::fromUtf8(m_data.mid(startOffset));
    int actualLineCount = fontMetrics.size(Qt::TextWordWrap, pageText).height() / lineHeight;

    // 第三步：微调对齐，最多调整kMaxAdjustTimes次，误差控制在1行内
    int adjustTimes = 0;
    while (actualLineCount < visibleLineCount && startOffset > 0 && adjustTimes < kMaxAdjustTimes) {
        startOffset = qMax(0, startOffset - charsPerLine); // 往前补一行
        pageText = QString::fromUtf8(m_data.mid(startOffset));
        actualLineCount = fontMetrics.size(Qt::TextWordWrap, pageText).height() / lineHeight;
        adjustTimes++;
    }
    while (actualLineCount > visibleLineCount + 1 && adjustTimes < kMaxAdjustTimes) {
        startOffset += charsPerLine / 2; // 往后减一行
        pageText = QString::fromUtf8(m_data.mid(startOffset));
        actualLineCount = fontMetrics.size(Qt::TextWordWrap, pageText).height() / lineHeight;
        adjustTimes++;
    }

    return m_data.size() - startOffset;
}
#if 0
int FastTextView::calcTextPageBytes(const QByteArray &data) const
{
    if (data.isEmpty()) return 0;

    const QFontMetrics fm(m_edit->font());
    const int viewWidth = m_edit->viewport()->width() - kHorizontalPadding;
    const int viewHeight = m_edit->viewport()->height();
    const int lineHeight = fm.lineSpacing();

    int accumulatedLines = 0;
    int scanPos = data.size();
    int lineEndPos = scanPos;

    while (scanPos > 0) {
        scanPos--;
        // 遇到硬换行：结算当前行
        if (data.at(scanPos) == '\n') {
            const int lineLen = lineEndPos - scanPos - 1;
            if (lineLen > 0) {
                const QString lineText = QString::fromUtf8(data.mid(scanPos + 1, lineLen));
                const int linePixelWidth = fm.horizontalAdvance(lineText);
                accumulatedLines += qMax(1, (linePixelWidth + viewWidth - 1) / viewWidth); // 向上取整
            } else {
                accumulatedLines++; // 空行也算一行
            }

            if (accumulatedLines * lineHeight >= viewHeight) {
                scanPos++;
                return data.size() - scanPos;
            }
            lineEndPos = scanPos;
        }
    }

    // 结算最顶部第一行
    const int firstLineLen = lineEndPos;
    if (firstLineLen > 0) {
        const QString firstLine = QString::fromUtf8(data.left(firstLineLen));
        const int firstLineWidth = fm.horizontalAdvance(firstLine);
        accumulatedLines += qMax(1, (firstLineWidth + viewWidth - 1) / viewWidth);
    }

    if (accumulatedLines * lineHeight < viewHeight) {
        return data.size();
    }

    return data.size() - scanPos;
}
#endif
#if 0
int FastTextView::calcTextPageBytes(const QByteArray &data) const
{
    if (data.isEmpty()) return 0;

    const QFontMetrics fm(m_edit->font());
    const int viewHeight = m_edit->viewport()->height();
    const int lineHeight = fm.lineSpacing();
    // 修复Bug3：宽度合法性兜底，避免除以0
    int viewWidth = m_edit->viewport()->width() - kHorizontalPadding;
    viewWidth = qMax(1, viewWidth);
    const int singleAsciiWidth = fm.horizontalAdvance('0');

    int accumulatedLines = 0;
    const char *ptr = data.constData();
    int scanPos = data.size();
    int lineEndPos = scanPos;

    while (scanPos > 0) {
        scanPos--;
        // 修复Bug4：兼容\r\n，跳过回车符
        if (ptr[scanPos] == '\r') {
            continue;
        }

        if (ptr[scanPos] == '\n') {
            const int lineLen = lineEndPos - scanPos - 1;
            if (lineLen > 0) {
                // 纯ASCII快速路径
                bool isAllAscii = true;
                for (int i = scanPos + 1; i < lineEndPos; ++i) {
                    if (static_cast<uchar>(ptr[i]) >= 0x80) {
                        isAllAscii = false;
                        break;
                    }
                }
                int linePixelWidth;
                if (isAllAscii) {
                    linePixelWidth = lineLen * singleAsciiWidth;
                } else {
                    const QString lineText = QString::fromUtf8(ptr + scanPos + 1, lineLen);
                    linePixelWidth = fm.horizontalAdvance(lineText);
                }
                accumulatedLines += qMax(1, (linePixelWidth + viewWidth - 1) / viewWidth);
            } else {
                accumulatedLines++; // 空行计入
            }

            // 修复Bug2：仅严格大于视口才停止，等于算放得下
            if (accumulatedLines * lineHeight > viewHeight) {
                scanPos++;
                return data.size() - scanPos;
            }
            lineEndPos = scanPos;
        }
    }

    // 修复Bug1：统一结算最顶部第一行，空行也必须计入
    const int firstLineLen = lineEndPos;
    if (firstLineLen > 0) {
        bool isAllAscii = true;
        for (int i = 0; i < firstLineLen; ++i) {
            if (static_cast<uchar>(ptr[i]) >= 0x80) {
                isAllAscii = false;
                break;
            }
        }
        int firstLineWidth;
        if (isAllAscii) {
            firstLineWidth = firstLineLen * singleAsciiWidth;
        } else {
            const QString firstLine = QString::fromUtf8(ptr, firstLineLen);
            firstLineWidth = fm.horizontalAdvance(firstLine);
        }
        accumulatedLines += qMax(1, (firstLineWidth + viewWidth - 1) / viewWidth);
    } else {
        accumulatedLines++; // 开头空行也计入
    }

    // 全部数据放得下，返回总大小
    if (accumulatedLines * lineHeight <= viewHeight) {
        return data.size();
    }

    return data.size() - scanPos;
}
#else
int FastTextView::calcTextPageBytes(const QByteArray &data) const
{
    if (data.isEmpty()) return 0;

    const QFontMetrics fm(m_edit->font());
    const int viewWidth = qMax(1, m_edit->viewport()->width() - kHorizontalPadding);
    const int viewHeight = m_edit->viewport()->height();
    const int lineHeight = fm.lineSpacing();
    const int asciiCharWidth = fm.horizontalAdvance('0');

    const char * const ptr = data.constData();
    const int totalSize = data.size();

    int accumulatedHeight = 0;
    int scanPos = totalSize;
    int lineEnd = totalSize; // 当前硬行的行尾（不含换行符）

    while (scanPos > 0) {
        scanPos--;

        if (ptr[scanPos] != '\n') {
            continue;
        }

        // ========== 遇到换行符：结算当前行 ==========
        int contentEnd = scanPos; // 行内容结束位置（不含换行符）
        // 兼容\r\n：往前跳一个回车符，行内容到\r为止
        if (scanPos > 0 && ptr[scanPos - 1] == '\r') {
            contentEnd--;
        }

        const int lineLen = contentEnd - (scanPos + 1);
        int linePixelHeight;

        if (lineLen > 0) {
            // 纯ASCII快速路径
            bool allAscii = true;
            for (int i = scanPos + 1; i < contentEnd; ++i) {
                if (static_cast<uchar>(ptr[i]) >= 0x80) {
                    allAscii = false;
                    break;
                }
            }
            if (allAscii) {
                const int softLines = qMax(1, (lineLen * asciiCharWidth + viewWidth - 1) / viewWidth);
                linePixelHeight = softLines * lineHeight;
            } else {
                const QString lineText = QString::fromUtf8(ptr + scanPos + 1, lineLen);
                linePixelHeight = fm.size(Qt::TextWordWrap, lineText).height();
            }
        } else {
            linePixelHeight = lineHeight; // 空行
        }

        accumulatedHeight += linePixelHeight;

        // 超过一屏：回退到当前行开头（换行符之后），返回刚好填满的字节数
        if (accumulatedHeight > viewHeight) {
            scanPos++;
            return totalSize - scanPos;
        }

        lineEnd = contentEnd;
        // 跳过回车符，继续往前扫
        if (contentEnd < scanPos) {
            scanPos--;
        }
    }

    // ========== 结算最顶部第一行（前面无换行符） ==========
    const int firstLineLen = lineEnd;
    int firstLineHeight;
    if (firstLineLen > 0) {
        bool allAscii = true;
        for (int i = 0; i < firstLineLen; ++i) {
            if (static_cast<uchar>(ptr[i]) >= 0x80) {
                allAscii = false;
                break;
            }
        }
        if (allAscii) {
            const int softLines = qMax(1, (firstLineLen * asciiCharWidth + viewWidth - 1) / viewWidth);
            firstLineHeight = softLines * lineHeight;
        } else {
            const QString firstLine = QString::fromUtf8(ptr, firstLineLen);
            firstLineHeight = fm.size(Qt::TextWordWrap, firstLine).height();
        }
    } else {
        firstLineHeight = lineHeight;
    }

    accumulatedHeight += firstLineHeight;

    // 全部数据都放得下
    if (accumulatedHeight <= viewHeight) {
        return totalSize;
    }

    return totalSize - scanPos;
}
#endif
