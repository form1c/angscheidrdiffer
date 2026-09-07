#include "diff/diffengine.h"
#include "diff/mergeengine.h"

#include <QTest>

// Applies line edits to a list of lines, from the bottom up. Test helper.
static QStringList applied(QStringList lines,
                           QList<MergeEngine::LineEdit> edits)
{
    std::sort(edits.begin(), edits.end(),
              [](const MergeEngine::LineEdit &a, const MergeEngine::LineEdit &b) {
                  return a.dstStart > b.dstStart;
              });
    for (const MergeEngine::LineEdit &e : edits) {
        for (int i = 0; i < e.dstCount; ++i)
            lines.removeAt(e.dstStart);
        for (int i = e.newLines.size() - 1; i >= 0; --i)
            lines.insert(e.dstStart, e.newLines[i]);
    }
    return lines;
}

class TstMergeEngine : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void similarityBasics();
    void alignPairsSimilarLines();
    void blockEditReplace();
    void blockEditUseBoth();
    void selectionMergeUserExample();
    void selectionKeepsUnselectedChanges();
    void selectionInsertsUnmatchedLine();
    void selectionPureInsertionBlock();
    void useBothSelectionIncludesEqualLines();
    void useBothSelectionInsertBefore();
};

void TstMergeEngine::similarityBasics()
{
    QVERIFY(MergeEngine::lineSimilarity(
                QStringLiteral("    test();"),
                QStringLiteral("    test(a);")) > 0.7);
    QVERIFY(MergeEngine::lineSimilarity(
                QStringLiteral("// new line"),
                QStringLiteral("test(a);")) < 0.5);
    QCOMPARE(MergeEngine::lineSimilarity(QString(), QString()), 1.0);
    QCOMPARE(MergeEngine::lineSimilarity(QStringLiteral("abc"), QString()), 0.0);
}

void TstMergeEngine::alignPairsSimilarLines()
{
    // Reported case: "test(a);" on the right has to pair with "test();" on
    // the left, not with the comment.
    const QList<int> align = MergeEngine::alignBlockRows(
        {QStringLiteral("    test(a);")},
        {QStringLiteral("    // new line"), QStringLiteral("    test();")});
    QCOMPARE(align.size(), 1);
    QCOMPARE(align[0], 1);
}

void TstMergeEngine::blockEditReplace()
{
    // Changed block: lines 1 to 2 on the left, line 1 on the right.
    const DiffBlock b{BlockType::Changed, 1, 2, 1, 1, -1};
    const QStringList left  = {QStringLiteral("A"), QStringLiteral("L1"),
                               QStringLiteral("L2"), QStringLiteral("Z")};
    const QStringList right = {QStringLiteral("A"), QStringLiteral("R1"),
                               QStringLiteral("Z")};

    // Right to left: the block on the left is replaced by R1.
    const auto edit = MergeEngine::blockEdit(b, right, left,
                                             /*srcIsLeft=*/false,
                                             MergeMode::Replace);
    QCOMPARE(edit.dstStart, 1);
    QCOMPARE(edit.dstCount, 2);
    QCOMPARE(edit.newLines, QStringList{QStringLiteral("R1")});
}

void TstMergeEngine::blockEditUseBoth()
{
    const DiffBlock b{BlockType::Changed, 1, 1, 1, 1, -1};
    const QStringList left  = {QStringLiteral("A"), QStringLiteral("L"),
                               QStringLiteral("Z")};
    const QStringList right = {QStringLiteral("A"), QStringLiteral("R"),
                               QStringLiteral("Z")};

    // Use Both, left first: the target on the right becomes L, then R.
    const auto first = MergeEngine::blockEdit(b, left, right, true,
                                              MergeMode::InsertBefore);
    QCOMPARE(first.newLines,
             (QStringList{QStringLiteral("L"), QStringLiteral("R")}));

    // Use Both, left last: the target on the right becomes R, then L.
    const auto last = MergeEngine::blockEdit(b, left, right, true,
                                             MergeMode::InsertAfter);
    QCOMPARE(last.newLines,
             (QStringList{QStringLiteral("R"), QStringLiteral("L")}));
}

void TstMergeEngine::selectionMergeUserExample()
{
    // Exactly the reported example:
    const QStringList left = {
        QStringLiteral("int add(int a, int b)"),
        QStringLiteral("{"),
        QStringLiteral("    // new line"),
        QStringLiteral("    test();"),
        QStringLiteral("    return a + b;"),
        QStringLiteral("}"),
    };
    const QStringList right = {
        QStringLiteral("int add(int a, int b)"),
        QStringLiteral("{"),
        QStringLiteral("    test(a);"),
        QStringLiteral("    return a + b;"),
        QStringLiteral("}"),
    };
    const auto blocks = DiffEngine::compare(left, right);

    // "test(a);" on the right, line 2, is selected and merged to the left:
    const auto edits = MergeEngine::selectionEdits(
        blocks, right, left, /*srcIsLeft=*/false, 2, 1);
    const QStringList result = applied(left, edits);

    const QStringList expected = {
        QStringLiteral("int add(int a, int b)"),
        QStringLiteral("{"),
        QStringLiteral("    // new line"),   // has to survive
        QStringLiteral("    test(a);"),        // ersetzt test();
        QStringLiteral("    return a + b;"),
        QStringLiteral("}"),
    };
    QCOMPARE(result, expected);
}

