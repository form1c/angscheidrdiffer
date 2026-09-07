#include "diffview.h"
#include "connectorwidget.h"
#include "difftextedit.h"
#include "overviewbar.h"

#include "diff/intralinediff.h"

#include <QHBoxLayout>
#include <QScrollBar>
#include <QTextBlock>
#include <QTimer>

namespace {

struct DiffColors {
    QColor added, removed, changed, intraline, moved, trivial;
};

DiffColors themeColors(const QPalette &pal)
{
    const bool dark = pal.color(QPalette::Base).lightness() < 128;
    if (dark) {
        return {QColor(0x29, 0x44, 0x36),   // added
                QColor(0x4b, 0x2b, 0x2b),   // removed
                QColor(0x4a, 0x4a, 0x2a),   // changed
                QColor(0x6e, 0x6e, 0x20),   // intraline, stronger
                QColor(0x2e, 0x3f, 0x5a),   // moved (blau)
                QColor(0x38, 0x38, 0x38)};  // trivial (grau, dezent)
    }
    return {QColor(0xdd, 0xf5, 0xdd),
            QColor(0xf8, 0xd7, 0xd7),
            QColor(0xfd, 0xf6, 0xc3),
            QColor(0xf5, 0xe0, 0x7a),
            QColor(0xd7, 0xe6, 0xf8),
            QColor(0xe9, 0xe9, 0xe9)};  // trivial (grau, dezent)
}

// Builds a full-width line highlight for 'count' lines from 'startLine'.
void appendLineSelections(QList<QTextEdit::ExtraSelection> &out,
                          QTextDocument *doc, int startLine, int count,
                          const QColor &color)
{
    for (int i = 0; i < count; ++i) {
        const QTextBlock block = doc->findBlockByNumber(startLine + i);
        if (!block.isValid())
            continue;
        QTextEdit::ExtraSelection sel;
        // Span the whole paragraph: with word wrap, a full-width selection
        // would otherwise colour the first visual row only.
        sel.cursor = QTextCursor(block);
        sel.cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        sel.format.setBackground(color);
        sel.format.setProperty(QTextFormat::FullWidthSelection, true);
        out.append(sel);
    }
}

// Replaces the lines [start, start+count) with newLines.
// joinUndo appends to the previous edit block, so that a copy spanning
// mehrere Block-Kopien).
void replaceLines(QTextDocument *doc, int start, int count,
                  const QStringList &newLines, bool joinUndo)
{
    QTextCursor cur(doc);
    if (joinUndo)
        cur.joinPreviousEditBlock();
    else
        cur.beginEditBlock();

    const int blockCount = doc->blockCount();
    if (count > 0) {
        const QTextBlock from = doc->findBlockByNumber(start);
        cur.setPosition(from.position());
        if (start + count < blockCount) {
            const QTextBlock to = doc->findBlockByNumber(start + count);
            cur.setPosition(to.position(), QTextCursor::KeepAnchor);
            cur.insertText(newLines.isEmpty()
                           ? QString()
                           : newLines.join(QLatin1Char('\n')) + QLatin1Char('\n'));
        } else {
            cur.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
            cur.insertText(newLines.join(QLatin1Char('\n')));
            if (newLines.isEmpty() && start > 0)
                cur.deletePreviousChar(); // trailing '\n' of the line before
        }
    } else {
        if (start >= blockCount) {
            cur.movePosition(QTextCursor::End);
            cur.insertText(QLatin1Char('\n') + newLines.join(QLatin1Char('\n')));
        } else {
            cur.setPosition(doc->findBlockByNumber(start).position());
            cur.insertText(newLines.join(QLatin1Char('\n')) + QLatin1Char('\n'));
        }
    }

    cur.endEditBlock();
}

} // namespace

