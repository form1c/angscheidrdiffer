#include "difftextedit.h"

#include <QAbstractTextDocumentLayout>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QFileInfo>
#include <QFontDatabase>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextLayout>

namespace {

class LineNumberArea : public QWidget
{
public:
    explicit LineNumberArea(DiffTextEdit *editor)
        : QWidget(editor), m_editor(editor) {}

    QSize sizeHint() const override
    {
        return {m_editor->lineNumberAreaWidth(), 0};
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        m_editor->lineNumberAreaPaintEvent(event);
    }

private:
    DiffTextEdit *m_editor;
};

} // namespace

DiffTextEdit::DiffTextEdit(QWidget *parent)
    : QPlainTextEdit(parent)
{
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setAcceptDrops(true);

    m_lineNumberArea = new LineNumberArea(this);

    connect(this, &QPlainTextEdit::blockCountChanged,
            this, &DiffTextEdit::updateLineNumberAreaWidth);
    connect(this, &QPlainTextEdit::updateRequest,
            this, &DiffTextEdit::updateLineNumberArea);

    // Ghost-Modus zeichnet Cursor/Selektion selbst → volle Repaints.
    const auto ghostRepaint = [this]() {
        if (ghostModeActive()) {
            viewport()->update();
            m_lineNumberArea->update();
        }
    };
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, ghostRepaint);
    connect(this, &QPlainTextEdit::selectionChanged, this, ghostRepaint);

    // The base class only knows real lines. After every range change the
    // placeholder compensation is added. This runs BEFORE setRange clamps
    // the value, so the scroll position is preserved.
    connect(verticalScrollBar(), &QScrollBar::rangeChanged,
            this, [this](int, int max) {
                if (m_adjustingRange)
                    return;
                m_baseScrollMax = max;
                updateGhostScrollRange();
            });

    updateLineNumberAreaWidth();
}

// ---------------------------------------------------------------------------
// Placeholder mode: placeholder rows as a pure rendering layer
// ---------------------------------------------------------------------------

void DiffTextEdit::setGhostLines(const QHash<int, int> &ghostAfter)
{
    m_ghostAfter = ghostAfter;
    updateGhostScrollRange();
    viewport()->update();
    m_lineNumberArea->update();
}

void DiffTextEdit::updateGhostScrollRange()
{
    QScrollBar *bar = verticalScrollBar();
    if (!ghostModeActive()) {
        if (bar->maximum() != m_baseScrollMax) {
            m_adjustingRange = true;
            bar->setMaximum(m_baseScrollMax);
            m_adjustingRange = false;
        }
        return;
    }

    // Smallest real start line from which all remaining visual rows, real
    // ones and placeholders, fit into the viewport. Walks backwards and
    // stops after at most viewportRows real lines.
    const int h = qMax(1, fontMetrics().lineSpacing());
    const int viewportRows = qMax(1, viewport()->height() / h);
    const int blockCount = document()->blockCount();
    int rows = 0;
    int desired = 0;
    for (int line = blockCount - 1; line >= 0; --line) {
        rows += 1 + m_ghostAfter.value(line, 0);
        if (rows > viewportRows) {
            desired = line + 1;
            break;
        }
    }
    const int wanted = qMax(m_baseScrollMax, desired);
    if (bar->maximum() != wanted) {
        m_adjustingRange = true;
        bar->setMaximum(wanted);
        m_adjustingRange = false;
    }
}

void DiffTextEdit::setDiffLineColors(const QHash<int, QColor> &lineBackgrounds)
{
    m_lineBg = lineBackgrounds;
    viewport()->update();
}

void DiffTextEdit::setCharHighlights(const QList<CharHighlight> &highlights)
{
    m_charHighlights = highlights;
    viewport()->update();
}

bool DiffTextEdit::ghostModeActive() const
{
    return !m_ghostAfter.isEmpty()
           && lineWrapMode() == QPlainTextEdit::NoWrap;
}

// ---------------------------------------------------------------------------
// Section-Rahmen (aktive Klick-Auswahl)
// ---------------------------------------------------------------------------

void DiffTextEdit::setSectionFrame(int startLine, int lineCount)
{
    if (m_frameStart == startLine && m_frameCount == lineCount)
        return;
    m_frameStart = startLine;
    m_frameCount = lineCount;
    viewport()->update();
}