void TstMergeEngine::selectionKeepsUnselectedChanges()
{
    // Two changed lines in the block, only ONE of them selected. The other
    // target line stays untouched.
    const QStringList left  = {QStringLiteral("aaa X"), QStringLiteral("bbb X")};
    const QStringList right = {QStringLiteral("aaa Y"), QStringLiteral("bbb Y")};
    const auto blocks = DiffEngine::compare(left, right);

    // Only line 0 on the right is selected and merged to the left:
    const auto edits = MergeEngine::selectionEdits(
        blocks, right, left, false, 0, 1);
    const QStringList result = applied(left, edits);
    QCOMPARE(result,
             (QStringList{QStringLiteral("aaa Y"), QStringLiteral("bbb X")}));
}

void TstMergeEngine::selectionInsertsUnmatchedLine()
{
    // A selected source line without a partner is inserted, target stays.
    const QStringList left  = {QStringLiteral("common"),
                               QStringLiteral("only left")};
    const QStringList right = {QStringLiteral("common"),
                               QStringLiteral("brand new line"),
                               QStringLiteral("only left")};
    const auto blocks = DiffEngine::compare(left, right);

    const auto edits = MergeEngine::selectionEdits(
        blocks, right, left, false, 1, 1);
    const QStringList result = applied(left, edits);
    QCOMPARE(result,
             (QStringList{QStringLiteral("common"),
                          QStringLiteral("brand new line"),
                          QStringLiteral("only left")}));
}

void TstMergeEngine::selectionPureInsertionBlock()
{
    // A right-only block: the selected new lines are inserted on the left.
    const QStringList left  = {QStringLiteral("A"), QStringLiteral("B")};
    const QStringList right = {QStringLiteral("A"), QStringLiteral("N1"),
                               QStringLiteral("N2"), QStringLiteral("B")};
    const auto blocks = DiffEngine::compare(left, right);

    const auto edits = MergeEngine::selectionEdits(
        blocks, right, left, false, 1, 2);
    const QStringList result = applied(left, edits);
    QCOMPARE(result,
             (QStringList{QStringLiteral("A"), QStringLiteral("N1"),
                          QStringLiteral("N2"), QStringLiteral("B")}));
}

void TstMergeEngine::useBothSelectionIncludesEqualLines()
{
    // Reported case: the selection includes the closing "}", a line that is
    // equal on both sides. Use Both, right last, has to take EVERY line.
    const QStringList left  = {QStringLiteral("A"), QStringLiteral("L1"),
                               QStringLiteral("}")};
    const QStringList right = {QStringLiteral("A"), QStringLiteral("R1"),
                               QStringLiteral("R2"), QStringLiteral("}")};
    const auto blocks = DiffEngine::compare(left, right);

    // Lines 1 to 3 on the right are selected (R1, R2, }), right last means
    // they are inserted after the section on the left:
    const auto edit = MergeEngine::useBothSelectionEdit(
        blocks, right, left.size(), /*srcIsLeft=*/false, 1, 3,
        /*insertBefore=*/false);
    const QStringList result = applied(left, {edit});

    const QStringList expected = {
        QStringLiteral("A"), QStringLiteral("L1"), QStringLiteral("}"),
        QStringLiteral("R1"), QStringLiteral("R2"), QStringLiteral("}"),
    };
    QCOMPARE(result, expected);
    QVERIFY(result.count(QStringLiteral("}")) == 2); // the "}" came along
}

void TstMergeEngine::useBothSelectionInsertBefore()
{
    const QStringList left  = {QStringLiteral("A"), QStringLiteral("L1"),
                               QStringLiteral("Z")};
    const QStringList right = {QStringLiteral("A"), QStringLiteral("R1"),
                               QStringLiteral("Z")};
    const auto blocks = DiffEngine::compare(left, right);

    // R1 on the right is selected, right first inserts BEFORE the section:
    const auto edit = MergeEngine::useBothSelectionEdit(
        blocks, right, left.size(), false, 1, 1, true);
    const QStringList result = applied(left, {edit});
    QCOMPARE(result,
             (QStringList{QStringLiteral("A"), QStringLiteral("R1"),
                          QStringLiteral("L1"), QStringLiteral("Z")}));
}

QTEST_GUILESS_MAIN(TstMergeEngine)
#include "tst_mergeengine.moc"
