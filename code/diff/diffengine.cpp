#include "diffengine.h"

#include <QHash>

#include <algorithm>

namespace {

// Myers O(ND) edit script over interned line IDs.
// Returns a sequence of ops: 'E' = equal, 'D' = delete from a, 'I' = insert from b.
QList<char> myersOps(const QList<int> &a, const QList<int> &b)
{
    const int n = a.size();
    const int m = b.size();
    if (n == 0 && m == 0) return {};
    if (n == 0) return QList<char>(m, 'I');
    if (m == 0) return QList<char>(n, 'D');

    const int maxD   = n + m;
    const int offset = maxD;

    // Memory guard: trace grows with D * (n+m). For pathological inputs
    // (huge and completely different) degrade to one big change instead of
    // exhausting memory.
    QList<int> v(2 * maxD + 2, 0);
    QList<QList<int>> trace;
    int dFound = -1;

    const qint64 memGuard = 50'000'000; // ~200 MB of ints worst case
    for (int d = 0; d <= maxD && dFound < 0; ++d) {
        if (qint64(d) * v.size() > memGuard) {
            QList<char> ops(n, 'D');
            ops += QList<char>(m, 'I');
            return ops;
        }
        trace.append(v);
        for (int k = -d; k <= d; k += 2) {
            int x;
            if (k == -d || (k != d && v[offset + k - 1] < v[offset + k + 1]))
                x = v[offset + k + 1];
            else
                x = v[offset + k - 1] + 1;
            int y = x - k;
            while (x < n && y < m && a[x] == b[y]) { ++x; ++y; }
            v[offset + k] = x;
            if (x >= n && y >= m) { dFound = d; break; }
        }
    }

    // Backtrack from (n, m) using the recorded V states.
    QList<char> rev;
    int x = n, y = m;
    for (int d = dFound; d > 0; --d) {
        const QList<int> &pv = trace[d];
        const int k = x - y;
        int prevK;
        if (k == -d || (k != d && pv[offset + k - 1] < pv[offset + k + 1]))
            prevK = k + 1;
        else
            prevK = k - 1;
        const int prevX = pv[offset + prevK];
        const int prevY = prevX - prevK;
        while (x > prevX && y > prevY) { rev.append('E'); --x; --y; }
        if (x == prevX) { rev.append('I'); --y; }
        else            { rev.append('D'); --x; }
    }
    while (x > 0 && y > 0) { rev.append('E'); --x; --y; }
    while (x > 0) { rev.append('D'); --x; }
    while (y > 0) { rev.append('I'); --y; }

    std::reverse(rev.begin(), rev.end());
    return rev;
}

int internLine(const QString &line, QHash<QString, int> &pool)
{
    auto it = pool.constFind(line);
    if (it != pool.constEnd())
        return it.value();
    const int id = pool.size();
    pool.insert(line, id);
    return id;
}

} // namespace