DiffView::DiffView(QWidget *parent)
    : QWidget(parent)
{
    m_left  = new DiffTextEdit(this);
    m_right = new DiffTextEdit(this);
    m_connector = new ConnectorWidget(m_left, m_right, this);
    m_overview  = new OverviewBar(this);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_left, 1);
    layout->addWidget(m_connector);
    layout->addWidget(m_right, 1);
    layout->addWidget(m_overview);

    m_rediffTimer = new QTimer(this);
    m_rediffTimer->setSingleShot(true);
    m_rediffTimer->setInterval(300);
    connect(m_rediffTimer, &QTimer::timeout, this, &DiffView::recompute);

    connect(m_left, &QPlainTextEdit::textChanged, this, [this]() {
        if (!m_applyingEdit) {
            m_lastChangedLeft = true;
            m_rediffTimer->start();
        }
    });
    connect(m_right, &QPlainTextEdit::textChanged, this, [this]() {
        if (!m_applyingEdit) {
            m_lastChangedLeft = false;
            m_rediffTimer->start();
        }
    });

    connect(m_left->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this]() { onScrolled(true); });
    connect(m_right->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this]() { onScrolled(false); });

    // Horizontal ebenfalls koppeln (Pixelwert, gleiche Schrift beidseits) —
    // that keeps the ends comparable. Clamping is done by setValue.
    connect(m_left->horizontalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int v) { onHScrolled(v, true); });
    connect(m_right->horizontalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int v) { onHScrolled(v, false); });

    connect(m_connector, &ConnectorWidget::copyBlockRequested,
            this, [this](int blockIndex, bool leftToRight) {
                flushPendingRediff();
                if (blockIndex < 0 || blockIndex >= m_blocks.size()
                    || m_blocks[blockIndex].type == BlockType::Equal)
                    return;
                m_currentBlock = blockIndex;
                const MergeEngine::LineEdit edit = MergeEngine::blockEdit(
                    m_blocks[blockIndex], documentLines(leftToRight),
                    documentLines(!leftToRight), leftToRight,
                    MergeMode::Replace);
                applyEdits({edit}, !leftToRight);
            });
    connect(m_overview, &OverviewBar::diffClicked,
            this, &DiffView::gotoBlock);

    // A click into a pane selects the section under the cursor. It becomes
    // visible as a frame and is the target of a section merge.
    connect(m_left, &QPlainTextEdit::cursorPositionChanged, this, [this]() {
        if (!m_applyingEdit)
            updateCurrentBlockFromCursor(true);
    });
    connect(m_right, &QPlainTextEdit::cursorPositionChanged, this, [this]() {
        if (!m_applyingEdit)
            updateCurrentBlockFromCursor(false);
    });
}

// Activates the section under the cursor line, but only when that line lies
// inside a changed block. A click into an unchanged area keeps the current
// bisherige Auswahl).
void DiffView::updateCurrentBlockFromCursor(bool leftSide)
{
    const int idx = blockIndexAtCursor(leftSide);
    if (idx < 0 || idx == m_currentBlock)
        return;
    m_currentBlock = idx;
    m_overview->setCurrentDiff(idx);
    updateSectionFrames();
    Q_EMIT diffStatusChanged();
}

// Frame around the current section in both panes, sized like the block.
void DiffView::updateSectionFrames()
{
    if (m_currentBlock >= 0 && m_currentBlock < m_blocks.size()
        && m_blocks[m_currentBlock].type != BlockType::Equal) {
        const DiffBlock &b = m_blocks[m_currentBlock];
        m_left->setSectionFrame(b.leftStart, b.leftCount);
        m_right->setSectionFrame(b.rightStart, b.rightCount);
    } else {
        m_left->setSectionFrame(-1, 0);
        m_right->setSectionFrame(-1, 0);
    }
}

void DiffView::setText(bool leftSide, const QString &text)
{
    m_applyingEdit = true;
    (leftSide ? m_left : m_right)->setPlainText(text);
    m_applyingEdit = false;
    recompute();
}

void DiffView::setLanguages(Language left, Language right)
{
    m_leftLang  = left;
    m_rightLang = right;
}