void DiffTextEdit::drawSectionFrame(QPainter &painter)
{
    if (m_frameStart < 0)
        return;

    const int h  = fontMetrics().lineSpacing();
    const int y1 = lineTopY(m_frameStart);
    const int y2 = ghostModeActive()
        ? y1 + m_frameCount * h
        : (m_frameCount > 0 ? lineTopY(m_frameStart + m_frameCount) : y1);
    if (y2 < 0 || y1 > viewport()->height())
        return;

    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QPen(palette().color(QPalette::Highlight), 2));
    painter.setBrush(Qt::NoBrush);
    if (m_frameCount == 0) {
        // A pure insertion point: a thick line instead of a frame.
        painter.drawLine(1, y1, viewport()->width() - 2, y1);
    } else {
        painter.drawRect(QRect(1, y1 + 1,
                               viewport()->width() - 3, y2 - y1 - 2));
    }
}

void DiffTextEdit::scrollContentsBy(int dx, int dy)
{
    QPlainTextEdit::scrollContentsBy(dx, dy);
    // The placeholder layout and the section frame do NOT move uniformly
    // with the pixel blit of the base class, so repaint everything.
    if (ghostModeActive() || m_frameStart >= 0) {
        viewport()->update();
        m_lineNumberArea->update();
    }
}

QList<DiffTextEdit::VisualSlot> DiffTextEdit::visibleSlots() const
{
    QList<VisualSlot> rows;
    const int h = fontMetrics().lineSpacing();
    const int height = viewport()->height();
    const int blockCount = document()->blockCount();

    int line = verticalScrollBar()->value(); // first visible real line
    int y = 0;

    // Show a gap before line 0 only when scrolled to the very top.
    if (line == 0) {
        const int lead = m_ghostAfter.value(-1, 0);
        for (int k = 0; k < lead && y < height; ++k) {
            rows.append({y, true, 0});
            y += h;
        }
    }

    while (y < height && line < blockCount) {
        rows.append({y, false, line});
        y += h;
        const int ghosts = m_ghostAfter.value(line, 0);
        for (int k = 0; k < ghosts && y < height; ++k) {
            rows.append({y, true, line});
            y += h;
        }
        ++line;
    }
    return rows;
}

void DiffTextEdit::paintEvent(QPaintEvent *event)
{
    if (!ghostModeActive()) {
        QPlainTextEdit::paintEvent(event);
        if (m_frameStart >= 0) {
            QPainter framePainter(viewport());
            drawSectionFrame(framePainter);
        }
        return;
    }

    QPainter painter(viewport());
    painter.fillRect(event->rect(), palette().color(QPalette::Base));

    const int h = fontMetrics().lineSpacing();
    const qreal x = document()->documentMargin()
                    - horizontalScrollBar()->value();
    const int width = viewport()->width();

    const bool dark = palette().color(QPalette::Base).lightness() < 128;
    const QColor ghostColor = dark ? QColor(255, 255, 255, 26)
                                   : QColor(0, 0, 0, 22);

    const QTextCursor cursor = textCursor();
    const int selStart = cursor.selectionStart();
    const int selEnd   = cursor.selectionEnd();

    const QList<VisualSlot> rows = visibleSlots();
    for (const VisualSlot &slot : rows) {
        if (slot.ghost) {
            painter.fillRect(QRect(0, slot.y, width, h),
                             QBrush(ghostColor, Qt::BDiagPattern));
            continue;
        }

        const QTextBlock block = document()->findBlockByNumber(slot.line);
        if (!block.isValid())
            continue;
        // Force the lazy layout of the block. The base class only lays out
        // sie selbst zeichnet).
        document()->documentLayout()->blockBoundingRect(block);

        const auto bgIt = m_lineBg.constFind(slot.line);
        if (bgIt != m_lineBg.constEnd())
            painter.fillRect(QRect(0, slot.y, width, h), bgIt.value());

        // Within-line colours and the selection as format ranges.
        QList<QTextLayout::FormatRange> ranges;
        for (const CharHighlight &chl : m_charHighlights) {
            if (chl.line != slot.line)
                continue;
            QTextLayout::FormatRange r;
            r.start  = chl.start;
            r.length = chl.length;
            r.format.setBackground(chl.color);
            ranges.append(r);
        }
        if (selEnd > selStart) {
            const int blockStart = block.position();
            const int blockEnd   = blockStart + block.length();
            const int s = qMax(selStart, blockStart);
            const int e = qMin(selEnd, blockEnd);
            if (e > s) {
                QTextLayout::FormatRange r;
                r.start  = s - blockStart;
                r.length = e - s;
                r.format.setBackground(palette().brush(QPalette::Highlight));
                r.format.setForeground(
                    palette().brush(QPalette::HighlightedText));
                ranges.append(r);
            }
        }

        block.layout()->draw(&painter, QPointF(x, slot.y), ranges,
                             event->rect());

        if (hasFocus() && cursor.block() == block && !isReadOnly()) {
            block.layout()->drawCursor(&painter, QPointF(x, slot.y),
                                       cursor.positionInBlock(),
                                       cursorWidth());
        }
    }

    drawSectionFrame(painter);
}