QList<DiffBlock> DiffEngine::compare(const QStringList &left, const QStringList &right)
{
    const int nAll = left.size();
    const int mAll = right.size();

    // Trim common prefix and suffix — shrinks the Myers problem drastically
    // for the typical "few changes in a big file" case.
    int pre = 0;
    while (pre < nAll && pre < mAll && left[pre] == right[pre])
        ++pre;
    int post = 0;
    while (post < nAll - pre && post < mAll - pre
           && left[nAll - 1 - post] == right[mAll - 1 - post])
        ++post;

    const int n = nAll - pre - post;
    const int m = mAll - pre - post;

    // Intern middle lines to ints for O(1) comparisons inside Myers.
    QHash<QString, int> pool;
    QList<int> a;
    a.reserve(n);
    for (int i = 0; i < n; ++i)
        a << internLine(left[pre + i], pool);
    QList<int> b;
    b.reserve(m);
    for (int i = 0; i < m; ++i)
        b << internLine(right[pre + i], pool);

    const QList<char> ops = myersOps(a, b);

    // Assemble blocks: runs of 'E' become Equal; maximal non-'E' runs become
    // Changed / OnlyLeft / OnlyRight depending on their D/I composition.
    QList<DiffBlock> blocks;
    int li = pre, ri = pre;

    if (pre > 0)
        blocks.append({BlockType::Equal, 0, pre, 0, pre});

    int idx = 0;
    const int total = ops.size();
    while (idx < total) {
        if (ops[idx] == 'E') {
            int c = 0;
            while (idx < total && ops[idx] == 'E') { ++c; ++idx; }
            if (!blocks.isEmpty() && blocks.last().type == BlockType::Equal) {
                blocks.last().leftCount  += c;
                blocks.last().rightCount += c;
            } else {
                blocks.append({BlockType::Equal, li, c, ri, c});
            }
            li += c;
            ri += c;
        } else {
            int dels = 0, inss = 0;
            while (idx < total && ops[idx] != 'E') {
                if (ops[idx] == 'D') ++dels;
                else                 ++inss;
                ++idx;
            }
            const BlockType t = (dels && inss) ? BlockType::Changed
                              : (dels ? BlockType::OnlyLeft : BlockType::OnlyRight);
            blocks.append({t, li, dels, ri, inss});
            li += dels;
            ri += inss;
        }
    }

    if (post > 0) {
        if (!blocks.isEmpty() && blocks.last().type == BlockType::Equal) {
            blocks.last().leftCount  += post;
            blocks.last().rightCount += post;
        } else {
            blocks.append({BlockType::Equal, li, post, ri, post});
        }
    }

    // Moved-Block-Erkennung: inhaltsgleiche OnlyLeft/OnlyRight-Paare markieren.
    // Threshold against noise: at least 5 characters after trimming,
    // otherwise every moved "}" would be reported as a move.
    QHash<QString, int> leftContent;
    for (int i = 0; i < blocks.size(); ++i) {
        const DiffBlock &blk = blocks[i];
        if (blk.type != BlockType::OnlyLeft)
            continue;
        QStringList joined;
        for (int j = 0; j < blk.leftCount; ++j)
            joined << left.value(blk.leftStart + j);
        const QString content = joined.join(QLatin1Char('\n'));
        if (content.trimmed().size() < 5)
            continue;
        if (!leftContent.contains(content))
            leftContent.insert(content, i);
    }
    if (!leftContent.isEmpty()) {
        for (int i = 0; i < blocks.size(); ++i) {
            DiffBlock &blk = blocks[i];
            if (blk.type != BlockType::OnlyRight)
                continue;
            QStringList joined;
            for (int j = 0; j < blk.rightCount; ++j)
                joined << right.value(blk.rightStart + j);
            const QString content = joined.join(QLatin1Char('\n'));
            const auto it = leftContent.constFind(content);
            if (it != leftContent.constEnd()) {
                blocks[it.value()].movedPartner = i;
                blk.movedPartner = it.value();
                leftContent.erase(it); // pair every block only once
            }
        }
    }

    return blocks;
}

// ---------------------------------------------------------------------------
// Sliding of insertion and deletion blocks whose position is ambiguous
// ---------------------------------------------------------------------------