void DiffView::setOptions(const DiffOptions &options)
{
    m_options = options;
    recompute();
}

QStringList DiffView::documentLines(bool leftSide) const
{
    return (leftSide ? m_left : m_right)->toPlainText().split(QLatin1Char('\n'));
}

void DiffView::recompute()
{
    m_rediffTimer->stop();

    const QStringList leftLines  = documentLines(true);
    const QStringList rightLines = documentLines(false);

    const QStringList normLeft =
        LineNormalizer::normalize(leftLines, m_options, m_leftLang);
    const QStringList normRight =
        LineNormalizer::normalize(rightLines, m_options, m_rightLang);

    m_blocks = DiffEngine::compare(normLeft, normRight);
    DiffEngine::slideAmbiguousBlocks(m_blocks, leftLines, rightLines,
                                     normLeft, normRight);
    // Only while an option can empty a line. Otherwise an inserted blank
    // line would suddenly count as trivial.
    if (m_options.ignoreComments || m_options.ignoreWhitespace)
        DiffEngine::markTrivialBlocks(m_blocks, normLeft, normRight);

    m_diffBlockIndices.clear();
    for (int i = 0; i < m_blocks.size(); ++i) {
        if (m_blocks[i].type != BlockType::Equal && !m_blocks[i].trivial)
            m_diffBlockIndices << i;
    }
    if (m_currentBlock >= m_blocks.size()
        || (m_currentBlock >= 0 && m_blocks[m_currentBlock].type == BlockType::Equal))
        m_currentBlock = -1;

    applyHighlights();
    applyGapMarkers();
    applyGhostLines();
    updateSectionFrames();
    m_connector->setBlocks(m_blocks);
    m_overview->setBlocks(m_blocks);
    m_overview->setCurrentDiff(m_currentBlock);

    Q_EMIT diffStatusChanged();
}

// Gutter wedges: mark the lines after which the other side has additional
// lines. -1 means before the first line.
void DiffView::applyGapMarkers()
{
    QSet<int> leftGaps, rightGaps;
    for (const DiffBlock &b : m_blocks) {
        if (b.type == BlockType::Equal)
            continue;
        if (b.rightCount > b.leftCount)
            leftGaps.insert(b.leftStart + b.leftCount - 1);   // -1 when count is 0
        if (b.leftCount > b.rightCount)
            rightGaps.insert(b.rightStart + b.rightCount - 1);
    }
    m_left->setGapMarkers(leftGaps);
    m_right->setGapMarkers(rightGaps);
}

// ---------------------------------------------------------------------------
// Aligned view: placeholder rows, a rendering layer inside DiffTextEdit
// ---------------------------------------------------------------------------

void DiffView::setAlignedMode(bool aligned)
{
    if (m_aligned == aligned)
        return;
    m_aligned = aligned;
    applyGhostLines();
}

// Computes the placeholder table per side, mapping a real line to the
// placeholders after it, plus the prefix sums for the scroll mapping.
void DiffView::applyGhostLines()
{
    QHash<int, int> ghostLeft, ghostRight;
    if (m_aligned) {
        for (const DiffBlock &b : m_blocks) {
            if (b.type == BlockType::Equal)
                continue;
            if (b.rightCount > b.leftCount) {
                ghostLeft[b.leftStart + b.leftCount - 1] +=
                    b.rightCount - b.leftCount; // key -1 means before line 0
            } else if (b.leftCount > b.rightCount) {
                ghostRight[b.rightStart + b.rightCount - 1] +=
                    b.leftCount - b.rightCount;
            }
        }
    }
    m_left->setGhostLines(ghostLeft);
    m_right->setGhostLines(ghostRight);

    const auto buildPrefix = [](const QHash<int, int> &ghosts, int lineCount) {
        QList<int> prefix;
        prefix.reserve(lineCount + 1);
        int acc = ghosts.value(-1, 0);
        for (int i = 0; i <= lineCount; ++i) {
            prefix.append(acc);
            acc += ghosts.value(i, 0);
        }
        return prefix;
    };
    if (m_aligned) {
        m_ghostPrefixLeft =
            buildPrefix(ghostLeft, m_left->document()->blockCount());
        m_ghostPrefixRight =
            buildPrefix(ghostRight, m_right->document()->blockCount());
    } else {
        m_ghostPrefixLeft.clear();
        m_ghostPrefixRight.clear();
    }
}

