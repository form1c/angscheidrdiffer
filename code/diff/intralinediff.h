#pragma once

#include <QString>

// Character-level difference between two lines: the range that differs
// once the common prefix and suffix have been removed.
struct IntralineSpan {
    int leftStart  = 0;
    int leftLen    = 0;
    int rightStart = 0;
    int rightLen   = 0;
    bool valid     = false;
};

namespace IntralineDiff {

// Determines the changed character range of both lines using the
// prefix and suffix method, O(n).
IntralineSpan compare(const QString &left, const QString &right);

} // namespace IntralineDiff
