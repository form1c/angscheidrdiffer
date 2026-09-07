#include "diff/merge3engine.h"

#include <QTest>

namespace {

QStringList lines(std::initializer_list<const char *> l)
{
    QStringList out;
    for (const char *s : l)
        out << QString::fromUtf8(s);
    return out;
}

// Invariant: the chunks cover all three files in order and without a gap.
void checkCoverage(const QList<Merge3Chunk> &chunks,
                   int nBase, int nMine, int nTheirs)
{
    int b = 0, m = 0, t = 0;
    for (const Merge3Chunk &c : chunks) {
        QCOMPARE(c.baseStart, b);
        QCOMPARE(c.mineStart, m);
        QCOMPARE(c.theirsStart, t);
        QVERIFY(c.baseCount >= 0);
        QVERIFY(c.mineCount >= 0);
        QVERIFY(c.theirsCount >= 0);
        b += c.baseCount;
        m += c.mineCount;
        t += c.theirsCount;
    }
    QCOMPARE(b, nBase);
    QCOMPARE(m, nMine);
    QCOMPARE(t, nTheirs);
}

int countType(const QList<Merge3Chunk> &chunks, Merge3ChunkType type)
{
    int n = 0;
    for (const Merge3Chunk &c : chunks)
        if (c.type == type)
            ++n;
    return n;
}

} // namespace

class TstMerge3Engine : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void allIdentical();
    void onlyMineChange();
    void onlyTheirsChange();
    void bothSameChange();
    void conflictDetected();
    void separateChangesBothApplied();
    void insertSamePositionConflict();
    void deleteVersusEditConflict();
    void conflictChoices();
    void unresolvedMarkers();
    void chunkStartPositions();
};

void TstMerge3Engine::allIdentical()
{
    const QStringList f = lines({"a", "b", "c"});
    const auto chunks = Merge3Engine::compare(f, f, f);
    QCOMPARE(chunks.size(), 1);
    QCOMPARE(chunks[0].type, Merge3ChunkType::Stable);
    checkCoverage(chunks, 3, 3, 3);
    QCOMPARE(Merge3Engine::buildResult(chunks, f, f, f).lines, f);
}

void TstMerge3Engine::onlyMineChange()
{
    const QStringList base   = lines({"a", "b", "c"});
    const QStringList mine   = lines({"a", "B", "c"});
    const QStringList theirs = base;
    const auto chunks = Merge3Engine::compare(base, mine, theirs);
    checkCoverage(chunks, 3, 3, 3);
    QCOMPARE(countType(chunks, Merge3ChunkType::OnlyMine), 1);
    QCOMPARE(countType(chunks, Merge3ChunkType::Conflict), 0);
    const auto out = Merge3Engine::buildResult(chunks, base, mine, theirs);
    QCOMPARE(out.lines, mine);
    QCOMPARE(out.unresolvedConflicts, 0);
}

void TstMerge3Engine::onlyTheirsChange()
{
    const QStringList base   = lines({"a", "b", "c"});
    const QStringList mine   = base;
    const QStringList theirs = lines({"a", "c"}); // b deleted
    const auto chunks = Merge3Engine::compare(base, mine, theirs);
    checkCoverage(chunks, 3, 3, 2);
    QCOMPARE(countType(chunks, Merge3ChunkType::OnlyTheirs), 1);
    const auto out = Merge3Engine::buildResult(chunks, base, mine, theirs);
    QCOMPARE(out.lines, theirs);
}

void TstMerge3Engine::bothSameChange()
{
    const QStringList base   = lines({"a", "b", "c"});
    const QStringList edited = lines({"a", "B", "c"});
    const auto chunks = Merge3Engine::compare(base, edited, edited);
    checkCoverage(chunks, 3, 3, 3);
    QCOMPARE(countType(chunks, Merge3ChunkType::BothSame), 1);
    QCOMPARE(countType(chunks, Merge3ChunkType::Conflict), 0);
    const auto out = Merge3Engine::buildResult(chunks, base, edited, edited);
    QCOMPARE(out.lines, edited); // the change is taken exactly once
}

void TstMerge3Engine::conflictDetected()
{
    const QStringList base   = lines({"a", "b", "c"});
    const QStringList mine   = lines({"a", "MINE", "c"});
    const QStringList theirs = lines({"a", "THEIRS", "c"});
    const auto chunks = Merge3Engine::compare(base, mine, theirs);
    checkCoverage(chunks, 3, 3, 3);
    QCOMPARE(countType(chunks, Merge3ChunkType::Conflict), 1);
}

void TstMerge3Engine::separateChangesBothApplied()
{
    const QStringList base   = lines({"a", "b", "c", "d", "e"});
    const QStringList mine   = lines({"A", "b", "c", "d", "e"});
    const QStringList theirs = lines({"a", "b", "c", "d", "E"});
    const auto chunks = Merge3Engine::compare(base, mine, theirs);
    checkCoverage(chunks, 5, 5, 5);
    QCOMPARE(countType(chunks, Merge3ChunkType::Conflict), 0);
    const auto out = Merge3Engine::buildResult(chunks, base, mine, theirs);
    QCOMPARE(out.lines, lines({"A", "b", "c", "d", "E"}));
}

