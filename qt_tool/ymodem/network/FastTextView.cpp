#include "FastTextView.h"

#include <QFont>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShowEvent>
#include <QTextBlock>
#include <QTextDocument>
#include <QWheelEvent>
#include <QtMath>

static const int kMaxLogBytes = 8 * 1024 * 1024;
static const int kDisplayWindowBytes = 6 * 1024;
static const int kHorizontalPadding = 8;
static const int kMinBytesPerLine = 8;
static const int kMaxAdjustTimes = 5;

FastTextView::FastTextView(QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    QFont font(QStringLiteral("Consolas"), 10);
    font.setStyleHint(QFont::Monospace);
    m_edit = new QPlainTextEdit(this);
    m_edit->setReadOnly(true);
    m_edit->setFont(font);
    m_edit->setUndoRedoEnabled(false);
    m_edit->setMaximumBlockCount(0);
    m_edit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_edit->setLineWrapMode(QPlainTextEdit::WidgetWidth);

    m_scroll = new QScrollBar(Qt::Vertical, this);
    lay->addWidget(m_edit, 1);
    lay->addWidget(m_scroll);

    connect(m_scroll, &QScrollBar::valueChanged, this, &FastTextView::onScroll);
    m_edit->viewport()->installEventFilter(this);
}

void FastTextView::setViewMode(ViewMode mode)
{
    if (m_viewMode == mode)
        return;
    m_viewMode = mode;
    refresh();
}

FastTextView::ViewMode FastTextView::viewMode() const
{
    return m_viewMode;
}

void FastTextView::setLineWrap(bool wrap)
{
    m_edit->setLineWrapMode(wrap ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    refresh();
}

void FastTextView::trimIfNeeded()
{
    if (m_data.size() <= kMaxLogBytes)
        return;
    const int drop = m_data.size() - kMaxLogBytes;
    m_data.remove(0, drop);
    const int newValue = qMax(0, m_scroll->value() - drop);
    m_scroll->blockSignals(true);
    m_scroll->setMaximum(m_data.size());
    m_scroll->setValue(newValue);
    m_scroll->blockSignals(false);
}

void FastTextView::addData(QByteArray data)
{
    if (data.isEmpty())
        return;
    const bool first = m_data.isEmpty();
    m_data.append(data);
    trimIfNeeded();
    if (!m_inited)
        return;

    const int readSize = calcReadSize();
    m_scroll->blockSignals(true);
    m_scroll->setMaximum(m_data.size());
    m_scroll->setPageStep(qMax(1, readSize / 2));
    m_scroll->setSingleStep(qMax(1, readSize / 20));
    m_scroll->blockSignals(false);

    if (first || readSize >= m_data.size())
        onScroll(m_scroll->value());
}

void FastTextView::clear()
{
    m_data.clear();
    if (!m_inited)
        return;
    m_edit->clear();
    m_scroll->setRange(0, 0);
}

void FastTextView::scrollToTop()
{
    if (!m_inited || m_data.isEmpty())
        return;
    m_scroll->setValue(0);
}

void FastTextView::scrollToBottom()
{
    if (!m_inited || m_data.isEmpty())
        return;
    m_scroll->setValue(m_scroll->maximum());
}

int FastTextView::totalVisualLineCount() const
{
    QTextDocument *doc = m_edit->document();
    const int lineH = QFontMetrics(m_edit->font()).lineSpacing();
    return qCeil(doc->size().height() / lineH);
}

int FastTextView::emptyLineCount() const
{
    int count = 0;
    QTextBlock block = m_edit->document()->begin();
    while (block.isValid()) {
        if (block.text().isEmpty())
            ++count;
        block = block.next();
    }
    return count;
}

bool FastTextView::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_edit->viewport() && event->type() == QEvent::Wheel) {
        auto *wheel = static_cast<QWheelEvent *>(event);
        const int delta = wheel->angleDelta().y();
        int step = qMin(1024, m_scroll->singleStep());
        if (m_wheelStep)
            step = m_wheelStep;
        m_scroll->setValue(m_scroll->value() + (delta > 0 ? -step : step));
        m_wheelStep = 0;
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

int FastTextView::calcReadSize() const
{
    const int h = m_edit->viewport()->height();
    return h > 0 ? h * 20 : 2400;
}

void FastTextView::refresh()
{
    if (!m_inited || m_data.isEmpty()) {
        m_edit->clear();
        m_scroll->setRange(0, 0);
        return;
    }
    const int readSize = calcReadSize();
    m_scroll->blockSignals(true);
    m_scroll->setRange(0, m_data.size());
    m_scroll->setPageStep(qMax(1, readSize / 2));
    m_scroll->setSingleStep(qMax(1, readSize / 20));
    m_scroll->blockSignals(false);
    onScroll(m_scroll->value());
}

void FastTextView::setData(const QByteArray &data)
{
    m_data = data;
    trimIfNeeded();
    m_scroll->setValue(0);
    refresh();
}

const QByteArray &FastTextView::getData() const
{
    return m_data;
}

void FastTextView::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    refresh();
}

void FastTextView::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    if (!m_inited) {
        m_inited = true;
        refresh();
    }
}

