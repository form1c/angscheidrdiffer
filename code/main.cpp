#include "ui/appwindow.h"

#include "diff/diffengine.h"
#include "diff/foldercompare.h"
#include "diff/merge3engine.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>

#include <cstdio>

namespace {

// Reads a file as a list of lines, normalised to LF. hadCrLf reports whether
QStringList readLinesFromFile(const QString &path, bool *ok,
                              bool *hadCrLf = nullptr)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (ok) *ok = false;
        return {};
    }
    if (ok) *ok = true;
    QString text = QString::fromUtf8(f.readAll());
    if (hadCrLf) *hadCrLf = text.contains(QLatin1String("\r\n"));
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return text.split(QLatin1Char('\n'));
}

// --batch --merge: three-way merge without a window.
// Exit codes: 0 = merged without conflicts and written,
// 1 = conflicts, nothing written, 2 = error.
int runBatchMerge(const QString &basePath, const QString &minePath,
                  const QString &theirsPath, const QString &outPath)
{
    bool okB = false, okM = false, okT = false, mineCrLf = false;
    const QStringList base   = readLinesFromFile(basePath, &okB);
    const QStringList mine   = readLinesFromFile(minePath, &okM, &mineCrLf);
    const QStringList theirs = readLinesFromFile(theirsPath, &okT);
    if (!okB || !okM || !okT) {
        std::fprintf(stderr, "Cannot read input files.\n");
        return 2;
    }

    const auto chunks = Merge3Engine::compare(base, mine, theirs);
    const Merge3Output out =
        Merge3Engine::buildResult(chunks, base, mine, theirs);
    if (out.unresolvedConflicts > 0) {
        std::fprintf(stderr, "%d unresolved conflict(s) — nothing written.\n",
                     out.unresolvedConflicts);
        return 1;
    }

    // Keep the line endings of the working copy, meaning mine.
    const QString joined = out.lines.join(
        mineCrLf ? QStringLiteral("\r\n") : QStringLiteral("\n"));
    if (outPath.isEmpty()) {
        std::fputs(qPrintable(joined), stdout);
        return 0;
    }
    QSaveFile file(outPath);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(joined.toUtf8()) < 0 || !file.commit()) {
        std::fprintf(stderr, "Cannot write output file: %s\n",
                     qPrintable(outPath));
        return 2;
    }
    return 0;
}

// --batch: compare files without a window.
// Exit codes as in diff: 0 = identical, 1 = different, 2 = error.
int runBatch(const QString &leftPath, const QString &rightPath)
{
    const auto readLines = [](const QString &path, bool *ok) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            *ok = false;
            return QStringList();
        }
        *ok = true;
        QString text = QString::fromUtf8(f.readAll());
        text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
        text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
        return text.split(QLatin1Char('\n'));
    };

    bool okL = false, okR = false;
    const QStringList left  = readLines(leftPath, &okL);
    const QStringList right = readLines(rightPath, &okR);
    if (!okL || !okR)
        return 2;

    const QList<DiffBlock> blocks = DiffEngine::compare(left, right);
    for (const DiffBlock &b : blocks) {
        if (b.type != BlockType::Equal)
            return 1;
    }
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("AnGscheidrDiffer"));
    QApplication::setApplicationName(QStringLiteral("AnGscheidrDiffer"));
    // APP_VERSION comes from the project() line in CMakeLists.txt. The
    // version is kept there and nowhere else.
#ifdef APP_VERSION
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION));
#else
    QApplication::setApplicationVersion(QStringLiteral("0.0.0"));
