#include "merge3engine.h"
#include "diffengine.h"

#include <climits>

namespace {

// A change of one side against the base, meaning a non-equal block.
struct Change {
    int baseStart  = 0;
    int baseCount  = 0;
    int otherStart = 0;
    int otherCount = 0;
};

QList<Change> changesAgainstBase(const QStringList &base,
                                 const QStringList &other)
{
    QList<Change> out;
    const QList<DiffBlock> blocks = DiffEngine::compare(base, other);
    for (const DiffBlock &b : blocks) {
        if (b.type != BlockType::Equal)
            out.append({b.leftStart, b.leftCount, b.rightStart, b.rightCount});
    }
    return out;
}

} // namespace

QList<Merge3Chunk> Merge3Engine::compare(const QStringList &base,
                                         const QStringList &mine,
                                         const QStringList &theirs)
{
    const QList<Change> cm = changesAgainstBase(base, mine);
    const QList<Change> ct = changesAgainstBase(base, theirs);

    QList<Merge3Chunk> chunks;
    const auto addStable = [&](int from, int to, int deltaMine,
                               int deltaTheirs) {
        if (to <= from)
            return;
        Merge3Chunk s;
        s.type = Merge3ChunkType::Stable;
        s.baseStart = from;            s.baseCount = to - from;
        s.mineStart = from + deltaMine;   s.mineCount = to - from;
        s.theirsStart = from + deltaTheirs; s.theirsCount = to - from;
        chunks.append(s);
    };

    int i = 0, j = 0;
    int basePos = 0;
    int deltaMine = 0;    // minePos - basePos at stable positions
    int deltaTheirs = 0;
    const int nBase = base.size();

    while (true) {
        const int nextM = i < cm.size() ? cm[i].baseStart : INT_MAX;
        const int nextT = j < ct.size() ? ct[j].baseStart : INT_MAX;
        const int lo = qMin(nextM, nextT);
        if (lo == INT_MAX) {
            addStable(basePos, nBase, deltaMine, deltaTheirs);
            break;
        }
        addStable(basePos, lo, deltaMine, deltaTheirs);

        // Cluster: every change of both sides that touches [lo, hi),
        // including insertions that merely adjoin. This is the conservative
        // choice: one conflict more rather than a silently wrong order.
        int hi = lo;
        int dMine = 0, dTheirs = 0;
        bool grew = true;
        while (grew) {
            grew = false;
            while (i < cm.size() && cm[i].baseStart <= hi) {
                hi = qMax(hi, cm[i].baseStart + cm[i].baseCount);
                dMine += cm[i].otherCount - cm[i].baseCount;
                ++i;
                grew = true;
            }
            while (j < ct.size() && ct[j].baseStart <= hi) {
                hi = qMax(hi, ct[j].baseStart + ct[j].baseCount);
                dTheirs += ct[j].otherCount - ct[j].baseCount;
                ++j;
                grew = true;
            }
        }

        Merge3Chunk c;
        c.baseStart   = lo;                 c.baseCount   = hi - lo;
        c.mineStart   = lo + deltaMine;     c.mineCount   = hi - lo + dMine;
        c.theirsStart = lo + deltaTheirs;   c.theirsCount = hi - lo + dTheirs;

        const QStringList baseSlice   = base.mid(c.baseStart, c.baseCount);
        const QStringList mineSlice   = mine.mid(c.mineStart, c.mineCount);
        const QStringList theirsSlice = theirs.mid(c.theirsStart,
                                                   c.theirsCount);
        const bool mineChanged   = mineSlice != baseSlice;
        const bool theirsChanged = theirsSlice != baseSlice;
        if (mineChanged && theirsChanged) {
            c.type = mineSlice == theirsSlice ? Merge3ChunkType::BothSame
                                              : Merge3ChunkType::Conflict;
        } else if (mineChanged) {
            c.type = Merge3ChunkType::OnlyMine;
        } else if (theirsChanged) {
            c.type = Merge3ChunkType::OnlyTheirs;
        } else {
            c.type = Merge3ChunkType::Stable; // the cluster cancelled out
        }
        chunks.append(c);

        basePos = hi;
        deltaMine += dMine;
        deltaTheirs += dTheirs;
    }

    return chunks;
}

Merge3Output Merge3Engine::buildResult(const QList<Merge3Chunk> &chunks,
                                       const QStringList &base,
                                       const QStringList &mine,
                                       const QStringList &theirs,
                                       const QHash<int, Merge3Choice> &choices)
{
    Merge3Output out;
    for (int idx = 0; idx < chunks.size(); ++idx) {
        const Merge3Chunk &c = chunks[idx];
        out.chunkStart.append(out.lines.size());

        const QStringList baseSlice   = base.mid(c.baseStart, c.baseCount);
        const QStringList mineSlice   = mine.mid(c.mineStart, c.mineCount);
        const QStringList theirsSlice = theirs.mid(c.theirsStart,
                                                   c.theirsCount);
        switch (c.type) {
        case Merge3ChunkType::Stable:
        case Merge3ChunkType::OnlyMine:
        case Merge3ChunkType::BothSame:
            // Stable and BothSame take mine, the version of the working
            // copy, whose raw text may differ when options are active.
            out.lines += mineSlice;
            break;
        case Merge3ChunkType::OnlyTheirs:
            out.lines += theirsSlice;
            break;
        case Merge3ChunkType::Conflict:
            switch (choices.value(idx, Merge3Choice::Unresolved)) {
            case Merge3Choice::Base:   out.lines += baseSlice;   break;
            case Merge3Choice::Mine:   out.lines += mineSlice;   break;
            case Merge3Choice::Theirs: out.lines += theirsSlice; break;
            case Merge3Choice::MineThenTheirs:
                out.lines += mineSlice;
                out.lines += theirsSlice;
                break;
            case Merge3Choice::TheirsThenMine:
                out.lines += theirsSlice;
                out.lines += mineSlice;
                break;
            case Merge3Choice::Unresolved:
                ++out.unresolvedConflicts;
                out.lines += QStringLiteral("<<<<<<< MINE");
                out.lines += mineSlice;
                out.lines += QStringLiteral("||||||| BASE");
                out.lines += baseSlice;
                out.lines += QStringLiteral("=======");
                out.lines += theirsSlice;
                out.lines += QStringLiteral(">>>>>>> THEIRS");
                break;
            }
            break;
        }
    }
    return out;
}