// Largest real line whose visual row is at most visualRow, by binary search.
int DiffView::realLineAtVisualRow(int visualRow, bool leftSide) const
{
    const QList<int> &prefix = leftSide ? m_ghostPrefixLeft
                                        : m_ghostPrefixRight;
    if (prefix.isEmpty())
        return visualRow;
    int lo = 0;
    int hi = prefix.size() - 1;
    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        if (mid + prefix[mid] <= visualRow)
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo;
}

void DiffView::applyHighlights()
{
    const DiffColors colors = themeColors(palette());
    const QStringList leftLines  = documentLines(true);
    const QStringList rightLines = documentLines(false);

    QList<QTextEdit::ExtraSelection> leftSels, rightSels;

    // In parallel, explicit colour data for the custom rendering, which
    // does not draw extra selections.
    QHash<int, QColor> bgLeft, bgRight;
    QList<CharHighlight> chLeft, chRight;
    const auto fillBg = [](QHash<int, QColor> &map, int start, int count,
                           const QColor &color) {
        for (int i = 0; i < count; ++i)
            map.insert(start + i, color);
    };

    for (const DiffBlock &b : m_blocks) {
        switch (b.type) {
        case BlockType::Equal:
            break;
        case BlockType::OnlyLeft: {
            const QColor c = b.trivial ? colors.trivial
                           : b.movedPartner >= 0 ? colors.moved
                                                 : colors.removed;
            appendLineSelections(leftSels, m_left->document(),
                                 b.leftStart, b.leftCount, c);
            fillBg(bgLeft, b.leftStart, b.leftCount, c);
            break;
        }
        case BlockType::OnlyRight: {
            const QColor c = b.trivial ? colors.trivial
                           : b.movedPartner >= 0 ? colors.moved
                                                 : colors.added;
            appendLineSelections(rightSels, m_right->document(),
                                 b.rightStart, b.rightCount, c);
            fillBg(bgRight, b.rightStart, b.rightCount, c);
            break;
        }
        case BlockType::Changed: {
            const QColor c = b.trivial ? colors.trivial : colors.changed;
            appendLineSelections(leftSels, m_left->document(),
                                 b.leftStart, b.leftCount, c);
            appendLineSelections(rightSels, m_right->document(),
                                 b.rightStart, b.rightCount, c);
            fillBg(bgLeft, b.leftStart, b.leftCount, c);
            fillBg(bgRight, b.rightStart, b.rightCount, c);

            // Within-line highlight for the paired lines of the block.
            const int pairs = b.trivial ? 0 : qMin(b.leftCount, b.rightCount);
            for (int i = 0; i < pairs; ++i) {
                const QString &l = leftLines.value(b.leftStart + i);
                const QString &r = rightLines.value(b.rightStart + i);
                if (l.size() > 2000 || r.size() > 2000)
                    continue;
                const IntralineSpan span = IntralineDiff::compare(l, r);
                if (!span.valid)
                    continue;

                if (span.leftLen > 0) {
                    const QTextBlock tb =
                        m_left->document()->findBlockByNumber(b.leftStart + i);
                    QTextEdit::ExtraSelection sel;
                    sel.cursor = QTextCursor(tb);
                    sel.cursor.setPosition(tb.position() + span.leftStart);
                    sel.cursor.setPosition(tb.position() + span.leftStart + span.leftLen,
                                           QTextCursor::KeepAnchor);
                    sel.format.setBackground(colors.intraline);
                    leftSels.append(sel);
                    chLeft.append({b.leftStart + i, span.leftStart,
                                   span.leftLen, colors.intraline});
                }
                if (span.rightLen > 0) {
                    const QTextBlock tb =
                        m_right->document()->findBlockByNumber(b.rightStart + i);
                    QTextEdit::ExtraSelection sel;
                    sel.cursor = QTextCursor(tb);
                    sel.cursor.setPosition(tb.position() + span.rightStart);
                    sel.cursor.setPosition(tb.position() + span.rightStart + span.rightLen,
                                           QTextCursor::KeepAnchor);
                    sel.format.setBackground(colors.intraline);
                    rightSels.append(sel);
                    chRight.append({b.rightStart + i, span.rightStart,
                                    span.rightLen, colors.intraline});
                }
            }
            break;
        }
        }
    }

    m_left->setDiffSelections(leftSels);
    m_right->setDiffSelections(rightSels);
    m_left->setDiffLineColors(bgLeft);
    m_right->setDiffLineColors(bgRight);
    m_left->setCharHighlights(chLeft);
    m_right->setCharHighlights(chRight);
}