void FastTextView::onScroll(int byteOffset)
{
    if (m_data.isEmpty()) {
        m_edit->clear();
        return;
    }

    int readSize = calcReadSize();
    byteOffset = qBound(0, byteOffset, static_cast<int>(m_data.size()) - 1);
    readSize = qMin(readSize, static_cast<int>(m_data.size() - byteOffset));
    const int dataOffset = byteOffset;

    if (byteOffset > (m_data.size() - kDisplayWindowBytes))
        byteOffset = qMax(0, m_data.size() - kDisplayWindowBytes);
    if (readSize < kDisplayWindowBytes)
        readSize = kDisplayWindowBytes;

    const QByteArray chunk = m_data.mid(byteOffset, readSize);
    QString showText;
    if (m_viewMode == HexMode) {
        static const char hexTable[] = "0123456789ABCDEF";
        showText.reserve(chunk.size() * 3);
        for (int i = 0; i < chunk.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(chunk.at(i));
            showText.append(QLatin1Char(hexTable[(c >> 4) & 0x0F]));
            showText.append(QLatin1Char(hexTable[c & 0x0F]));
            showText.append(QLatin1Char(' '));
        }
    } else {
        showText = QString::fromUtf8(chunk);
    }
    m_edit->setPlainText(showText);

    qreal progress = 0;
    if (m_scroll->maximum() > 0) {
        progress = static_cast<qreal>(dataOffset) / m_scroll->maximum();
        if (m_scroll->value() + 2 * kDisplayWindowBytes > m_scroll->maximum())
            m_wheelStep = m_scroll->value() + 2 * kDisplayWindowBytes - m_scroll->maximum();
    }
    QScrollBar *innerSb = m_edit->verticalScrollBar();
    innerSb->setValue(static_cast<int>(innerSb->maximum() * progress));
}

int FastTextView::calcHexPageBytes(int fixedBytesPerLine) const
{
    const QFontMetrics fontMetrics(m_edit->font());
    const int viewportHeight = m_edit->viewport()->height();
    const int lineHeight = fontMetrics.lineSpacing();
    const int visibleLineCount = lineHeight > 0 ? viewportHeight / lineHeight : 0;

    if (fixedBytesPerLine > 0)
        return visibleLineCount * fixedBytesPerLine;

    const int viewportWidth = m_edit->viewport()->width() - kHorizontalPadding;
    const int hexByteWidth = qMax(1, fontMetrics.horizontalAdvance(QStringLiteral("FF ")));
    const int autoBytesPerLine = qMax(kMinBytesPerLine, viewportWidth / hexByteWidth);
    return visibleLineCount * autoBytesPerLine;
}

int FastTextView::calcTextPageBytes() const
{
    if (m_data.isEmpty())
        return 0;

    const QFontMetrics fontMetrics(m_edit->font());
    const int viewportHeight = m_edit->viewport()->height();
    const int lineHeight = fontMetrics.lineSpacing();
    const int visibleLineCount = lineHeight > 0 ? viewportHeight / lineHeight : 0;
    const int viewportWidth = m_edit->viewport()->width() - kHorizontalPadding;
    const int singleCharWidth = qMax(1, fontMetrics.horizontalAdvance(QLatin1Char('0')));
    const int charsPerLine = qMax(1, viewportWidth / singleCharWidth);
    int estimatedPageBytes = visibleLineCount * charsPerLine;
    if (estimatedPageBytes >= m_data.size())
        return m_data.size();

    int startOffset = qMax(0, m_data.size() - estimatedPageBytes);
    QString pageText = QString::fromUtf8(m_data.mid(startOffset));
    int actualLineCount = lineHeight > 0
                              ? fontMetrics.size(Qt::TextWordWrap, pageText).height() / lineHeight
                              : 0;

    int adjustTimes = 0;
    while (actualLineCount < visibleLineCount && startOffset > 0 && adjustTimes < kMaxAdjustTimes) {
        startOffset = qMax(0, startOffset - charsPerLine);
        pageText = QString::fromUtf8(m_data.mid(startOffset));
        actualLineCount = fontMetrics.size(Qt::TextWordWrap, pageText).height() / lineHeight;
        ++adjustTimes;
    }
    while (actualLineCount > visibleLineCount + 1 && adjustTimes < kMaxAdjustTimes) {
        startOffset += charsPerLine / 2;
        pageText = QString::fromUtf8(m_data.mid(startOffset));
        actualLineCount = fontMetrics.size(Qt::TextWordWrap, pageText).height() / lineHeight;
        ++adjustTimes;
    }
    return m_data.size() - startOffset;
}

int FastTextView::calcTextPageBytes(const QByteArray &data) const
{
    if (data.isEmpty())
        return 0;

    const QFontMetrics fm(m_edit->font());
    const int viewWidth = qMax(1, m_edit->viewport()->width() - kHorizontalPadding);
    const int viewHeight = m_edit->viewport()->height();
    const int lineHeight = qMax(1, fm.lineSpacing());
    const int asciiCharWidth = qMax(1, fm.horizontalAdvance(QLatin1Char('0')));
    const char *const ptr = data.constData();
    const int totalSize = data.size();

    int accumulatedHeight = 0;
    int scanPos = totalSize;
    int lineEnd = totalSize;

    while (scanPos > 0) {
        --scanPos;
        if (ptr[scanPos] != '\n')
            continue;

        int contentEnd = scanPos;
        if (scanPos > 0 && ptr[scanPos - 1] == '\r')
            --contentEnd;

        const int lineLen = contentEnd - (scanPos + 1);
        int linePixelHeight;
        if (lineLen > 0) {
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
            linePixelHeight = lineHeight;
        }

        accumulatedHeight += linePixelHeight;
        if (accumulatedHeight > viewHeight) {
            ++scanPos;
            return totalSize - scanPos;
        }
        lineEnd = contentEnd;
        if (contentEnd < scanPos)
            --scanPos;
    }

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
    if (accumulatedHeight <= viewHeight)
        return totalSize;
    return totalSize - scanPos;
}
