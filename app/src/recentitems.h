#ifndef RECENTITEMS_H
#define RECENTITEMS_H

#include <QStringList>

/**
 * Recent files/folders policy: MRU-first canonical absolute paths, files and
 * folders mixed in one list. Pure functions; MainWindow owns the QSettings
 * key ("recentItems") and the menu.
 */
Q_DECL_EXPORT QStringList updatedRecents(QStringList items, const QString& path,
                                         int cap = 10);

/// Drops entries that no longer exist on disk, preserving order.
Q_DECL_EXPORT QStringList prunedRecents(const QStringList& items);

#endif // RECENTITEMS_H