#endif

    // The window icon always comes from the embedded resources, never from
    // the icon theme, where an outdated installed icon would win.
    QIcon appIcon;
    for (const int size : {16, 22, 24, 32, 48, 64, 128, 256}) {
        appIcon.addFile(QStringLiteral(":/icons/diff_%1.png").arg(size),
                        QSize(size, size));
    }
    QApplication::setWindowIcon(appIcon);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Side-by-side diff and merge tool"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("left"),
                                 QStringLiteral("Left file or folder"));
    parser.addPositionalArgument(QStringLiteral("right"),
                                 QStringLiteral("Right file or folder"));

    const QCommandLineOption batchOption(
        QStringLiteral("batch"),
        QStringLiteral("Compare without GUI; exit code 0 = identical, "
                       "1 = different, 2 = error."));
    const QCommandLineOption readOnlyOption(
        QStringLiteral("readonly"),
        QStringLiteral("Open in read-only mode (no editing, merging or saving)."));
    const QCommandLineOption selectOption(
        QStringLiteral("select-for-compare"),
        QStringLiteral("Remember the given file for a later comparison "
                       "(no window)."));
    const QCommandLineOption compareWithOption(
        QStringLiteral("compare-with"),
        QStringLiteral("Compare the previously selected file (left) "
                       "with the given file (right)."));
    const QCommandLineOption mergeOption(
        QStringLiteral("merge"),
        QStringLiteral("Three-way merge; positional arguments are "
                       "<base> <mine> <theirs>."));
    const QCommandLineOption outputOption(
        {QStringLiteral("o"), QStringLiteral("output")},
        QStringLiteral("Output file for --merge."),
        QStringLiteral("file"));
    parser.addOption(batchOption);
    parser.addOption(readOnlyOption);
    parser.addOption(selectOption);
    parser.addOption(compareWithOption);
    parser.addOption(mergeOption);
    parser.addOption(outputOption);
    parser.process(app);

    const QStringList args = parser.positionalArguments();

    // --- 3-Wege-Merge: <base> <mine> <theirs> ---
    if (parser.isSet(mergeOption)) {
        if (args.size() != 3) {
            std::fprintf(stderr,
                         "--merge needs exactly three files: "
                         "<base> <mine> <theirs>\n");
            return 2;
        }
        if (parser.isSet(batchOption)) {
            return runBatchMerge(args.at(0), args.at(1), args.at(2),
                                 parser.value(outputOption));
        }
        AppWindow window;
        window.show();
        window.addMergeTab(args.at(0), args.at(1), args.at(2),
                           parser.value(outputOption));
        // Exit code 0 means saved and free of conflicts. A calling version
        // control client can use that to decide whether to mark the conflict
        // as resolved afterwards.
        const int rc = app.exec();
        if (rc != 0)
            return rc;
        return window.hasAnyDifferences() ? 1 : 0;
    }

    const bool leftIsDir  = args.size() >= 1 && QFileInfo(args.at(0)).isDir();
    const bool rightIsDir = args.size() >= 2 && QFileInfo(args.at(1)).isDir();

    if (args.size() >= 2 && leftIsDir != rightIsDir) {
        std::fprintf(stderr,
                     "Cannot compare a folder with a file: %s vs %s\n",
                     qPrintable(args.at(0)), qPrintable(args.at(1)));
        return 2;
    }

    // --- Batch mode: no window, exit code as in diff ---
    if (parser.isSet(batchOption)) {
        if (args.size() < 2)
            return 2;
        if (leftIsDir && rightIsDir) {
            const FolderDiffEntry root = FolderCompare::compareFolders(
                args.at(0), args.at(1), FolderCompareSettings{});
            return FolderCompare::hasDifferences(root) ? 1 : 0;
        }
        return runBatch(args.at(0), args.at(1));
    }

    // --- Service menu: "Select for Compare" remembers a path, no window ---
    if (parser.isSet(selectOption)) {
        if (args.isEmpty())
            return 2;
        QSettings settings;
        settings.setValue(QStringLiteral("pendingCompare"), args.first());
        return 0;
    }

    AppWindow window;
    window.show();

    if (parser.isSet(compareWithOption)) {
        // Service menu: "Compare with Selected", for a file or a directory.
        if (args.isEmpty())
            return 2;
        QSettings settings;
        const QString pending =
            settings.value(QStringLiteral("pendingCompare")).toString();
        settings.remove(QStringLiteral("pendingCompare"));
        const bool bothDirs = QFileInfo(pending).isDir()
                              && QFileInfo(args.first()).isDir();
        if (bothDirs)
            window.addFolderTab(pending, args.first());
        else
            window.addFileTab(pending, args.first());
    } else if (leftIsDir && rightIsDir) {
        window.addFolderTab(args.at(0), args.at(1));
    } else {
        window.addFileTab(args.value(0), args.value(1));
    }

    if (parser.isSet(readOnlyOption))
        window.setReadOnly(true);

    // Exit code of the window: 0 = identical, 1 = differences, for scripts.
    const int rc = app.exec();
    if (rc != 0)
        return rc;
    return window.hasAnyDifferences() ? 1 : 0;
}
