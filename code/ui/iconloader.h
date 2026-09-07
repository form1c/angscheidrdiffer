#ifndef ICONLOADER_H
#define ICONLOADER_H

#include <QIcon>
#include <QString>

// Builds one action icon from the embedded resources
// (graphics/icons/<size>/<Name>_<size>.png) across all interface sizes.
inline QIcon actionIcon(const QString &baseName)
{
    QIcon icon;
    for (const int size : {16, 22, 24, 32, 48, 64}) {
        icon.addFile(QStringLiteral(":/actions/%1_%2.png")
                         .arg(baseName).arg(size),
                     QSize(size, size));
    }
    return icon;
}

#endif // ICONLOADER_H
