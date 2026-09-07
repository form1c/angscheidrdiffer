#include "intralinediff.h"

IntralineSpan IntralineDiff::compare(const QString &left, const QString &right)
{
    IntralineSpan span;
    if (left == right)
        return span;

    const int lLen = left.size();
    const int rLen = right.size();

    int prefix = 0;
    while (prefix < lLen && prefix < rLen && left[prefix] == right[prefix])
        ++prefix;

    int suffix = 0;
    while (suffix < lLen - prefix && suffix < rLen - prefix
           && left[lLen - 1 - suffix] == right[rLen - 1 - suffix])
        ++suffix;

    span.leftStart  = prefix;
    span.leftLen    = lLen - prefix - suffix;
    span.rightStart = prefix;
    span.rightLen   = rLen - prefix - suffix;
    span.valid      = true;
    return span;
}
