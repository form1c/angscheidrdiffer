#include "diff/diffengine.h"
#include "diff/commentstripper.h"
#include "diff/foldercompare.h"
#include "diff/intralinediff.h"
#include "diff/linenormalizer.h"

#include <QDir>
#include <QTemporaryDir>
#include <QTest>

class TstDiffEngine : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void bothEmpty();
    void identical();
    void leftEmpty();
    void rightEmpty();
    void pureInsertion();
    void pureDeletion();
    void singleChange();
    void mixed();
    void blocksCoverAllLines();
    void largeFile();
    void commentStripCpp();
    void commentStripHtml();
    void normalizeWhitespace();
    void normalizeCase();
    void ignoreFirstLines();
    void ignoreFirstLinesShortFile();
    void intraline();
    void movedBlockDetected();
    void changedMovedBlockNotDetected();
    void tinyMovedLineNotDetected();
    void slideInsertionToRawMatch();
    void slideNoOpWithoutAmbiguity();
    void slideKeepsCoverage();
    void trivialBlockMarked();
    void folderIdentical();
    void folderOnlyLeftAndChanged();
    void folderSubfolderAggregation();
    void folderExcludePattern();
    void folderIgnoreFirstLines();
    void folderIgnoreEol();
    void folderQuickComparesSizeOnly();
    void folderBinaryComparedByBytes();
    void folderTypeConflict();
    void folderOneSidedDirNotDescended();
    void folderCancelStopsScan();
    void trivialNotMarkedWhenLinesCarryContent();
    void memoryGuardDegradesToOneBlock();
    void folderExcludeWildcard();
    void intralineEdgeCases();
};

void TstDiffEngine::bothEmpty()
{
    const auto blocks = DiffEngine::compare({}, {});
    QVERIFY(blocks.isEmpty());
}

void TstDiffEngine::identical()
{
    const QStringList lines = {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")};
    const auto blocks = DiffEngine::compare(lines, lines);
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].type, BlockType::Equal);
    QCOMPARE(blocks[0].leftCount, 3);
    QCOMPARE(blocks[0].rightCount, 3);
}

void TstDiffEngine::leftEmpty()
{
    const auto blocks = DiffEngine::compare({}, {QStringLiteral("x"), QStringLiteral("y")});
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].type, BlockType::OnlyRight);
    QCOMPARE(blocks[0].rightCount, 2);
    QCOMPARE(blocks[0].leftCount, 0);
}

void TstDiffEngine::rightEmpty()
{
    const auto blocks = DiffEngine::compare({QStringLiteral("x")}, {});
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].type, BlockType::OnlyLeft);
    QCOMPARE(blocks[0].leftCount, 1);
}