QTextCursor DiffTextEdit::cursorForGhostPosition(const QPoint &pos) const
{
    const int h = fontMetrics().lineSpacing();
    const QList<VisualSlot> rows = visibleSlots();

    if (rows.isEmpty()) {
        QTextCursor c(document());
        c.movePosition(QTextCursor::End);
        return c;
    }

    const int slotIndex = qBound(0, pos.y() / h, int(rows.size()) - 1);
    const int line = rows[slotIndex].line;

    const QTextBlock block = document()->findBlockByNumber(line);
    document()->documentLayout()->blockBoundingRect(block);

    int column = 0;
    if (block.layout()->lineCount() > 0) {
        const qreal xInDoc = pos.x() - document()->documentMargin()
                             + horizontalScrollBar()->value();
        column = block.layout()->lineAt(0).xToCursor(xInDoc);
    }
    QTextCursor c(block);
    c.setPosition(block.position() + column);
    return c;
}

void DiffTextEdit::mousePressEvent(QMouseEvent *event)
{
    if (!ghostModeActive() || event->button() != Qt::LeftButton) {
        QPlainTextEdit::mousePressEvent(event);
        return;
    }
    setTextCursor(cursorForGhostPosition(event->pos()));
    event->accept();
}

void DiffTextEdit::mouseMoveEvent(QMouseEvent *event)
{
    if (!ghostModeActive() || !(event->buttons() & Qt::LeftButton)) {
        QPlainTextEdit::mouseMoveEvent(event);
        return;
    }
    QTextCursor cur = textCursor();
    cur.setPosition(cursorForGhostPosition(event->pos()).position(),
                    QTextCursor::KeepAnchor);
    setTextCursor(cur);
    event->accept();
}

void DiffTextEdit::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (!ghostModeActive() || event->button() != Qt::LeftButton) {
        QPlainTextEdit::mouseDoubleClickEvent(event);
        return;
    }
    QTextCursor c = cursorForGhostPosition(event->pos());
    c.select(QTextCursor::WordUnderCursor);
    setTextCursor(c);
    event->accept();
}

void DiffTextEdit::setDiffSelections(const QList<QTextEdit::ExtraSelection> &selections)
{
    m_diffSelections = selections;
    setExtraSelections(m_diffSelections);
}

void DiffTextEdit::setGapMarkers(const QSet<int> &linesWithGapAfter)
{
    m_gapMarkers = linesWithGapAfter;
    m_lineNumberArea->update();
}

void DiffTextEdit::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu *menu = createStandardContextMenu(event->pos());
    Q_EMIT contextMenuAboutToShow(menu);
    menu->exec(event->globalPos());
    delete menu;
}

void DiffTextEdit::selectedLineRange(int *start, int *count) const
{
    const QTextCursor cur = textCursor();
    const int a = document()->findBlock(cur.selectionStart()).blockNumber();
    int b = document()->findBlock(cur.selectionEnd()).blockNumber();
    // A selection ending exactly at the start of a line excludes that line.
    if (cur.hasSelection()
        && document()->findBlock(cur.selectionEnd()).position() == cur.selectionEnd()
        && b > a) {
        --b;
    }
    if (start) *start = a;
    if (count) *count = b - a + 1;
}

int DiffTextEdit::firstVisibleLine() const
{
    return firstVisibleBlock().blockNumber();
}

int DiffTextEdit::visualRowForLine(int line) const
{
    if (lineWrapMode() == QPlainTextEdit::NoWrap)
        return line;
    int row = 0;
    QTextBlock b = document()->firstBlock();
    for (int i = 0; i < line && b.isValid(); ++i, b = b.next()) {
        // The layout is lazy and has to be forced, or lineCount() is 0.
        document()->documentLayout()->blockBoundingRect(b);
        row += qMax(1, b.layout()->lineCount());
    }
    return row;
}

void DiffTextEdit::scrollToLine(int line)
{
    verticalScrollBar()->setValue(visualRowForLine(line));
}

