#pragma once

#include "linenormalizer.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <atomic>

enum class FolderEntryStatus {
    Identical,
    Different,
    OnlyLeft,
    OnlyRight,
};

// One entry of the merged directory tree.
struct FolderDiffEntry {
    QString name;                 // file or directory name
    QString relPath;              // path relative to the comparison root
    bool    isDir    = false;
    bool    isBinary = false;
    FolderEntryStatus status = FolderEntryStatus::Identical;
    qint64    sizeLeft  = -1;     // -1 = not present on this side
    qint64    sizeRight = -1;
    QDateTime mtimeLeft;
    QDateTime mtimeRight;
    QList<FolderDiffEntry> children; // only for isDir, present on both sides
};

struct FolderCompareSettings {
    DiffOptions diffOptions;        // including ignoreFirstLines
    bool ignoreEol = false;         // treat CRLF and LF as equal
    QStringList excludePatterns;    // wildcards on names, e.g. ".svn", "*.o"
    bool compareContent = true;     // false = quick mode, file size only
};

namespace FolderCompare {

// Compares two directory trees recursively and returns the root entry
// (isDir=true, empty relPath). The optional cancelFlag aborts the scan,
// in which case the result is incomplete.
FolderDiffEntry compareFolders(const QString &leftDir, const QString &rightDir,
                               const FolderCompareSettings &settings,
                               const std::atomic<bool> *cancelFlag = nullptr);

// True when the tree contains any entry that is not identical.
bool hasDifferences(const FolderDiffEntry &root);

} // namespace FolderCompare