// ---------------------------------------------------------------------------
// Synchronized scrolling
// ---------------------------------------------------------------------------

int DiffView::mapLine(int line, bool leftToRight) const
{
    // Placeholder mode: exact mapping over visual rows, because both sides
    // then have the same total number of visual rows.
    if (!m_ghostPrefixLeft.isEmpty() && !m_ghostPrefixRight.isEmpty()) {
        const QList<int> &from = leftToRight ? m_ghostPrefixLeft
                                             : m_ghostPrefixRight;
        const int clamped = qBound(0, line, int(from.size()) - 1);
        const int visualRow = clamped + from[clamped];
        return realLineAtVisualRow(visualRow, !leftToRight);
    }

    for (const DiffBlock &b : m_blocks) {
        const int s  = leftToRight ? b.leftStart  : b.rightStart;
        const int c  = leftToRight ? b.leftCount  : b.rightCount;
        const int os = leftToRight ? b.rightStart : b.leftStart;
        const int oc = leftToRight ? b.rightCount : b.leftCount;
        if (line < s + c) {
            if (c <= 0)
                return os;
            const double f = double(line - s) / c;
            return os + int(f * oc);
        }
    }
    if (m_blocks.isEmpty())
        return line;
    const DiffBlock &last = m_blocks.last();
    return leftToRight ? last.rightStart + last.rightCount
                       : last.leftStart + last.leftCount;
}

void DiffView::onScrolled(bool fromLeft)
{
    m_connector->update();
    if (m_syncing)
        return;
    m_syncing = true;
    // Work with line numbers instead of raw scroll bar values: with word
    // wrap the scroll bar counts visual rows, not paragraphs.
    if (fromLeft)
        m_right->scrollToLine(mapLine(m_left->firstVisibleLine(), true));
    else
        m_left->scrollToLine(mapLine(m_right->firstVisibleLine(), false));
    m_syncing = false;
}

