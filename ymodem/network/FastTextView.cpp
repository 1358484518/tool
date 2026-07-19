#include "FastTextView.h"
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QHBoxLayout>
#include <QFont>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <QWheelEvent>

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
    m_edit->installEventFilter(this);
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
    int maxOffset = qMax(0, (int)m_data.size() - readSize / 2);

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
    }
    if(calcReadSize()>m_data.size()&&m_lastSizeValue!=m_data.size()){
        m_lastSizeValue = m_data.size();
        onScroll(curVal);
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

bool FastTextView::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_edit && event->type() == QEvent::Wheel) {
        QWheelEvent *wheel = static_cast<QWheelEvent*>(event);
        int delta = wheel->angleDelta().y();
        int step = m_scroll->singleStep(); // 和滚动条单步一致，保证联动
        if (delta > 0) {
            m_scroll->setValue(m_scroll->value() - step * 3); // 滚一下动3步，手感合适
        } else {
            m_scroll->setValue(m_scroll->value() + step * 3);
        }
        // 立刻更新内容+滑块位置
        m_lastScrollValue = m_scroll->value();
        onScroll(m_lastScrollValue);
//        handleWheelEvent(static_cast<QWheelEvent*>(event));
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

int FastTextView::calcReadSize() {
    int h = m_edit->viewport()->height();
    return h > 0 ? h * 20 : 24000;
}
void FastTextView::refresh()
{
    if (!m_inited || m_data.isEmpty()) {
        m_edit->clear();
        m_scroll->setRange(0,0);
        return;
    }
    int readSize = calcReadSize();
    int maxOffset = qMax(0, (int)m_data.size() - readSize/2);
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
#if 1
void FastTextView::onScroll(int byteOffset)
{
    if (m_data.isEmpty()) {
        m_edit->clear();
        return;
    }
    int readSize = calcReadSize();
    byteOffset = qBound(0, byteOffset, (int)m_data.size()-1);
    readSize = qMin(readSize, (int)(m_data.size() - byteOffset));
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
        progress = (qreal)byteOffset / m_scroll->maximum();
    }
    //避免进度条拉到底都不显示最后文本的情况
    QScrollBar *innerSb = m_edit->verticalScrollBar();
    innerSb->setValue(innerSb->maximum() * progress);
}
#else


#endif