void TstDiffEngine::pureInsertion()
{
    const QStringList l = {QStringLiteral("a"), QStringLiteral("c")};
    const QStringList r = {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")};
    const auto blocks = DiffEngine::compare(l, r);
    QCOMPARE(blocks.size(), 3);
    QCOMPARE(blocks[0].type, BlockType::Equal);
    QCOMPARE(blocks[1].type, BlockType::OnlyRight);
    QCOMPARE(blocks[1].rightStart, 1);
    QCOMPARE(blocks[1].rightCount, 1);
    QCOMPARE(blocks[1].leftStart, 1);
    QCOMPARE(blocks[1].leftCount, 0);
    QCOMPARE(blocks[2].type, BlockType::Equal);
}

void TstDiffEngine::pureDeletion()
{
    const QStringList l = {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")};
    const QStringList r = {QStringLiteral("a"), QStringLiteral("c")};
    const auto blocks = DiffEngine::compare(l, r);
    QCOMPARE(blocks.size(), 3);
    QCOMPARE(blocks[1].type, BlockType::OnlyLeft);
    QCOMPARE(blocks[1].leftStart, 1);
    QCOMPARE(blocks[1].leftCount, 1);
}

void TstDiffEngine::singleChange()
{
    const QStringList l = {QStringLiteral("a"), QStringLiteral("OLD"), QStringLiteral("c")};
    const QStringList r = {QStringLiteral("a"), QStringLiteral("NEW"), QStringLiteral("c")};
    const auto blocks = DiffEngine::compare(l, r);
    QCOMPARE(blocks.size(), 3);
    QCOMPARE(blocks[1].type, BlockType::Changed);
    QCOMPARE(blocks[1].leftCount, 1);
    QCOMPARE(blocks[1].rightCount, 1);
}

void TstDiffEngine::mixed()
{
    const QStringList l = {QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"),
                            QStringLiteral("4"), QStringLiteral("5")};
    const QStringList r = {QStringLiteral("1"), QStringLiteral("zwei"), QStringLiteral("3"),
                            QStringLiteral("5"), QStringLiteral("6")};
    const auto blocks = DiffEngine::compare(l, r);

    // Consistency: the ranges have to cover both sides without a gap.
    int li = 0, ri = 0;
    for (const DiffBlock &b : blocks) {
        QCOMPARE(b.leftStart, li);
        QCOMPARE(b.rightStart, ri);
        li += b.leftCount;
        ri += b.rightCount;
    }
    QCOMPARE(li, l.size());
    QCOMPARE(ri, r.size());
}

void TstDiffEngine::blocksCoverAllLines()
{
    const QStringList l = {QStringLiteral("x"), QStringLiteral("a"), QStringLiteral("b")};
    const QStringList r = {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("y"),
                            QStringLiteral("z")};
    const auto blocks = DiffEngine::compare(l, r);
    int li = 0, ri = 0;
    for (const DiffBlock &b : blocks) {
        QCOMPARE(b.leftStart, li);
        QCOMPARE(b.rightStart, ri);
        li += b.leftCount;
        ri += b.rightCount;
    }
    QCOMPARE(li, l.size());
    QCOMPARE(ri, r.size());
}

void TstDiffEngine::largeFile()
{
    QStringList l, r;
    for (int i = 0; i < 5000; ++i) {
        l << QStringLiteral("line %1").arg(i);
        r << QStringLiteral("line %1").arg(i);
    }
    r[2500] = QStringLiteral("modified");
    const auto blocks = DiffEngine::compare(l, r);
    QCOMPARE(blocks.size(), 3);
    QCOMPARE(blocks[1].type, BlockType::Changed);
    QCOMPARE(blocks[1].leftStart, 2500);
}

void TstDiffEngine::commentStripCpp()
{
    const QStringList in = {
        QStringLiteral("int a = 1; // Kommentar"),
        QStringLiteral("/* Block"),
        QStringLiteral("   weiter */ int b = 2;"),
        QStringLiteral("QString s = \"http://not/a/comment\";"),
    };
    const QStringList out = CommentStripper::stripComments(in, Language::CFamily);
    QCOMPARE(out.size(), in.size());
    QCOMPARE(out[0], QStringLiteral("int a = 1; "));
    QCOMPARE(out[1], QString());
    QCOMPARE(out[2], QStringLiteral(" int b = 2;"));
    QCOMPARE(out[3], in[3]); // "//" inside a string literal is kept
}

void TstDiffEngine::commentStripHtml()
{
    const QStringList in = {
        QStringLiteral("<p>Text</p><!-- Kommentar -->"),
        QStringLiteral("<!-- mehrzeilig"),
        QStringLiteral("ends here --><div>"),
    };
    const QStringList out = CommentStripper::stripComments(in, Language::Html);
    QCOMPARE(out[0], QStringLiteral("<p>Text</p>"));
    QCOMPARE(out[1], QString());
    QCOMPARE(out[2], QStringLiteral("<div>"));
}

void TstDiffEngine::normalizeWhitespace()
{
    DiffOptions opt;
    opt.ignoreWhitespace = true;
    const QStringList out = LineNormalizer::normalize(
        {QStringLiteral("  int   a = 1;  ")}, opt, Language::None);
    QCOMPARE(out[0], QStringLiteral("int a = 1;"));
}

void TstDiffEngine::normalizeCase()
{
    DiffOptions opt;
    opt.ignoreCase = true;
    const QStringList out = LineNormalizer::normalize(
        {QStringLiteral("Int A = 1;")}, opt, Language::None);
    QCOMPARE(out[0], QStringLiteral("int a = 1;"));
}

void TstDiffEngine::movedBlockDetected()
{
    // A block of three lines moved from the top to the bottom.
    const QStringList l = {QStringLiteral("void moved() {"),
                            QStringLiteral("    work();"),
                            QStringLiteral("}"),
                            QStringLiteral("common 1"),
                            QStringLiteral("common 2")};
    const QStringList r = {QStringLiteral("common 1"),
                            QStringLiteral("common 2"),
                            QStringLiteral("void moved() {"),
                            QStringLiteral("    work();"),
                            QStringLiteral("}")};
    const auto blocks = DiffEngine::compare(l, r);

    int movedPairs = 0;
    for (int i = 0; i < blocks.size(); ++i) {
        if (blocks[i].movedPartner >= 0) {
            ++movedPairs;
            // The partner has to point back.
            QCOMPARE(blocks[blocks[i].movedPartner].movedPartner, i);
        }
    }
    QCOMPARE(movedPairs, 2); // a left-only and a right-only block as a pair
}

void TstDiffEngine::changedMovedBlockNotDetected()
{
    // Moved AND changed, which is not a move pair.
    const QStringList l = {QStringLiteral("void moved() {"),
                            QStringLiteral("    workOld();"),
                            QStringLiteral("}"),
                            QStringLiteral("common 1"),
                            QStringLiteral("common 2")};
    const QStringList r = {QStringLiteral("common 1"),
                            QStringLiteral("common 2"),
                            QStringLiteral("void moved() {"),
                            QStringLiteral("    workNew();"),
                            QStringLiteral("}")};
    const auto blocks = DiffEngine::compare(l, r);
    for (const DiffBlock &b : blocks)
        QCOMPARE(b.movedPartner, -1);
}

void TstDiffEngine::tinyMovedLineNotDetected()
{
    // Content that is too short ("}") must not be reported as a move.
    const QStringList l = {QStringLiteral("}"),
                            QStringLiteral("common 1"),
                            QStringLiteral("common 2")};
    const QStringList r = {QStringLiteral("common 1"),
                            QStringLiteral("common 2"),
                            QStringLiteral("}")};
    const auto blocks = DiffEngine::compare(l, r);
    for (const DiffBlock &b : blocks)
        QCOMPARE(b.movedPartner, -1);
}

void TstDiffEngine::intraline()
{
    const auto span = IntralineDiff::compare(
        QStringLiteral("int alt = 1;"), QStringLiteral("int neu = 1;"));
    QVERIFY(span.valid);
    QCOMPARE(span.leftStart, 4);
    QCOMPARE(span.leftLen, 3);
    QCOMPARE(span.rightStart, 4);
    QCOMPARE(span.rightLen, 3);
}

void TstDiffEngine::ignoreFirstLines()
{
    // The keyword header in line 1 differs.
    const QStringList l = {QStringLiteral("/* $Id: a.cpp 100 $ */"),
                            QStringLiteral("int x = 1;")};
    const QStringList r = {QStringLiteral("/* $Id: a.cpp 205 $ */"),
                            QStringLiteral("int x = 1;")};

    DiffOptions off;
    const auto blocksOff = DiffEngine::compare(
        LineNormalizer::normalize(l, off, Language::None),
        LineNormalizer::normalize(r, off, Language::None));
    QVERIFY(blocksOff.size() > 1); // Unterschied sichtbar

    DiffOptions on;
    on.ignoreFirstLines = 1;
    const auto blocksOn = DiffEngine::compare(
        LineNormalizer::normalize(l, on, Language::None),
        LineNormalizer::normalize(r, on, Language::None));
    QCOMPARE(blocksOn.size(), 1);
    QCOMPARE(blocksOn[0].type, BlockType::Equal);
}

void TstDiffEngine::ignoreFirstLinesShortFile()
{
    // A file shorter than N must not crash.
    DiffOptions opt;
    opt.ignoreFirstLines = 10;
    const QStringList out = LineNormalizer::normalize(
        {QStringLiteral("a"), QStringLiteral("b")}, opt, Language::None);
    QCOMPARE(out.size(), 2);
    QCOMPARE(out[0], QString());
    QCOMPARE(out[1], QString());
}

// --- Folder comparison -----------------------------------------------------

namespace {

void writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(content);
}

// Finds a child entry by name, on the top level only.
const FolderDiffEntry *childByName(const FolderDiffEntry &root, const QString &name)
{
    for (const FolderDiffEntry &c : root.children) {
        if (c.name == name)
            return &c;
    }
    return nullptr;
}

} // namespace

void TstDiffEngine::folderIdentical()
{
    QTemporaryDir tmp;
    const QString l = tmp.path() + QStringLiteral("/l");
    const QString r = tmp.path() + QStringLiteral("/r");
    writeFile(l + QStringLiteral("/a.txt"), "hello\n");
    writeFile(r + QStringLiteral("/a.txt"), "hello\n");

    const auto root = FolderCompare::compareFolders(l, r, {});
    QCOMPARE(root.status, FolderEntryStatus::Identical);
    QVERIFY(!FolderCompare::hasDifferences(root));
}

void TstDiffEngine::folderOnlyLeftAndChanged()
{
    QTemporaryDir tmp;
    const QString l = tmp.path() + QStringLiteral("/l");
    const QString r = tmp.path() + QStringLiteral("/r");
    writeFile(l + QStringLiteral("/only-left.txt"), "x\n");
    writeFile(l + QStringLiteral("/changed.txt"), "old\n");
    writeFile(r + QStringLiteral("/changed.txt"), "new\n");
    writeFile(r + QStringLiteral("/only-right.txt"), "y\n");

    const auto root = FolderCompare::compareFolders(l, r, {});
    QCOMPARE(root.status, FolderEntryStatus::Different);
    QVERIFY(FolderCompare::hasDifferences(root));

    const auto *onlyLeft = childByName(root, QStringLiteral("only-left.txt"));
    QVERIFY(onlyLeft);
    QCOMPARE(onlyLeft->status, FolderEntryStatus::OnlyLeft);

    const auto *changed = childByName(root, QStringLiteral("changed.txt"));
    QVERIFY(changed);
    QCOMPARE(changed->status, FolderEntryStatus::Different);

    const auto *onlyRight = childByName(root, QStringLiteral("only-right.txt"));
    QVERIFY(onlyRight);
    QCOMPARE(onlyRight->status, FolderEntryStatus::OnlyRight);
}

void TstDiffEngine::folderSubfolderAggregation()
{
    QTemporaryDir tmp;
    const QString l = tmp.path() + QStringLiteral("/l");
    const QString r = tmp.path() + QStringLiteral("/r");
    writeFile(l + QStringLiteral("/sub/deep.txt"), "old\n");
    writeFile(r + QStringLiteral("/sub/deep.txt"), "new\n");

    const auto root = FolderCompare::compareFolders(l, r, {});
    const auto *sub = childByName(root, QStringLiteral("sub"));
    QVERIFY(sub);
    QVERIFY(sub->isDir);
    QCOMPARE(sub->status, FolderEntryStatus::Different); // Aggregat
}

void TstDiffEngine::folderExcludePattern()
{
    QTemporaryDir tmp;
    const QString l = tmp.path() + QStringLiteral("/l");
    const QString r = tmp.path() + QStringLiteral("/r");
    writeFile(l + QStringLiteral("/.svn/entries"), "left\n");
    writeFile(r + QStringLiteral("/.svn/entries"), "right\n");
    writeFile(l + QStringLiteral("/a.txt"), "same\n");
    writeFile(r + QStringLiteral("/a.txt"), "same\n");

    FolderCompareSettings s;
    s.excludePatterns = {QStringLiteral(".svn")};
    const auto root = FolderCompare::compareFolders(l, r, s);
    QCOMPARE(root.status, FolderEntryStatus::Identical);
    QVERIFY(!childByName(root, QStringLiteral(".svn")));
}

void TstDiffEngine::folderIgnoreFirstLines()
{
    QTemporaryDir tmp;
    const QString l = tmp.path() + QStringLiteral("/l");
    const QString r = tmp.path() + QStringLiteral("/r");
    writeFile(l + QStringLiteral("/a.cpp"), "/* $Id: 1 $ */\nint x;\n");
    writeFile(r + QStringLiteral("/a.cpp"), "/* $Id: 2 $ */\nint x;\n");

    const auto without = FolderCompare::compareFolders(l, r, {});
    QCOMPARE(childByName(without, QStringLiteral("a.cpp"))->status,
             FolderEntryStatus::Different);

    FolderCompareSettings s;
    s.diffOptions.ignoreFirstLines = 1;
    const auto with = FolderCompare::compareFolders(l, r, s);
    QCOMPARE(childByName(with, QStringLiteral("a.cpp"))->status,
             FolderEntryStatus::Identical);
}

// A comment-only block was inserted on the right. With comments ignored,
// every candidate line normalises to empty, so the algorithm places the
// insertion at the end of the run. The pass has to slide it back to the
// position the raw lines suggest.
void TstDiffEngine::slideInsertionToRawMatch()
{
    const QStringList rawL = {QStringLiteral("start"),
                              QStringLiteral("// alpha"),
                              QStringLiteral("// beta"),
                              QStringLiteral("end")};
    const QStringList rawR = {QStringLiteral("start"),
                              QStringLiteral("// neu 1"),
                              QStringLiteral("// neu 2"),
                              QStringLiteral("// alpha"),
                              QStringLiteral("// beta"),
                              QStringLiteral("end")};
    DiffOptions opt;
    opt.ignoreComments = true;
    const QStringList normL = LineNormalizer::normalize(rawL, opt, Language::CFamily);
    const QStringList normR = LineNormalizer::normalize(rawR, opt, Language::CFamily);

    auto blocks = DiffEngine::compare(normL, normR);
    DiffEngine::slideAmbiguousBlocks(blocks, rawL, rawR, normL, normR);

    int insertions = 0;
    for (const DiffBlock &b : blocks) {
        if (b.type != BlockType::OnlyRight)
            continue;
        ++insertions;
        QCOMPARE(b.rightStart, 1);   // at the insertion point of the raw text
        QCOMPARE(b.rightCount, 2);
        QCOMPARE(b.leftStart, 1);    // and the left anchor moves along
    }
    QCOMPARE(insertions, 1);
    // Danach paaren "// alpha"/"// beta" wieder roh-gleich.
}

void TstDiffEngine::slideNoOpWithoutAmbiguity()
{
    const QStringList l = {QStringLiteral("a"), QStringLiteral("b"),
                           QStringLiteral("c")};
    const QStringList r = {QStringLiteral("a"), QStringLiteral("x"),
                           QStringLiteral("b"), QStringLiteral("c")};
    auto blocks = DiffEngine::compare(l, r);
    const auto before = blocks;
    DiffEngine::slideAmbiguousBlocks(blocks, l, r, l, r);
    QCOMPARE(blocks.size(), before.size());
    for (int i = 0; i < blocks.size(); ++i) {
        QCOMPARE(blocks[i].rightStart, before[i].rightStart);
        QCOMPARE(blocks[i].leftStart, before[i].leftStart);
    }
}

// After sliding, the blocks still have to cover both files without a gap,
// the same invariant as in blocksCoverAllLines.
void TstDiffEngine::slideKeepsCoverage()
{
    const QStringList rawL = {QStringLiteral("s"), QStringLiteral("//k1"),
                              QStringLiteral("//k2"), QStringLiteral("//k3"),
                              QStringLiteral("e")};
    const QStringList rawR = {QStringLiteral("s"), QStringLiteral("//n"),
                              QStringLiteral("//k1"), QStringLiteral("//k2"),
                              QStringLiteral("//k3"), QStringLiteral("e")};
    DiffOptions opt;
    opt.ignoreComments = true;
    const QStringList normL = LineNormalizer::normalize(rawL, opt, Language::CFamily);
    const QStringList normR = LineNormalizer::normalize(rawR, opt, Language::CFamily);
    auto blocks = DiffEngine::compare(normL, normR);
    DiffEngine::slideAmbiguousBlocks(blocks, rawL, rawR, normL, normR);

    int nextL = 0, nextR = 0;
    for (const DiffBlock &b : blocks) {
        QCOMPARE(b.leftStart, nextL);
        QCOMPARE(b.rightStart, nextR);
        QVERIFY(b.leftCount >= 0);
        QVERIFY(b.rightCount >= 0);
        nextL += b.leftCount;
        nextR += b.rightCount;
    }
    QCOMPARE(nextL, rawL.size());
    QCOMPARE(nextR, rawR.size());
}

// A comment-only insertion counts as trivial while comments are ignored.
// A real difference in code beside it does not.
void TstDiffEngine::trivialBlockMarked()
{
    const QStringList rawL = {QStringLiteral("code();"),
                              QStringLiteral("mid();"),
                              QStringLiteral("end();")};
    const QStringList rawR = {QStringLiteral("code();"),
                              QStringLiteral("// comment only"),
                              QStringLiteral("mid();"),
                              QStringLiteral("changed();")};
    DiffOptions opt;
    opt.ignoreComments = true;
    const QStringList normL = LineNormalizer::normalize(rawL, opt, Language::CFamily);
    const QStringList normR = LineNormalizer::normalize(rawR, opt, Language::CFamily);

    auto blocks = DiffEngine::compare(normL, normR);
    DiffEngine::markTrivialBlocks(blocks, normL, normR);

    bool sawTrivial = false, sawReal = false;
    for (const DiffBlock &b : blocks) {
        if (b.type == BlockType::Equal)
            continue;
        if (b.trivial)
            sawTrivial = true;
        else
            sawReal = true;
    }
    QVERIFY(sawTrivial);  // the comment line
    QVERIFY(sawReal);     // end() vs changed()
}


// Treating line endings as equal: CRLF against LF. Without the option the
// difference counts, with it it does not.
void TstDiffEngine::folderIgnoreEol()
{
    QTemporaryDir tmp;
    const QString l = tmp.path() + QStringLiteral("/l");
    const QString r = tmp.path() + QStringLiteral("/r");
    writeFile(l + QStringLiteral("/a.txt"), "one\r\ntwo\r\n");
    writeFile(r + QStringLiteral("/a.txt"), "one\ntwo\n");

    const auto without = FolderCompare::compareFolders(l, r, {});
    QCOMPARE(childByName(without, QStringLiteral("a.txt"))->status,
             FolderEntryStatus::Different);

    FolderCompareSettings s;
    s.ignoreEol = true;
    const auto with = FolderCompare::compareFolders(l, r, s);
    QCOMPARE(childByName(with, QStringLiteral("a.txt"))->status,
             FolderEntryStatus::Identical);
}

// The quick mode compares the file size only. Files of equal length with
// different content count as equal there, and as different with a content
// comparison. The second half is the actual proof: without it the case
// would stay green over a content comparison as well.
void TstDiffEngine::folderQuickComparesSizeOnly()
{
    QTemporaryDir tmp;
    const QString l = tmp.path() + QStringLiteral("/l");
    const QString r = tmp.path() + QStringLiteral("/r");
    writeFile(l + QStringLiteral("/a.txt"), "aaa\n");
    writeFile(r + QStringLiteral("/a.txt"), "bbb\n"); // gleiche Laenge

    FolderCompareSettings quick;
    quick.compareContent = false;
    const auto fast = FolderCompare::compareFolders(l, r, quick);
    QCOMPARE(childByName(fast, QStringLiteral("a.txt"))->status,
             FolderEntryStatus::Identical);

    const auto full = FolderCompare::compareFolders(l, r, {});
    QCOMPARE(childByName(full, QStringLiteral("a.txt"))->status,
             FolderEntryStatus::Different);
}

// A file containing a null byte counts as binary and is compared byte by
// byte. Ignore options must not change that.
void TstDiffEngine::folderBinaryComparedByBytes()
{
    QTemporaryDir tmp;
    const QString l = tmp.path() + QStringLiteral("/l");
    const QString r = tmp.path() + QStringLiteral("/r");
    const QByteArray binA("\x01\x00\x02 a", 6);
    const QByteArray binB("\x01\x00\x02 A", 6); // differs in letter case only
    writeFile(l + QStringLiteral("/blob.bin"), binA);
    writeFile(r + QStringLiteral("/blob.bin"), binB);

    FolderCompareSettings s;
    s.diffOptions.ignoreCase = true; // must not apply to binary data
    const auto root = FolderCompare::compareFolders(l, r, s);
    const auto *blob = childByName(root, QStringLiteral("blob.bin"));
    QVERIFY(blob);
    QVERIFY(blob->isBinary);
    QCOMPARE(blob->status, FolderEntryStatus::Different);

    // The same content on both sides stays identical.
    writeFile(r + QStringLiteral("/blob.bin"), binA);
    const auto same = FolderCompare::compareFolders(l, r, s);
    QCOMPARE(childByName(same, QStringLiteral("blob.bin"))->status,
             FolderEntryStatus::Identical);
}

// The same name, once a file and once a directory, counts as different.
void TstDiffEngine::folderTypeConflict()
{
    QTemporaryDir tmp;
    const QString l = tmp.path() + QStringLiteral("/l");
    const QString r = tmp.path() + QStringLiteral("/r");
    writeFile(l + QStringLiteral("/thing"), "file\n");
    writeFile(r + QStringLiteral("/thing/inner.txt"), "dir\n");

    const auto root = FolderCompare::compareFolders(l, r, {});
    const auto *thing = childByName(root, QStringLiteral("thing"));
    QVERIFY(thing);
    QCOMPARE(thing->status, FolderEntryStatus::Different);
    QVERIFY(FolderCompare::hasDifferences(root));
}

// A directory that exists on one side only is not descended into. It
// reports its side but carries no children.
void TstDiffEngine::folderOneSidedDirNotDescended()
{
    QTemporaryDir tmp;
    const QString l = tmp.path() + QStringLiteral("/l");
    const QString r = tmp.path() + QStringLiteral("/r");
    writeFile(l + QStringLiteral("/lonely/deep/a.txt"), "x\n");
    QDir().mkpath(r);

    const auto root = FolderCompare::compareFolders(l, r, {});
    const auto *lonely = childByName(root, QStringLiteral("lonely"));
    QVERIFY(lonely);
    QVERIFY(lonely->isDir);
    QCOMPARE(lonely->status, FolderEntryStatus::OnlyLeft);
    QVERIFY(lonely->children.isEmpty());
}

// A cancel flag that is already set ends the scan at once.
void TstDiffEngine::folderCancelStopsScan()
{
    QTemporaryDir tmp;
    const QString l = tmp.path() + QStringLiteral("/l");
    const QString r = tmp.path() + QStringLiteral("/r");
    writeFile(l + QStringLiteral("/a.txt"), "x\n");
    writeFile(r + QStringLiteral("/b.txt"), "y\n");

    std::atomic<bool> cancel(true);
    const auto root = FolderCompare::compareFolders(l, r, {}, &cancel);
    QVERIFY(root.children.isEmpty());

    // Counter-check without cancelling: the same tree yields entries.
    std::atomic<bool> run(false);
    const auto full = FolderCompare::compareFolders(l, r, {}, &run);
    QVERIFY(!full.children.isEmpty());
}

// Counter-case to trivialBlockMarked: a block whose lines still carry
// content after normalisation must not count as trivial.
//
// Blind spot, deliberately not covered here: markTrivialBlocks does not
// check the ignore options itself. That it is only called while an option
// is active is decided by the caller in the interface layer, which is not
// observable from this level.
void TstDiffEngine::trivialNotMarkedWhenLinesCarryContent()
{
    const QStringList l = {QStringLiteral("a"), QStringLiteral("b")};
    const QStringList r = {QStringLiteral("a"), QStringLiteral("neu"),
                           QStringLiteral("b")};

    DiffOptions opt;
    opt.ignoreComments = true; // active, yet the line still carries content
    const QStringList nl = LineNormalizer::normalize(l, opt, Language::None);
    const QStringList nr = LineNormalizer::normalize(r, opt, Language::None);
    auto blocks = DiffEngine::compare(nl, nr);
    DiffEngine::markTrivialBlocks(blocks, nl, nr);

    bool anyTrivial = false;
    for (const DiffBlock &b : blocks) {
        if (b.trivial)
            anyTrivial = true;
    }
    QVERIFY(!anyTrivial);
}


// Two large files whose differences exceed the memory guard of the engine.
// The comparison then stops and reports the remainder as one large change
// instead of exhausting memory.
//
// The inputs share every hundredth line on purpose. Without the guard the
// algorithm would find all of those and return roughly sixty blocks. With it,
// only the leading shared line survives as its own block and the rest becomes
// one. That difference is what makes this case fail if the guard ever stops
// working, and it is why the inputs are this large: a smaller pair never
// reaches the limit. The case costs about a second.
void TstDiffEngine::memoryGuardDegradesToOneBlock()
{
    QStringList l, r;
    for (int i = 0; i < 3000; ++i) {
        if (i % 100 == 0) {
            l << QStringLiteral("shared %1").arg(i);
            r << QStringLiteral("shared %1").arg(i);
        } else {
            l << QStringLiteral("left %1 aaaa").arg(i);
            r << QStringLiteral("right %1 bbbb").arg(i);
        }
    }

    const auto blocks = DiffEngine::compare(l, r);

    // The guard collapsed the remainder: far fewer blocks than the shared
    // lines would allow.
    QCOMPARE(blocks.size(), 2);
    QCOMPARE(blocks[0].type, BlockType::Equal);
    QCOMPARE(blocks[1].type, BlockType::Changed);

    // The coverage invariant holds for the degraded result as well.
    int leftCovered = 0, rightCovered = 0;
    for (const DiffBlock &b : blocks) {
        QCOMPARE(b.leftStart, leftCovered);
        QCOMPARE(b.rightStart, rightCovered);
        leftCovered += b.leftCount;
        rightCovered += b.rightCount;
    }
    QCOMPARE(leftCovered, l.size());
    QCOMPARE(rightCovered, r.size());
}

// Exclude patterns are wildcards, not plain names. The existing case covers
// an exact name, this one covers the wildcard form the default settings use.
void TstDiffEngine::folderExcludeWildcard()
{
    QTemporaryDir tmp;
    const QString l = tmp.path() + QStringLiteral("/l");
    const QString r = tmp.path() + QStringLiteral("/r");
    writeFile(l + QStringLiteral("/keep.txt"), "same\n");
    writeFile(r + QStringLiteral("/keep.txt"), "same\n");
    writeFile(l + QStringLiteral("/build.o"), "left\n");
    writeFile(r + QStringLiteral("/build.o"), "right\n");

    FolderCompareSettings s;
    s.excludePatterns = {QStringLiteral("*.o")};
    const auto root = FolderCompare::compareFolders(l, r, s);
    QCOMPARE(root.status, FolderEntryStatus::Identical);
    QVERIFY(!childByName(root, QStringLiteral("build.o")));
    QVERIFY(childByName(root, QStringLiteral("keep.txt")));

    // Counter-check: without the pattern the same tree differs.
    const auto without = FolderCompare::compareFolders(l, r, {});
    QCOMPARE(without.status, FolderEntryStatus::Different);
}

// Edge cases of the within-line difference: equal lines carry no range, and
// a line that shares nothing with its counterpart spans its whole length.
void TstDiffEngine::intralineEdgeCases()
{
    const IntralineSpan equal =
        IntralineDiff::compare(QStringLiteral("same"), QStringLiteral("same"));
    QVERIFY(!equal.valid);   // nothing differs, so there is no range

    const IntralineSpan added =
        IntralineDiff::compare(QString(), QStringLiteral("added"));
    QVERIFY(added.valid);
    QCOMPARE(added.leftLen, 0);
    QCOMPARE(added.rightStart, 0);
    QCOMPARE(added.rightLen, 5);

    // Different lengths on both sides, with a common prefix and suffix.
    const IntralineSpan both =
        IntralineDiff::compare(QStringLiteral("a-XXX-z"),
                               QStringLiteral("a-Y-z"));
    QVERIFY(both.valid);
    QCOMPARE(both.leftStart, 2);
    QCOMPARE(both.leftLen, 3);
    QCOMPARE(both.rightStart, 2);
    QCOMPARE(both.rightLen, 1);
}

QTEST_GUILESS_MAIN(TstDiffEngine)
#include "tst_diffengine.moc"
