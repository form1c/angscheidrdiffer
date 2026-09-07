#pragma once

#include <QList>
#include <QStringList>

enum class BlockType {
    Equal,      // identical lines on both sides
    Changed,    // present on both sides, but different
    OnlyLeft,   // present on the left only (deleted on the right)
    OnlyRight,  // present on the right only (added on the right)
};

// An aligned section of both files. The line ranges refer to the ORIGINAL
// line lists, not to the normalised comparison lines.
struct DiffBlock {
    BlockType type = BlockType::Equal;
    int leftStart  = 0;
    int leftCount  = 0;
    int rightStart = 0;
    int rightCount = 0;
    // Detection of moved blocks: index of the partner block when this
    // left-only block has the same content as a right-only one, or vice versa.
    int movedPartner = -1;
    // Trivial difference: every line involved normalises to empty, for
    // example a comment-only block while comments are ignored. Drawn
    // discreetly and skipped by navigation and counting. Merging still works.
    bool trivial = false;
};

namespace DiffEngine {

// Compares two line lists with a Myers difference and returns the block list.
// The lists should already be normalised when ignore options are active.
// The blocks apply to the original lists, which have the same length.
QList<DiffBlock> compare(const QStringList &left, const QStringList &right);

// With an ignore option active, the position of a pure insertion or deletion
// inside a run of lines that normalise to the same value is ambiguous, and
// the algorithm picks one of several valid places. This pass moves such a
// block to the position where the RAW lines of the surrounding pairs match
// best, which is the natural place for it.
// norm*: the comparison lines passed to compare(). raw*: the originals.
void slideAmbiguousBlocks(QList<DiffBlock> &blocks,
                          const QStringList &rawLeft,
                          const QStringList &rawRight,
                          const QStringList &normLeft,
                          const QStringList &normRight);

// Sets DiffBlock::trivial for non-equal blocks whose lines all normalise to
// empty on both sides. This is only meaningful while an option that can
// empty a line is active. The caller decides, this function does not check.
void markTrivialBlocks(QList<DiffBlock> &blocks,
                       const QStringList &normLeft,
                       const QStringList &normRight);

} // namespace DiffEngine
