#include "commentstripper.h"

#include <QFileInfo>

namespace {

QStringList stripCFamily(const QStringList &lines)
{
    QStringList out;
    out.reserve(lines.size());

    bool inBlockComment = false;

    for (const QString &line : lines) {
        QString result;
        result.reserve(line.size());

        int i = 0;
        const int len = line.size();
        QChar stringDelim; // active string literal: ", ' or `

        while (i < len) {
            const QChar c = line[i];
            const QChar next = (i + 1 < len) ? line[i + 1] : QChar();

            if (inBlockComment) {
                if (c == QLatin1Char('*') && next == QLatin1Char('/')) {
                    inBlockComment = false;
                    i += 2;
                } else {
                    ++i;
                }
                continue;
            }

            if (!stringDelim.isNull()) {
                result += c;
                if (c == QLatin1Char('\\') && i + 1 < len) {
                    result += next;
                    i += 2;
                    continue;
                }
                if (c == stringDelim)
                    stringDelim = QChar();
                ++i;
                continue;
            }

            if (c == QLatin1Char('"') || c == QLatin1Char('\'')
                || c == QLatin1Char('`')) {
                stringDelim = c;
                result += c;
                ++i;
                continue;
            }

            if (c == QLatin1Char('/') && next == QLatin1Char('/'))
                break; // the rest of the line is a comment

            if (c == QLatin1Char('/') && next == QLatin1Char('*')) {
                inBlockComment = true;
                i += 2;
                continue;
            }

            result += c;
            ++i;
        }

        out << result;
    }

    return out;
}

QStringList stripHtml(const QStringList &lines)
{
    QStringList out;
    out.reserve(lines.size());

    bool inComment = false;

    for (const QString &line : lines) {
        QString result;
        result.reserve(line.size());

        int i = 0;
        const int len = line.size();

        while (i < len) {
            if (inComment) {
                const int end = line.indexOf(QLatin1String("-->"), i);
                if (end < 0) {
                    i = len;
                } else {
                    inComment = false;
                    i = end + 3;
                }
                continue;
            }
            const int start = line.indexOf(QLatin1String("<!--"), i);
            if (start < 0) {
                result += line.mid(i);
                break;
            }
            result += line.mid(i, start - i);
            inComment = true;
            i = start + 4;
        }

        out << result;
    }

    return out;
}

} // namespace

Language CommentStripper::languageForFile(const QString &fileName)
{
    const QString suffix = QFileInfo(fileName).suffix().toLower();

    static const QStringList cFamily = {
        QStringLiteral("c"),   QStringLiteral("h"),
        QStringLiteral("cpp"), QStringLiteral("hpp"),
        QStringLiteral("cc"),  QStringLiteral("hh"),
        QStringLiteral("cxx"), QStringLiteral("hxx"),
        QStringLiteral("java"),
        QStringLiteral("js"),  QStringLiteral("mjs"), QStringLiteral("cjs"),
        QStringLiteral("jsx"),
        QStringLiteral("ts"),  QStringLiteral("tsx"),
    };
    static const QStringList html = {
        QStringLiteral("html"), QStringLiteral("htm"),
        QStringLiteral("xhtml"), QStringLiteral("xml"),
    };

    if (cFamily.contains(suffix)) return Language::CFamily;
    if (html.contains(suffix))    return Language::Html;
    return Language::None;
}

QStringList CommentStripper::stripComments(const QStringList &lines, Language lang)
{
    switch (lang) {
    case Language::CFamily: return stripCFamily(lines);
    case Language::Html:    return stripHtml(lines);
    case Language::None:    break;
    }
    return lines;
}
