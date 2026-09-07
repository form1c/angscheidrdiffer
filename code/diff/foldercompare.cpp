#include "foldercompare.h"
#include "commentstripper.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace {

bool isExcluded(const QString &name, const QList<QRegularExpression> &excludes)
{
    for (const QRegularExpression &re : excludes) {
        if (re.match(name).hasMatch())
            return true;
    }
    return false;
}

// Binary heuristic: a null byte within the first 8 KB.
bool looksBinary(const QByteArray &data)
{
    const int probe = qMin(data.size(), qsizetype(8192));
    return QByteArray::fromRawData(data.constData(), probe).contains('\0');
}

// Compares two text files using the ignore options.
// Returns true when they count as equal.
bool textEquals(const QByteArray &rawL, const QByteArray &rawR,
                const QString &fileName, const FolderCompareSettings &s)
{
    QString textL = QString::fromUtf8(rawL);
    QString textR = QString::fromUtf8(rawR);

    if (s.ignoreEol) {
        textL.replace(QLatin1String("\r\n"), QLatin1String("\n"));
        textL.replace(QLatin1Char('\r'), QLatin1Char('\n'));
        textR.replace(QLatin1String("\r\n"), QLatin1String("\n"));
        textR.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    }

    const QStringList linesL = textL.split(QLatin1Char('\n'));
    const QStringList linesR = textR.split(QLatin1Char('\n'));

    const Language lang = CommentStripper::languageForFile(fileName);
    return LineNormalizer::normalize(linesL, s.diffOptions, lang)
           == LineNormalizer::normalize(linesR, s.diffOptions, lang);
}

// Compares a pair of files that exists on both sides.
FolderEntryStatus compareFilePair(const QString &leftPath, const QString &rightPath,
                                  const QString &fileName,
                                  const FolderCompareSettings &s,
                                  bool *isBinary)
{
    const qint64 sizeL = QFileInfo(leftPath).size();
    const qint64 sizeR = QFileInfo(rightPath).size();

    // Quick mode: the file size alone decides.
    if (!s.compareContent) {
        return sizeL == sizeR ? FolderEntryStatus::Identical
                              : FolderEntryStatus::Different;
    }

    QFile fl(leftPath), fr(rightPath);
    if (!fl.open(QIODevice::ReadOnly) || !fr.open(QIODevice::ReadOnly))
        return FolderEntryStatus::Different; // unreadable, report as different

    const QByteArray rawL = fl.readAll();
    const QByteArray rawR = fr.readAll();

    if (looksBinary(rawL) || looksBinary(rawR)) {
        if (isBinary) *isBinary = true;
        return rawL == rawR ? FolderEntryStatus::Identical
                            : FolderEntryStatus::Different;
    }

    // Without an active ignore option a byte comparison is enough.
    const DiffOptions &o = s.diffOptions;
    const bool anyIgnore = o.ignoreWhitespace || o.ignoreComments
                           || o.ignoreCase || o.ignoreFirstLines > 0
                           || s.ignoreEol;
    if (!anyIgnore)
        return rawL == rawR ? FolderEntryStatus::Identical
                            : FolderEntryStatus::Different;

    return textEquals(rawL, rawR, fileName, s)
        ? FolderEntryStatus::Identical
        : FolderEntryStatus::Different;
}

