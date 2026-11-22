#include "FastTextView.h"

#include <QFont>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShowEvent>
#include <QTextCursor>
#include <QTimer>
#include <QWheelEvent>
#include <cstring>

static const int kMaxLogBytes = 8 * 1024 * 1024;
static const int kPaintIntervalMs = 16;
static const int kMaxShownChars = 32 * 1024;
static const int kHorizontalPadding = 8;
static const quint8 kPieceTime = 1;
static const quint8 kPieceText = 2;
static const quint8 kPieceData = 3;

static void appendUtf8Text(QString *text, const char *ptr, int length, int *col, int maxCols)
{
    int i = 0;
    while (i < length) {
        if (ptr[i] == '\n') {
            text->append(QLatin1Char('\n'));
            *col = 0;
            ++i;
            continue;
        }
        int room = maxCols - *col;
        if (room <= 0) {
            text->append(QLatin1Char('\n'));
            *col = 0;
            room = maxCols;
        }
        int piece = 0;
        while (i + piece < length && ptr[i + piece] != '\n' && piece < room)
            ++piece;
        if (piece <= 0)
            break;
        int use = piece;
        if (i + use < length && ptr[i + use] != '\n') {
            int cut = use;
            while (cut > 0 && (static_cast<unsigned char>(ptr[i + cut - 1]) & 0xC0) == 0x80)
                --cut;
            if (cut > 0 && cut < use) {
                const unsigned char lead = static_cast<unsigned char>(ptr[i + cut - 1]);
                int need = 1;
                if ((lead & 0xE0) == 0xC0)
                    need = 2;
                else if ((lead & 0xF0) == 0xE0)
                    need = 3;
                else if ((lead & 0xF8) == 0xF0)
                    need = 4;
                if (cut - 1 + need > use)
                    use = qMax(1, cut - 1);
            }
        }
        text->append(QString::fromUtf8(ptr + i, use));
        *col += use;
        i += piece;
    }
}

static void appendLocalText(QString *text, const QByteArray &bytes, int *col, int maxCols)
{
    const QString decoded = QString::fromLocal8Bit(bytes);
    for (int i = 0; i < decoded.size(); ++i) {
        const QChar ch = decoded.at(i);
        if (ch == QLatin1Char('\n')) {
            text->append(ch);
            *col = 0;
            continue;
        }
        if (*col >= maxCols) {
            text->append(QLatin1Char('\n'));
            *col = 0;
        }
        text->append(ch);
        ++(*col);
    }
}

static void appendHexText(QString *text, const char *ptr, int length, int *col, int maxCols)
{
    static const char hexTable[] = "0123456789ABCDEF";
    const int room = qMax(3, maxCols);
    for (int i = 0; i < length; ++i) {
        if (*col + 3 > room && *col > 0) {
            text->append(QLatin1Char('\n'));
            *col = 0;
        }
        const unsigned char c = static_cast<unsigned char>(ptr[i]);
        text->append(QLatin1Char(hexTable[(c >> 4) & 0x0F]));
        text->append(QLatin1Char(hexTable[c & 0x0F]));
        text->append(QLatin1Char(' '));
        *col += 3;
    }
}

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

    connect(m_scroll, &QScrollBar::valueChanged, this, [this](int value) {
        m_followBottom = (m_scroll->maximum() <= 0) || (value >= m_scroll->maximum() - 1);
        onScroll(value);
    });
    m_edit->viewport()->installEventFilter(this);

    m_paintTimer = new QTimer(this);
    m_paintTimer->setSingleShot(true);
    m_paintTimer->setInterval(kPaintIntervalMs);  // 一帧画一次，这一帧里的包合并成一屏
    connect(m_paintTimer, &QTimer::timeout, this, &FastTextView::flushPendingPaint);
}

