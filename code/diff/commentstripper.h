#pragma once

#include <QString>
#include <QStringList>

enum class Language {
    None,     // no comment handling
    CFamily,  // C, C++, Java, JavaScript, TypeScript: //, /* */
    Html,     // HTML/XML: <!-- -->
};

namespace CommentStripper {

// Determines the language from the file extension.
Language languageForFile(const QString &fileName);

// Removes comments from all lines (state machine across line boundaries).
// The line count stays the same. Comment text is replaced by an empty
// string so that the alignment of the comparison is preserved.
QStringList stripComments(const QStringList &lines, Language lang);

} // namespace CommentStripper
