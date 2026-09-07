#include "linenormalizer.h"

QStringList LineNormalizer::normalize(const QStringList &lines,
                                       const DiffOptions &options,
                                       Language language)
{
    QStringList result = lines;

    if (options.ignoreComments && language != Language::None)
        result = CommentStripper::stripComments(result, language);

    if (options.ignoreWhitespace) {
        for (QString &line : result)
            line = line.simplified();
    }

    if (options.ignoreCase) {
        for (QString &line : result)
            line = line.toLower();
    }

    // Neutralise the first N lines so that they always compare as equal.
    const int skip = qMin(options.ignoreFirstLines, int(result.size()));
    for (int i = 0; i < skip; ++i)
        result[i] = QString();

    return result;
}
