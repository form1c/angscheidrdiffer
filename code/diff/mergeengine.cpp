#include "mergeengine.h"

#include <QVector>

namespace {

// Line ranges of a block for the source and the target side.
struct BlockSides {
    int srcStart, srcCount, dstStart, dstCount;
};

BlockSides sidesOf(const DiffBlock &b, bool srcIsLeft)
{
    if (srcIsLeft)
        return {b.leftStart, b.leftCount, b.rightStart, b.rightCount};
    return {b.rightStart, b.rightCount, b.leftStart, b.leftCount};
}

} // namespace

double MergeEngine::lineSimilarity(const QString &a, const QString &b)
{
    const QString ta = a.trimmed().left(200);
    const QString tb = b.trimmed().left(200);
    if (ta.isEmpty() && tb.isEmpty())
        return 1.0;
    if (ta.isEmpty() || tb.isEmpty())
        return 0.0;
    if (ta == tb)
        return 1.0;

    // Length of the longest common subsequence over two rolling rows.
    const int n = ta.size();
    const int m = tb.size();
    QVector<int> prev(m + 1, 0), cur(m + 1, 0);
    for (int i = 1; i <= n; ++i) {
        for (int j = 1; j <= m; ++j) {
            if (ta[i - 1] == tb[j - 1])
                cur[j] = prev[j - 1] + 1;
            else
                cur[j] = qMax(prev[j], cur[j - 1]);
        }
        prev = cur;
    }
    return 2.0 * prev[m] / double(n + m);
}

QList<int> MergeEngine::alignBlockRows(const QStringList &srcRows,
                                       const QStringList &dstRows)
{
    const int n = srcRows.size();
    const int m = dstRows.size();
    QList<int> result(n, -1);
    if (n == 0 || m == 0)
        return result;

    constexpr double kThreshold = 0.5;

    // Needleman-Wunsch style dynamic programming: maximise the sum of the
    // similarities, pair only above the threshold, gaps are free.
    QVector<QVector<double>> score(n + 1, QVector<double>(m + 1, 0.0));
    QVector<QVector<char>>   move(n + 1, QVector<char>(m + 1, 0)); // 1=diag 2=up 3=left

    for (int i = 1; i <= n; ++i) {
        for (int j = 1; j <= m; ++j) {
            const double sim = lineSimilarity(srcRows[i - 1], dstRows[j - 1]);
            double best = score[i - 1][j];
            char bestMove = 2;
            if (score[i][j - 1] > best) {
                best = score[i][j - 1];
                bestMove = 3;
            }
            if (sim >= kThreshold && score[i - 1][j - 1] + sim > best) {
                best = score[i - 1][j - 1] + sim;
                bestMove = 1;
            }
            score[i][j] = best;
            move[i][j]  = bestMove;
        }
    }

    int i = n, j = m;
    while (i > 0 && j > 0) {
        switch (move[i][j]) {
        case 1:
            result[i - 1] = j - 1;
            --i;
            --j;
            break;
        case 2:
            --i;
            break;
        default:
            --j;
            break;
        }
    }
    return result;
}

MergeEngine::LineEdit MergeEngine::blockEdit(const DiffBlock &block,
                                             const QStringList &srcLines,
                                             const QStringList &dstLines,
                                             bool srcIsLeft, MergeMode mode)
{
    const BlockSides s = sidesOf(block, srcIsLeft);

    QStringList srcBlock, dstBlock;
    for (int i = 0; i < s.srcCount; ++i)
        srcBlock << srcLines.value(s.srcStart + i);
    for (int i = 0; i < s.dstCount; ++i)
        dstBlock << dstLines.value(s.dstStart + i);

    LineEdit edit;
    edit.dstStart = s.dstStart;
    edit.dstCount = s.dstCount;
    switch (mode) {
    case MergeMode::Replace:      edit.newLines = srcBlock;            break;
    case MergeMode::InsertBefore: edit.newLines = srcBlock + dstBlock; break;
    case MergeMode::InsertAfter:  edit.newLines = dstBlock + srcBlock; break;
    }
    return edit;
}

QList<MergeEngine::LineEdit> MergeEngine::selectionEdits(
    const QList<DiffBlock> &blocks,
    const QStringList &srcLines, const QStringList &dstLines,
    bool srcIsLeft, int selStart, int selCount)
{
    QList<LineEdit> edits;
    const int selEnd = selStart + selCount;

    for (const DiffBlock &b : blocks) {
        if (b.type == BlockType::Equal)
            continue;
        const BlockSides s = sidesOf(b, srcIsLeft);
        if (s.srcCount == 0)
            continue; // pure insertion of the other side, nothing to take
        if (s.srcStart >= selEnd || s.srcStart + s.srcCount <= selStart)
            continue; // the selection does not touch this block

        QStringList srcRows, dstRows;
        for (int i = 0; i < s.srcCount; ++i)
            srcRows << srcLines.value(s.srcStart + i);
        for (int j = 0; j < s.dstCount; ++j)
            dstRows << dstLines.value(s.dstStart + j);

        const QList<int> align = alignBlockRows(srcRows, dstRows);

        QStringList replaced = dstRows;
        QVector<QStringList> insertBefore(s.dstCount + 1); // vor Ziel-Slot k
        int lastAlignedDst = -1;

        for (int i = 0; i < srcRows.size(); ++i) {
            const int srcLine = s.srcStart + i;
            const bool selected = srcLine >= selStart && srcLine < selEnd;
            const int partner = align[i];
            if (partner >= 0) {
                if (selected)
                    replaced[partner] = srcRows[i];
                lastAlignedDst = partner;
            } else if (selected) {
                insertBefore[lastAlignedDst + 1].append(srcRows[i]);
            }
        }

        QStringList newLines;
        for (int k = 0; k < s.dstCount; ++k) {
            newLines += insertBefore[k];
            newLines << replaced[k];
        }
        newLines += insertBefore[s.dstCount];

        if (newLines == dstRows)
            continue; // the selection changes nothing here

        edits.append({s.dstStart, s.dstCount, newLines});
    }
    return edits;
}

MergeEngine::LineEdit MergeEngine::useBothSelectionEdit(
    const QList<DiffBlock> &blocks, const QStringList &srcLines,
    int dstLineCount, bool srcIsLeft, int selStart, int selCount,
    bool insertBefore)
{
    // Determine the aligned target position for a source line.
    // atEnd=true gives the position AFTER the aligned range of that line.
    const auto dstPosForSrcRow = [&](int srcRow, bool atEnd) {
        for (const DiffBlock &b : blocks) {
            const BlockSides s = sidesOf(b, srcIsLeft);
            if (srcRow >= s.srcStart + s.srcCount)
                continue;
            if (b.type == BlockType::Equal)
                return s.dstStart + (srcRow - s.srcStart) + (atEnd ? 1 : 0);
            return atEnd ? s.dstStart + s.dstCount : s.dstStart;
        }
        return dstLineCount; // after the last line
    };

    LineEdit edit;
    edit.dstStart = insertBefore
        ? dstPosForSrcRow(selStart, false)
        : dstPosForSrcRow(selStart + selCount - 1, true);
    edit.dstCount = 0;
    for (int i = 0; i < selCount; ++i)
        edit.newLines << srcLines.value(selStart + i);
    return edit;
}
