#pragma once

#include <QHash>
#include <QList>
#include <QStringList>

// Three-way merge following the diff3 principle: two comparisons against
// the common base produce a list of chunks. A change made by one side only
// is taken automatically. Only a real conflict needs a decision.

enum class Merge3ChunkType {
    Stable,      // nobody changed anything
    OnlyMine,    // only mine changed, taken automatically
    OnlyTheirs,  // only theirs changed, taken automatically
    BothSame,    // both made the same change, taken automatically
    Conflict,    // both changed it differently, a decision is required
};

// An aligned section of all three files. The ranges refer to the ORIGINAL
// line lists, counted as in DiffBlock: start plus count.
struct Merge3Chunk {
    Merge3ChunkType type = Merge3ChunkType::Stable;
    int baseStart   = 0;
    int baseCount   = 0;
    int mineStart   = 0;
    int mineCount   = 0;
    int theirsStart = 0;
    int theirsCount = 0;
};

// Resolution of a conflicting chunk.
enum class Merge3Choice {
    Unresolved,
    Base,
    Mine,
    Theirs,
    MineThenTheirs,
    TheirsThenMine,
};

struct Merge3Output {
    QStringList lines;          // the merged result
    QList<int>  chunkStart;     // first line of each chunk in the result
    int unresolvedConflicts = 0;
};

namespace Merge3Engine {

// Builds the chunk list from base, mine and theirs. The lists should
// already be normalised when ignore options are active. The chunks apply
// to the original lists, which have the same length. Changes of both sides
// that overlap or merely touch are combined into one cluster.
QList<Merge3Chunk> compare(const QStringList &base,
                           const QStringList &mine,
                           const QStringList &theirs);

// Assembles the result from the RAW lines. choices maps a chunk index to
// its resolution, which matters for conflicting chunks only. Unresolved
// conflicts are written with diff3 style markers and counted.
Merge3Output buildResult(const QList<Merge3Chunk> &chunks,
                         const QStringList &base,
                         const QStringList &mine,
                         const QStringList &theirs,
                         const QHash<int, Merge3Choice> &choices = {});

} // namespace Merge3Engine