FolderDiffEntry compareDir(const QString &leftDir, const QString &rightDir,
                           const QString &relPath,
                           const FolderCompareSettings &s,
                           const QList<QRegularExpression> &excludes,
                           const std::atomic<bool> *cancelFlag)
{
    FolderDiffEntry entry;
    entry.name    = QFileInfo(relPath.isEmpty() ? leftDir : relPath).fileName();
    entry.relPath = relPath;
    entry.isDir   = true;

    if (cancelFlag && cancelFlag->load())
        return entry;

    const QDir::Filters filters =
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden;

    // Merge the names of both sides, sorted, directories first.
    QSet<QString> nameSet;
    const QStringList leftNames  = QDir(leftDir).entryList(filters);
    const QStringList rightNames = QDir(rightDir).entryList(filters);
    for (const QString &n : leftNames)  nameSet.insert(n);
    for (const QString &n : rightNames) nameSet.insert(n);

    QStringList names = nameSet.values();
    std::sort(names.begin(), names.end(),
              [&](const QString &a, const QString &b) {
                  const bool dirA = QFileInfo(QDir(leftDir).filePath(a)).isDir()
                                    || QFileInfo(QDir(rightDir).filePath(a)).isDir();
                  const bool dirB = QFileInfo(QDir(leftDir).filePath(b)).isDir()
                                    || QFileInfo(QDir(rightDir).filePath(b)).isDir();
                  if (dirA != dirB)
                      return dirA; // directories before files
                  return a.localeAwareCompare(b) < 0;
              });

    bool anyChildDiff = false;

    for (const QString &name : std::as_const(names)) {
        if (cancelFlag && cancelFlag->load())
            break;
        if (isExcluded(name, excludes))
            continue;

        const QString lPath = QDir(leftDir).filePath(name);
        const QString rPath = QDir(rightDir).filePath(name);
        const QFileInfo lInfo(lPath);
        const QFileInfo rInfo(rPath);
        const bool onLeft  = lInfo.exists() || lInfo.isSymLink();
        const bool onRight = rInfo.exists() || rInfo.isSymLink();
        const QString childRel =
            relPath.isEmpty() ? name : relPath + QLatin1Char('/') + name;

        // Symbolic links are not followed, which rules out cycles. They are
        const bool lIsDir = onLeft && lInfo.isDir() && !lInfo.isSymLink();
        const bool rIsDir = onRight && rInfo.isDir() && !rInfo.isSymLink();

        FolderDiffEntry child;

        if (lIsDir && rIsDir) {
            child = compareDir(lPath, rPath, childRel, s, excludes, cancelFlag);
        } else if (lIsDir != rIsDir && onLeft && onRight) {
            // Type conflict, a directory on one side and a file on the other.
            child.name    = name;
            child.relPath = childRel;
            child.isDir   = false;
            child.status  = FolderEntryStatus::Different;
        } else {
            child.name    = name;
            child.relPath = childRel;
            child.isDir   = lIsDir || rIsDir;
            if (onLeft && onRight) {
                child.status = compareFilePair(lPath, rPath, name, s,
                                               &child.isBinary);
            } else {
                child.status = onLeft ? FolderEntryStatus::OnlyLeft
                                      : FolderEntryStatus::OnlyRight;
            }
        }

        if (onLeft) {
            child.sizeLeft  = lIsDir ? 0 : lInfo.size();
            child.mtimeLeft = lInfo.lastModified();
        }
        if (onRight) {
            child.sizeRight  = rIsDir ? 0 : rInfo.size();
            child.mtimeRight = rInfo.lastModified();
        }

        if (child.status != FolderEntryStatus::Identical)
            anyChildDiff = true;
        entry.children.append(child);
    }

    entry.status = anyChildDiff ? FolderEntryStatus::Different
                                : FolderEntryStatus::Identical;
    return entry;
}

} // namespace

FolderDiffEntry FolderCompare::compareFolders(const QString &leftDir,
                                              const QString &rightDir,
                                              const FolderCompareSettings &settings,
                                              const std::atomic<bool> *cancelFlag)
{
    QList<QRegularExpression> excludes;
    for (const QString &pattern : settings.excludePatterns) {
        const QString p = pattern.trimmed();
        if (!p.isEmpty())
            excludes << QRegularExpression::fromWildcard(p, Qt::CaseSensitive);
    }
    return compareDir(QDir(leftDir).absolutePath(),
                      QDir(rightDir).absolutePath(),
                      QString(), settings, excludes, cancelFlag);
}

bool FolderCompare::hasDifferences(const FolderDiffEntry &root)
{
    if (root.status != FolderEntryStatus::Identical && !root.isDir)
        return true;
    if (root.isDir && root.status != FolderEntryStatus::Identical
        && root.children.isEmpty())
        return true; // directory present on one side only
    for (const FolderDiffEntry &c : root.children) {
        if (c.isDir) {
            if (hasDifferences(c))
                return true;
        } else if (c.status != FolderEntryStatus::Identical) {
            return true;
        }
    }
    return false;
}
