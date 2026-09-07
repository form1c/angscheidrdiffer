#pragma once

#include "commentstripper.h"

#include <QStringList>

// Options that decide when two lines count as equal.
// The display always shows the original lines. Only the comparison runs
// on the normalised versions.
struct DiffOptions {
    bool ignoreWhitespace = false;  // leading, trailing and repeated spaces
    bool ignoreComments   = false;  // Sprach-Kommentare (C-Familie, HTML)
    bool ignoreCase       = false;  // letter case
    // Exclude the first N lines of each file from the comparison, 0 = off.
    // Intended for headers that always differ, such as $Id$ keywords.
    int  ignoreFirstLines = 0;
};

namespace LineNormalizer {

// Returns the comparison version of the lines, same count as the input.
QStringList normalize(const QStringList &lines,
                      const DiffOptions &options,
                      Language language);

} // namespace LineNormalizer
