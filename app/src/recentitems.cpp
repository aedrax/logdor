#include "recentitems.h"

#include <QFileInfo>

QStringList updatedRecents(QStringList items, const QString& path, int cap)
{
    // Canonicalize so "dir/../file" and a symlinked spelling collapse into
    // one entry; fall back to the absolute path for files that vanished.
    const QFileInfo info(path);
    QString canonical = info.canonicalFilePath();
    if (canonical.isEmpty())
        canonical = info.absoluteFilePath();

    items.removeAll(canonical);
    items.prepend(canonical);
    while (items.size() > cap)
        items.removeLast();
    return items;
}

QStringList prunedRecents(const QStringList& items)
{
    QStringList kept;
    for (const QString& item : items) {
        if (QFileInfo::exists(item))
            kept.append(item);
    }
    return kept;
}
