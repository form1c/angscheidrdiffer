#pragma once

#include "diffengine.h"

#include <QList>
#include <QStringList>

// Behaviour of a merge operation.
enum class MergeMode {
    Replace,      // replace the target range with the source lines
    InsertBefore, // insert the source lines BEFORE the target, keeping both
    InsertAfter,  // insert the source lines AFTER the target, keeping both
};

// Merge logic without interface code. The view only applies the returned
// line edits, from the bottom up and as a single undo step.
namespace MergeEngine {

// One replacement to apply in the target document.
struct LineEdit {
    int dstStart = 0;
    int dstCount = 0;       // 0 = pure insertion before dstStart
    QStringList newLines;
};

// Character similarity of two lines, 0 to 1, from the longest common
double lineSimilarity(const QString &a, const QString &b);

// Monotone alignment of the lines of a changed block by similarity:
// result[srcRow] = dstRow, or -1 when there is no partner. Threshold 0.5.
QList<int> alignBlockRows(const QStringList &srcRows, const QStringList &dstRows);

// Section, All and Use Both without a selection: the whole block.
LineEdit blockEdit(const DiffBlock &block,
                   const QStringList &srcLines, const QStringList &dstLines,
                   bool srcIsLeft, MergeMode mode);

// Selection merge: transfer ONLY the selected source lines
// [selStart, selStart+selCount). Inside a changed block, a selected line
// replaces its aligned partner. A selected line without a partner is
// inserted at the aligned position. Unselected source lines leave their
// partner untouched.
QList<LineEdit> selectionEdits(const QList<DiffBlock> &blocks,
                               const QStringList &srcLines,
                               const QStringList &dstLines,
                               bool srcIsLeft, int selStart, int selCount);

// Use Both with a selection: insert the selected source lines verbatim,
// including lines that are equal on both sides, BEFORE (insertBefore=true)
// or AFTER the aligned target position.
LineEdit useBothSelectionEdit(const QList<DiffBlock> &blocks,
                              const QStringList &srcLines,
                              int dstLineCount,
                              bool srcIsLeft, int selStart, int selCount,
                              bool insertBefore);

} // namespace MergeEngine