void FastTextView::setLineWrap(bool wrap)
{
    m_edit->setLineWrapMode(wrap ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    refresh();
}

void FastTextView::setFollowBottom(bool follow)
{
    m_followBottom = follow;
}

void FastTextView::setLocal8Bit(bool local8Bit)
{
    if (m_local8Bit == local8Bit)
        return;
    m_local8Bit = local8Bit;
    invalidateShown();
    refresh();
}

void FastTextView::setHexDisplay(bool on)
{
    if (m_hexDisplay == on)
        return;
    m_hexDisplay = on;
    invalidateShown();
    refresh();
}

void FastTextView::setShowTimestamp(bool on)
{
    if (m_showTimestamp == on)
        return;
    m_showTimestamp = on;
    invalidateShown();
    refresh();
}

bool FastTextView::isEmpty() const
{
    return m_count == 0;
}

void FastTextView::ensureRing()
{
    if (m_ring.size() != kMaxLogBytes)
        m_ring.resize(kMaxLogBytes);
}

int FastTextView::appendBytes(const QByteArray &data)
{
    ensureRing();
    const int cap = m_ring.size();
    const int n = data.size();
    if (n <= 0)
        return 0;
    if (n >= cap) {
        const int skip = n - cap;
        memcpy(m_ring.data(), data.constData() + skip, cap);
        m_head = 0;
        m_count = cap;
        invalidateShown();
        if (m_scroll) {
            m_scroll->blockSignals(true);
            m_scroll->setRange(0, m_count);
            m_scroll->setValue(m_followBottom ? m_count : 0);
            m_scroll->blockSignals(false);
        }
        return m_count + skip;
    }

    int drop = 0;
    if (m_count + n > cap) {
        drop = m_count + n - cap;
        m_head = (m_head + drop) % cap;
        m_count -= drop;
    }
    int pos = (m_head + m_count) % cap;
    const int first = qMin(n, cap - pos);
    memcpy(m_ring.data() + pos, data.constData(), first);
    if (first < n)
        memcpy(m_ring.data(), data.constData() + first, n - first);
    m_count += n;
    if (drop > 0)
        noteDropped(drop);
    return drop;
}

void FastTextView::noteDropped(int drop)
{
    if (drop <= 0)
        return;
    const int shownEnd = m_shownStart + m_shownLength;
    if (m_shownStart >= 0) {
        if (shownEnd <= drop) {
            invalidateShown();
        } else {
            const int newEnd = shownEnd - drop;
            m_shownStart = qMax(0, m_shownStart - drop);
            m_shownLength = newEnd - m_shownStart;
        }
    }
    if (!m_scroll)
        return;
    const int value = m_followBottom ? m_count : qMax(0, m_scroll->value() - drop);
    m_scroll->blockSignals(true);
    m_scroll->setMaximum(m_count);
    m_scroll->setValue(qMin(value, m_count));
    m_scroll->blockSignals(false);
}

char FastTextView::logByte(int logical) const
{
    return m_ring.at((m_head + logical) % m_ring.size());
}

QByteArray FastTextView::logSlice(int start, int length) const
{
    if (length <= 0 || start < 0 || start >= m_count || m_ring.isEmpty())
        return QByteArray();
    length = qMin(length, m_count - start);
    QByteArray out;
    out.resize(length);
    const int cap = m_ring.size();
    int pos = (m_head + start) % cap;
    const int first = qMin(length, cap - pos);
    memcpy(out.data(), m_ring.constData() + pos, first);
    if (first < length)
        memcpy(out.data() + first, m_ring.constData(), length - first);
    return out;
}

void FastTextView::appendPiece(const QByteArray &bytes, quint8 kind)
{
    if (bytes.isEmpty())
        return;
    if (isAtBottom())
        m_followBottom = true;
    ensureRing();
    if (bytes.size() >= m_ring.size()) {
        appendBytes(bytes);
        m_pieces.clear();
        m_pieceBegin = 0;
        Piece piece;
        piece.start = 0;
        piece.length = m_count;
        piece.kind = kind;
        m_pieces.append(piece);
        schedulePaint();
        return;
    }
    const int drop = appendBytes(bytes);
    if (drop > 0)
        shiftPieces(drop);
    Piece piece;
    piece.start = m_count - bytes.size();
    piece.length = bytes.size();
    piece.kind = kind;
    m_pieces.append(piece);
    schedulePaint();
}

void FastTextView::shiftPieces(int drop)
{
    if (drop <= 0)
        return;
    while (m_pieceBegin < m_pieces.size()) {
        const Piece &piece = m_pieces.at(m_pieceBegin);
        if (piece.start + piece.length > drop)
            break;
        ++m_pieceBegin;
    }
    for (int i = m_pieceBegin; i < m_pieces.size(); ++i) {
        Piece &piece = m_pieces[i];
        if (piece.start >= drop) {
            piece.start -= drop;
        } else {
            piece.length -= drop - piece.start;
            piece.start = 0;
        }
    }
    if (m_pieceBegin > 2048 && m_pieceBegin * 2 >= m_pieces.size()) {
        m_pieces.erase(m_pieces.begin(), m_pieces.begin() + m_pieceBegin);
        m_pieceBegin = 0;
    }
}

int FastTextView::pieceIndexAt(int logical) const
{
    int lo = m_pieceBegin;
    int hi = m_pieces.size();
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        const Piece &piece = m_pieces.at(mid);
        if (piece.start + piece.length <= logical)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

int FastTextView::kindAt(int logical) const
{
    if (logical < 0 || logical >= m_count)
        return -1;
    const int index = pieceIndexAt(logical);
    if (index < m_pieceBegin || index >= m_pieces.size())
        return -1;
    const Piece &piece = m_pieces.at(index);
    if (logical < piece.start || logical >= piece.start + piece.length)
        return -1;
    return piece.kind;
}

void FastTextView::addTimestamp(const QString &text)
{
    appendPiece(text.toUtf8(), kPieceTime);
}

void FastTextView::addText(const QString &text)
{
    appendPiece(text.toUtf8(), kPieceText);
}

void FastTextView::addData(QByteArray data)
{
    appendPiece(data, kPieceData);
}

void FastTextView::clear()
{
    m_head = 0;
    m_count = 0;
    m_linear.clear();
    m_pieces.clear();
    m_pieceBegin = 0;
    m_followBottom = true;
    invalidateShown();
    if (m_paintTimer)
        m_paintTimer->stop();
    if (!m_inited)
        return;
    m_edit->clear();
    m_scroll->setRange(0, 0);
}

void FastTextView::scrollToTop()
{
    m_followBottom = false;
    if (!m_inited || m_count == 0)
        return;
    m_scroll->setValue(0);
}

void FastTextView::scrollToBottom()
{
    m_followBottom = true;
    schedulePaint();
}

bool FastTextView::isAtBottom() const
{
    if (!m_inited || m_count == 0)
        return true;
    return m_scroll->value() >= m_scroll->maximum() - 1;
}

void FastTextView::schedulePaint()
{
    if (!m_inited)
        return;
    if (!m_paintTimer->isActive())
        m_paintTimer->start();
}

void FastTextView::flushPendingPaint()
{
    if (!m_inited)
        return;
    const int readSize = calcReadSize();
    m_scroll->blockSignals(true);
    m_scroll->setRange(0, m_count);
    m_scroll->setPageStep(qMax(1, readSize / 2));
    m_scroll->setSingleStep(qMax(1, readSize / 20));
    if (m_followBottom)
        m_scroll->setValue(m_scroll->maximum());
    m_scroll->blockSignals(false);
    onScroll(m_scroll->value());
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

int FastTextView::columnCount() const
{
    const QFontMetrics fm(m_edit->font());
    const int charW = qMax(1, fm.horizontalAdvance(QLatin1Char('0')));
    const int width = m_edit->viewport()->width() - kHorizontalPadding;
    if (width <= 0)
        return 80;
    return qMax(8, width / charW);
}

int FastTextView::rowCount() const
{
    const int lineH = qMax(1, QFontMetrics(m_edit->font()).lineSpacing());
    const int height = m_edit->viewport()->height();
    if (height <= 0)
        return 24;
    return qMax(1, height / lineH);
}

void FastTextView::invalidateShown()
{
    m_shownStart = -1;
    m_shownLength = 0;
    m_shownWidth = -1;
    m_tailShown = false;
}

int FastTextView::calcReadSize() const
{
    const bool wrap = m_edit->lineWrapMode() == QPlainTextEdit::WidgetWidth;
    const int cols = wrap ? columnCount() : qMin(2048, qMax(columnCount(), 256));
    const int chars = qBound(64, rowCount() * cols, kMaxShownChars);
    if (m_hexDisplay)
        return qMax(16, chars / 3);
    return chars;
}

void FastTextView::refresh()
{
    if (!m_inited || m_count == 0) {
        m_edit->clear();
        m_scroll->setRange(0, 0);
        return;
    }
    const int readSize = calcReadSize();
    m_scroll->blockSignals(true);
    m_scroll->setRange(0, m_count);
    m_scroll->setPageStep(qMax(1, readSize / 2));
    m_scroll->setSingleStep(qMax(1, readSize / 20));
    m_scroll->blockSignals(false);
    onScroll(m_scroll->value());
}

const QByteArray &FastTextView::getData() const
{
    m_linear = formatWindow(0, m_count).toUtf8();
    return m_linear;
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

int FastTextView::lineStartAt(int pos) const
{
    const bool wrap = m_edit->lineWrapMode() == QPlainTextEdit::WidgetWidth;
    const int maxCols = wrap ? columnCount() : qMin(2048, qMax(columnCount(), 256));
    int col = 0;
    int i = pos;
    while (i > 0 && col < maxCols) {
        const int kind = kindAt(i - 1);
        if (kind < 0)
            break;
        if (kind == kPieceTime && !m_showTimestamp) {
            --i;
            continue;
        }
        const unsigned char c = static_cast<unsigned char>(logByte(i - 1));
        if (kind == kPieceData && m_hexDisplay) {
            col += 3;
            --i;
            continue;
        }
        if (c == '\n')
            break;
        ++col;
        --i;
    }
    return i;
}

QString FastTextView::formatWindow(int start, int length) const
{
    if (length <= 0 || start < 0 || start >= m_count)
        return QString();
    const int end = qMin(m_count, start + length);
    const bool wrap = m_edit->lineWrapMode() == QPlainTextEdit::WidgetWidth;
    const int maxCols = qMax(1, wrap ? columnCount() : qMin(2048, qMax(columnCount(), 256)));
    QString text;
    text.reserve(qMin(length * (m_hexDisplay ? 3 : 1) + 8, kMaxShownChars + 8));
    int col = 0;
    for (int index = pieceIndexAt(start); index < m_pieces.size(); ++index) {
        const Piece &piece = m_pieces.at(index);
        if (piece.start >= end)
            break;
        const int from = qMax(start, piece.start);
        const int to = qMin(end, piece.start + piece.length);
        if (to <= from)
            continue;
        if (piece.kind == kPieceTime && !m_showTimestamp)
            continue;
        const QByteArray slice = logSlice(from, to - from);
        if (piece.kind == kPieceData && m_hexDisplay)
            appendHexText(&text, slice.constData(), slice.size(), &col, maxCols);
        else if (piece.kind == kPieceData && m_local8Bit)
            appendLocalText(&text, slice, &col, maxCols);
        else
            appendUtf8Text(&text, slice.constData(), slice.size(), &col, maxCols);
    }
    return text;
}

void FastTextView::onScroll(int byteOffset)
{
    if (m_count == 0) {
        m_edit->clear();
        invalidateShown();
        return;
    }

    const int budget = calcReadSize();
    const bool wrap = m_edit->lineWrapMode() == QPlainTextEdit::WidgetWidth;
    const int rows = rowCount() + 1;
    const int anchor = m_followBottom ? qMax(0, m_count - budget) : qBound(0, byteOffset, m_count);
    int start = anchor > 0 ? lineStartAt(anchor) : 0;
    const int length = m_followBottom
                           ? qMax(0, m_count - start)
                           : qMin(m_count - start, budget + qMax(0, anchor - start));

    const int width = m_edit->viewport()->width();
    const int shownEnd = m_shownStart + m_shownLength;
    const bool sameMode = wrap == m_shownWrap && width == m_shownWidth
        && m_hexDisplay == m_shownHex && m_showTimestamp == m_shownStamp
        && m_local8Bit == m_shownLocal;
    if (m_followBottom && m_tailShown && shownEnd >= 0 && shownEnd <= m_count && sameMode) {
        const int extra = m_count - shownEnd;
        if (extra > 0) {
            const QString added = formatWindow(shownEnd, extra);
            m_edit->setMaximumBlockCount(qMax(8, rows + 2));
            QTextCursor cursor(m_edit->document());
            cursor.movePosition(QTextCursor::End);
            cursor.insertText(added);
            m_edit->verticalScrollBar()->setValue(m_edit->verticalScrollBar()->maximum());
        }
        m_shownLength += m_count - shownEnd;
        return;
    }
    if (start == m_shownStart && length == m_shownLength && sameMode && m_tailShown == m_followBottom)
        return;

    const QString showText = formatWindow(start, length);
    m_edit->setUpdatesEnabled(false);
    m_edit->setMaximumBlockCount(m_followBottom ? qMax(8, rows + 2) : 0);
    m_edit->setPlainText(showText);
    QScrollBar *inner = m_edit->verticalScrollBar();
    inner->setValue(m_followBottom ? inner->maximum() : 0);
    m_edit->setUpdatesEnabled(true);

    m_shownStart = start;
    m_shownLength = length;
    m_shownWrap = wrap;
    m_shownWidth = width;
    m_shownHex = m_hexDisplay;
    m_shownStamp = m_showTimestamp;
    m_shownLocal = m_local8Bit;
    m_tailShown = m_followBottom;

    if (m_scroll->maximum() > 0 && m_scroll->value() + budget > m_scroll->maximum())
        m_wheelStep = qMax(1, m_scroll->maximum() - m_scroll->value());
    else
        m_wheelStep = 0;
}