void TstMerge3Engine::insertSamePositionConflict()
{
    const QStringList base   = lines({"a", "b"});
    const QStringList mine   = lines({"a", "M1", "b"});
    const QStringList theirs = lines({"a", "T1", "b"});
    const auto chunks = Merge3Engine::compare(base, mine, theirs);
    checkCoverage(chunks, 2, 3, 3);
    QCOMPARE(countType(chunks, Merge3ChunkType::Conflict), 1);
}

void TstMerge3Engine::deleteVersusEditConflict()
{
    const QStringList base   = lines({"a", "b", "c"});
    const QStringList mine   = lines({"a", "c"});          // b deleted
    const QStringList theirs = lines({"a", "B!", "c"});    // b changed
    const auto chunks = Merge3Engine::compare(base, mine, theirs);
    checkCoverage(chunks, 3, 2, 3);
    QCOMPARE(countType(chunks, Merge3ChunkType::Conflict), 1);
}

void TstMerge3Engine::conflictChoices()
{
    const QStringList base   = lines({"a", "b", "c"});
    const QStringList mine   = lines({"a", "MINE", "c"});
    const QStringList theirs = lines({"a", "THEIRS", "c"});
    const auto chunks = Merge3Engine::compare(base, mine, theirs);

    int conflictIdx = -1;
    for (int i = 0; i < chunks.size(); ++i)
        if (chunks[i].type == Merge3ChunkType::Conflict)
            conflictIdx = i;
    QVERIFY(conflictIdx >= 0);

    QHash<int, Merge3Choice> choose;
    choose[conflictIdx] = Merge3Choice::Mine;
    QCOMPARE(Merge3Engine::buildResult(chunks, base, mine, theirs,
                                       choose).lines, mine);
    choose[conflictIdx] = Merge3Choice::Theirs;
    QCOMPARE(Merge3Engine::buildResult(chunks, base, mine, theirs,
                                       choose).lines, theirs);
    choose[conflictIdx] = Merge3Choice::Base;
    QCOMPARE(Merge3Engine::buildResult(chunks, base, mine, theirs,
                                       choose).lines, base);
    choose[conflictIdx] = Merge3Choice::MineThenTheirs;
    QCOMPARE(Merge3Engine::buildResult(chunks, base, mine, theirs,
                                       choose).lines,
             lines({"a", "MINE", "THEIRS", "c"}));
    choose[conflictIdx] = Merge3Choice::TheirsThenMine;
    QCOMPARE(Merge3Engine::buildResult(chunks, base, mine, theirs,
                                       choose).lines,
             lines({"a", "THEIRS", "MINE", "c"}));
}

void TstMerge3Engine::unresolvedMarkers()
{
    const QStringList base   = lines({"a", "b", "c"});
    const QStringList mine   = lines({"a", "MINE", "c"});
    const QStringList theirs = lines({"a", "THEIRS", "c"});
    const auto chunks = Merge3Engine::compare(base, mine, theirs);
    const auto out = Merge3Engine::buildResult(chunks, base, mine, theirs);
    QCOMPARE(out.unresolvedConflicts, 1);
    QVERIFY(out.lines.contains(QStringLiteral("<<<<<<< MINE")));
    QVERIFY(out.lines.contains(QStringLiteral("||||||| BASE")));
    QVERIFY(out.lines.contains(QStringLiteral("=======")));
    QVERIFY(out.lines.contains(QStringLiteral(">>>>>>> THEIRS")));
    QVERIFY(out.lines.contains(QStringLiteral("MINE")));
    QVERIFY(out.lines.contains(QStringLiteral("THEIRS")));
}

void TstMerge3Engine::chunkStartPositions()
{
    const QStringList base   = lines({"a", "b", "c", "d"});
    const QStringList mine   = lines({"a", "M", "c", "d"});
    const QStringList theirs = lines({"a", "b", "c", "T"});
    const auto chunks = Merge3Engine::compare(base, mine, theirs);
    const auto out = Merge3Engine::buildResult(chunks, base, mine, theirs);
    QCOMPARE(out.chunkStart.size(), chunks.size());
    // Every chunkStart has to point at the first result line of its chunk.
    int expected = 0;
    for (int i = 0; i < chunks.size(); ++i) {
        QCOMPARE(out.chunkStart[i], expected);
        const Merge3Chunk &c = chunks[i];
        switch (c.type) {
        case Merge3ChunkType::OnlyTheirs: expected += c.theirsCount; break;
        default: expected += c.mineCount; break;
        }
    }
    QCOMPARE(expected, out.lines.size());
}

QTEST_GUILESS_MAIN(TstMerge3Engine)
#include "tst_merge3engine.moc"