void DiffEngine::slideAmbiguousBlocks(QList<DiffBlock> &blocks,
                                      const QStringList &rawLeft,
                                      const QStringList &rawRight,
                                      const QStringList &normLeft,
                                      const QStringList &normRight)
{
    constexpr int kMaxSlide = 400; // Kostendeckel pro Block

    for (int i = 0; i < blocks.size(); ++i) {
        DiffBlock &b = blocks[i];
        const bool onlyRight = b.type == BlockType::OnlyRight;
        if ((b.type != BlockType::OnlyLeft && !onlyRight)
            || b.movedPartner >= 0) // moved pairs rely on the exact content
            continue;

        const QStringList &norm = onlyRight ? normRight : normLeft;
        const int start = onlyRight ? b.rightStart : b.leftStart;
        const int count = onlyRight ? b.rightCount : b.leftCount;
        if (count <= 0)
            continue;

        // A block can only slide through adjacent equal blocks.
        DiffBlock *prev = (i > 0 && blocks[i - 1].type == BlockType::Equal)
                              ? &blocks[i - 1] : nullptr;
        DiffBlock *next = (i + 1 < blocks.size()
                           && blocks[i + 1].type == BlockType::Equal)
                              ? &blocks[i + 1] : nullptr;

        // Maximum distance upwards (u) and downwards (d). The block stays
        // equivalent after normalisation as long as the line moving in at
        // herausfallenden entspricht.
        int maxUp = 0;
        if (prev) {
            const int limit = qMin(prev->leftCount, kMaxSlide);
            while (maxUp < limit
                   && norm.value(start - maxUp - 1)
                          == norm.value(start + count - maxUp - 1))
                ++maxUp;
        }
        int maxDown = 0;
        if (next) {
            const int limit = qMin(next->leftCount, kMaxSlide);
            while (maxDown < limit
                   && norm.value(start + maxDown)
                          == norm.value(start + count + maxDown))
                ++maxDown;
        }
        if (maxUp == 0 && maxDown == 0)
            continue;

        // Scoring: for every candidate position, count how many of the
        // context lines within the window match their counterpart RAW.
        const QStringList &srcRaw   = onlyRight ? rawRight : rawLeft;
        const QStringList &otherRaw = onlyRight ? rawLeft : rawRight;
        const int otherStart = onlyRight ? b.leftStart : b.rightStart;
        int bestShift = 0;
        int bestScore = -1;
        for (int s = -maxUp; s <= maxDown; ++s) {
            int score = 0;
            for (int m = 0; m < maxUp + maxDown; ++m) {
                const int srcLine = start - maxUp + m
                                    + ((start - maxUp + m >= start + s) ? count : 0);
                const int otherLine = otherStart - maxUp + m;
                if (srcRaw.value(srcLine) == otherRaw.value(otherLine))
                    ++score;
            }
            if (score > bestScore
                || (score == bestScore && qAbs(s) < qAbs(bestShift))) {
                bestScore = score;
                bestShift = s;
            }
        }
        if (bestShift == 0)
            continue;

        // Apply: move the block and its neighbours on both sides, so that
        // the insertion point of the other side follows along.
        const int s = bestShift;
        b.leftStart += s;
        b.rightStart += s;
        if (prev) {
            prev->leftCount += s;
            prev->rightCount += s;
        }
        if (next) {
            next->leftStart += s;
            next->rightStart += s;
            next->leftCount -= s;
            next->rightCount -= s;
        }
    }

    // Leergewordene Equal-Nachbarn entfernen; movedPartner-Indizes anpassen.
    QList<int> indexMap(blocks.size(), -1);
    QList<DiffBlock> compact;
    compact.reserve(blocks.size());
    for (int i = 0; i < blocks.size(); ++i) {
        if (blocks[i].type == BlockType::Equal && blocks[i].leftCount == 0)
            continue;
        indexMap[i] = compact.size();
        compact.append(blocks[i]);
    }
    for (DiffBlock &blk : compact) {
        if (blk.movedPartner >= 0)
            blk.movedPartner = indexMap.value(blk.movedPartner, -1);
    }
    blocks = compact;
}

void DiffEngine::markTrivialBlocks(QList<DiffBlock> &blocks,
                                   const QStringList &normLeft,
                                   const QStringList &normRight)
{
    for (DiffBlock &b : blocks) {
        if (b.type == BlockType::Equal || b.leftCount + b.rightCount == 0)
            continue;
        bool allEmpty = true;
        for (int j = 0; j < b.leftCount && allEmpty; ++j)
            allEmpty = normLeft.value(b.leftStart + j).isEmpty();
        for (int j = 0; j < b.rightCount && allEmpty; ++j)
            allEmpty = normRight.value(b.rightStart + j).isEmpty();
        b.trivial = allEmpty;
    }
}