int DiffTextEdit::lineTopY(int line) const
{
    if (ghostModeActive()) {
        const int h = fontMetrics().lineSpacing();
        const int first = verticalScrollBar()->value();
        const int blockCount = document()->blockCount();
        const int target = qMin(line, blockCount); // blockCount = "hinter Ende"
        if (target < first)
            return -h;
        int y = (first == 0) ? m_ghostAfter.value(-1, 0) * h : 0;
        for (int l = first; l < target; ++l)
            y += h * (1 + m_ghostAfter.value(l, 0));
        return y;
    }

    const QTextBlock block = document()->findBlockByNumber(line);
    if (!block.isValid()) {
        // Position after the last line, for an insertion mark at the end.
        const QTextBlock last = document()->lastBlock();
        const QRectF g = blockBoundingGeometry(last).translated(contentOffset());
        return int(g.bottom());
    }
    const QRectF g = blockBoundingGeometry(block).translated(contentOffset());
    return int(g.top());
}

// ---------------------------------------------------------------------------
// Line number gutter (Qt "Code Editor" example pattern)
// ---------------------------------------------------------------------------

int DiffTextEdit::lineNumberAreaWidth() const
{
    int digits = 1;
    int max = qMax(1, blockCount());
    while (max >= 10) { max /= 10; ++digits; }
    return 8 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void DiffTextEdit::updateLineNumberAreaWidth()
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void DiffTextEdit::updateLineNumberArea(const QRect &rect, int dy)
{
    if (dy)
        m_lineNumberArea->scroll(0, dy);
    else
        m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());

    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth();
}

void DiffTextEdit::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    const QRect cr = contentsRect();
    m_lineNumberArea->setGeometry(
        QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
    // The viewport height enters the placeholder compensation, and
    // rangeChanged does not fire when the base maximum is unchanged.
    updateGhostScrollRange();
}

void DiffTextEdit::lineNumberAreaPaintEvent(QPaintEvent *event)
{
    QPainter painter(m_lineNumberArea);
    painter.fillRect(event->rect(),
                     palette().color(QPalette::Window));

    // Placeholder mode: numbers at the shifted positions, placeholders stay
    // empty. Wedges are unnecessary here because the gaps are visible.
    if (ghostModeActive()) {
        painter.setPen(palette().color(QPalette::PlaceholderText));
        const QList<VisualSlot> rows = visibleSlots();
        for (const VisualSlot &slot : rows) {
            if (slot.ghost)
                continue;
            painter.drawText(0, slot.y, m_lineNumberArea->width() - 4,
                             fontMetrics().height(), Qt::AlignRight,
                             QString::number(slot.line + 1));
        }
        return;
    }

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    const QColor wedgeColor = palette().color(QPalette::PlaceholderText);
    const auto drawWedge = [&](int y) {
        // A small wedge at the gutter edge: the other side has more lines here.
        QPainterPath path;
        const int w = m_lineNumberArea->width();
        path.moveTo(w - 7, y - 3);
        path.lineTo(w - 1, y);
        path.lineTo(w - 7, y + 3);
        path.closeSubpath();
        painter.fillPath(path, wedgeColor);
    };

    // Gap before the first line.
    if (m_gapMarkers.contains(-1) && blockNumber == 0 && top >= event->rect().top())
        drawWedge(qMax(3, top));

    painter.setPen(palette().color(QPalette::PlaceholderText));
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            painter.drawText(0, top, m_lineNumberArea->width() - 4,
                             fontMetrics().height(), Qt::AlignRight,
                             QString::number(blockNumber + 1));
            if (m_gapMarkers.contains(blockNumber))
                drawWedge(bottom);
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

// ---------------------------------------------------------------------------
// File drag & drop
// ---------------------------------------------------------------------------

void DiffTextEdit::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        return;
    }
    QPlainTextEdit::dragEnterEvent(event);
}

void DiffTextEdit::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        return;
    }
    QPlainTextEdit::dragMoveEvent(event);
}

void DiffTextEdit::dropEvent(QDropEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        const QList<QUrl> urls = event->mimeData()->urls();
        if (!urls.isEmpty() && urls.first().isLocalFile()) {
            event->acceptProposedAction();
            const QString path = urls.first().toLocalFile();
            if (QFileInfo(path).isDir())
                Q_EMIT folderDropped(path);
            else
                Q_EMIT fileDropped(path);
            return;
        }
    }
    QPlainTextEdit::dropEvent(event);
}