void DiffView::onHScrolled(int value, bool fromLeft)
{
    if (m_syncing)
        return;
    m_syncing = true;
    (fromLeft ? m_right : m_left)->horizontalScrollBar()->setValue(value);
    m_syncing = false;
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

int DiffView::diffCount() const
{
    return m_diffBlockIndices.size();
}

int DiffView::currentDiffNumber() const
{
    const int pos = m_diffBlockIndices.indexOf(m_currentBlock);
    return pos < 0 ? 0 : pos + 1;
}

void DiffView::gotoBlock(int blockIndex)
{
    if (blockIndex < 0 || blockIndex >= m_blocks.size())
        return;
    m_currentBlock = blockIndex;
    const DiffBlock &b = m_blocks[blockIndex];

    m_syncing = true;
    // Put the cursor at the start of the block on both sides. The caret is
    // then visible at the difference and navigation continues from there.
    // The guard keeps updateCurrentBlockFromCursor from jumping to a
    // neighbouring block, since on an insertion the other cursor lies outside.
    m_applyingEdit = true;
    const auto placeCursor = [](DiffTextEdit *edit, int line) {
        const QTextBlock tb = edit->document()->findBlockByNumber(line);
        if (tb.isValid())
            edit->setTextCursor(QTextCursor(tb));
    };
    placeCursor(m_left, b.leftStart);
    placeCursor(m_right, b.rightStart);
    m_applyingEdit = false;

    if (m_left->ghostModeActive() || m_right->ghostModeActive()) {
        // Placeholder mode: the scroll value counts real lines and
        // centerCursor() does not know the placeholders, so align at the top.
        m_left->scrollToLine(qMax(0, b.leftStart - 3));
        m_right->scrollToLine(qMax(0, b.rightStart - 3));
    } else {
        m_left->centerCursor();
        m_right->centerCursor();
    }
    m_syncing = false;
    m_connector->update();

    m_overview->setCurrentDiff(blockIndex);
    updateSectionFrames();
    Q_EMIT diffStatusChanged();
}

// Anchor for next and previous: the cursor line of the focused pane, the
// left one otherwise. Moving the cursor moves where navigation continues.
int DiffView::navigationCursorLine(bool *leftSide) const
{
    const bool left = !m_right->hasFocus();
    if (leftSide)
        *leftSide = left;
    return (left ? m_left : m_right)->textCursor().blockNumber();
}

void DiffView::gotoNextDiff()
{
    if (m_diffBlockIndices.isEmpty())
        return;
    bool leftSide = true;
    const int line = navigationCursorLine(&leftSide);
    for (const int idx : std::as_const(m_diffBlockIndices)) {
        const int s = leftSide ? m_blocks[idx].leftStart
                               : m_blocks[idx].rightStart;
        if (s > line) {
            gotoBlock(idx);
            return;
        }
    }
    gotoBlock(m_diffBlockIndices.last());
}

void DiffView::gotoPrevDiff()
{
    if (m_diffBlockIndices.isEmpty())
        return;
    bool leftSide = true;
    const int line = navigationCursorLine(&leftSide);
    for (auto it = m_diffBlockIndices.crbegin();
         it != m_diffBlockIndices.crend(); ++it) {
        const int s = leftSide ? m_blocks[*it].leftStart
                               : m_blocks[*it].rightStart;
        const int c = leftSide ? m_blocks[*it].leftCount
                               : m_blocks[*it].rightCount;
        // Entirely above the cursor. A pure insertion point (c==0) counts
        // only when strictly above, otherwise previous would stick to it.
        if (c > 0 ? (s + c <= line) : (s < line)) {
            gotoBlock(*it);
            return;
        }
    }
    gotoBlock(m_diffBlockIndices.first());
}

// ---------------------------------------------------------------------------
// Merge operations (Berechnung: MergeEngine; Anwendung: applyEdits)
// ---------------------------------------------------------------------------

// A merge must never work on an outdated block list. When a recomputation
// is still pending after an edit, it runs at once. This was the cause of the
// fault where a merge only took effect on the second attempt.
void DiffView::flushPendingRediff()
{
    if (m_rediffTimer->isActive())
        recompute(); // stoppt den Timer selbst
}

void DiffView::applyEdits(QList<MergeEngine::LineEdit> edits, bool targetLeft)
{
    if (edits.isEmpty())
        return;

    // Apply from the bottom up so that the line numbers above stay valid.
    std::sort(edits.begin(), edits.end(),
              [](const MergeEngine::LineEdit &a, const MergeEngine::LineEdit &b) {
                  return a.dstStart > b.dstStart;
              });

    QTextDocument *doc = (targetLeft ? m_left : m_right)->document();
    m_applyingEdit = true;
    bool first = true;
    for (const MergeEngine::LineEdit &e : std::as_const(edits)) {
        replaceLines(doc, e.dstStart, e.dstCount, e.newLines, !first);
        first = false;
    }
    m_applyingEdit = false;
    m_lastChangedLeft = targetLeft;

    recompute();
}

void DiffView::copyAll(bool leftToRight)
{
    flushPendingRediff();
    const QStringList srcLines = documentLines(leftToRight);
    const QStringList dstLines = documentLines(!leftToRight);

    QList<MergeEngine::LineEdit> edits;
    for (const int idx : std::as_const(m_diffBlockIndices)) {
        edits.append(MergeEngine::blockEdit(m_blocks[idx], srcLines, dstLines,
                                            leftToRight, MergeMode::Replace));
    }
    applyEdits(edits, !leftToRight);
}

void DiffView::copySelectedLines(bool leftToRight)
{
    flushPendingRediff();
    DiffTextEdit *source = leftToRight ? m_left : m_right;

    if (!source->textCursor().hasSelection()) {
        Q_EMIT statusMessage(
            leftToRight
                ? tr("Select lines on the left side first.")
                : tr("Select lines on the right side first."),
            3000);
        return;
    }

    int selStart = 0, selCount = 0;
    source->selectedLineRange(&selStart, &selCount);

    const QList<MergeEngine::LineEdit> edits = MergeEngine::selectionEdits(
        m_blocks, documentLines(leftToRight), documentLines(!leftToRight),
        leftToRight, selStart, selCount);

    if (edits.isEmpty()) {
        Q_EMIT statusMessage(
            tr("The selection contains no changes to merge."), 3000);
        return;
    }
    applyEdits(edits, !leftToRight);
}

int DiffView::blockIndexAtCursor(bool leftSide) const
{
    const int line = (leftSide ? m_left : m_right)->textCursor().blockNumber();
    for (int i = 0; i < m_blocks.size(); ++i) {
        const DiffBlock &b = m_blocks[i];
        if (b.type == BlockType::Equal)
            continue;
        const int s = leftSide ? b.leftStart : b.rightStart;
        const int c = leftSide ? b.leftCount : b.rightCount;
        if (line >= s && line < s + c)
            return i;
    }
    return -1;
}

void DiffView::copyCurrentBlock(bool leftToRight, MergeMode mode)
{
    flushPendingRediff();

    DiffTextEdit *source = leftToRight ? m_left : m_right;

    // Use Both with a selection: insert exactly the selected lines, those
    // equal on both sides included, before or after the aligned position.
    if (mode != MergeMode::Replace && source->textCursor().hasSelection()) {
        int selStart = 0, selCount = 0;
        source->selectedLineRange(&selStart, &selCount);
        const MergeEngine::LineEdit edit = MergeEngine::useBothSelectionEdit(
            m_blocks, documentLines(leftToRight),
            documentLines(!leftToRight).size(),
            leftToRight, selStart, selCount,
            mode == MergeMode::InsertBefore);
        applyEdits({edit}, !leftToRight);
        return;
    }

    // Aktive Section (per Klick/Navigation); Fallback: Cursor-Block —
    // the focused pane first, then the other one.
    int idx = m_currentBlock;
    if (idx < 0 || idx >= m_blocks.size()
        || m_blocks[idx].type == BlockType::Equal) {
        const bool leftFocused = m_left->hasFocus();
        idx = blockIndexAtCursor(leftFocused);
        if (idx < 0)
            idx = blockIndexAtCursor(!leftFocused);
    }
    if (idx < 0 || m_blocks[idx].type == BlockType::Equal) {
        Q_EMIT statusMessage(
            tr("Click into a change section first."), 3000);
        return;
    }

    const MergeEngine::LineEdit edit = MergeEngine::blockEdit(
        m_blocks[idx], documentLines(leftToRight), documentLines(!leftToRight),
        leftToRight, mode);
    applyEdits({edit}, !leftToRight);
}
