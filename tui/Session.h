#pragma once

#include <logdor/FileIdentity.h>
#include <logdor/FileSource.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

// The filter of each file, in the sessions.json file of the GUI. The TUI
// reads and writes the same "filter" object as MainWindow, so a file shows
// the same filter in the GUI and the TUI. The TUI keeps all the other data
// of the file (plugin states, time range terms) and of the other files.
//
// ponytail: a GUI that runs at the same time keeps its own copy and writes
// it on the next file switch, which removes the change of the TUI.
namespace session {

inline QString defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/sessions.json");
}

// The same key as MainWindow::sessionKey().
inline QString keyFor(const QString& fileName)
{
    const QString canonical = QFileInfo(fileName).canonicalFilePath();
    return canonical.isEmpty() ? fileName : canonical;
}

inline QJsonObject readRoot(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    return root.value(QStringLiteral("version")).toInt() == 1 ? root : QJsonObject();
}

// The saved filter of the file, or an empty object. A file with other
// content (rotated or replaced) gets an empty object, as in the GUI.
inline QJsonObject loadFilter(const QString& path, const logdor::FileSource& source)
{
    const QJsonObject entry = readRoot(path)
                                  .value(QStringLiteral("sessions"))
                                  .toObject()
                                  .value(keyFor(source.filePath()))
                                  .toObject();
    const QJsonObject identity = entry.value(QStringLiteral("identity")).toObject();
    logdor::FileIdentity saved;
    saved.size = quint64(identity.value(QStringLiteral("size")).toDouble());
    saved.prefixSha256 = identity.value(QStringLiteral("sha")).toString().toUtf8();
    saved.prefixLength = quint32(identity.value(QStringLiteral("prefixLength")).toInt());
    if (saved.isValid()
        && logdor::matchIdentity(saved, source) == logdor::IdentityMatch::Mismatch)
        return {};
    return entry.value(QStringLiteral("filter")).toObject();
}

inline void saveFilter(const QString& path, const logdor::FileSource& source,
                       const QJsonObject& filter)
{
    QJsonObject root = readRoot(path);
    QJsonObject sessions = root.value(QStringLiteral("sessions")).toObject();
    const QString key = keyFor(source.filePath());
    QJsonObject entry = sessions.value(key).toObject();
    const logdor::FileIdentity id = logdor::computeFileIdentity(source);
    entry.insert(QStringLiteral("filter"), filter);
    entry.insert(QStringLiteral("identity"),
                 QJsonObject{{QStringLiteral("size"), double(id.size)},
                             {QStringLiteral("sha"), QString::fromUtf8(id.prefixSha256)},
                             {QStringLiteral("prefixLength"), int(id.prefixLength)}});
    entry.insert(QStringLiteral("lastUsed"), double(QDateTime::currentMSecsSinceEpoch()));
    sessions.insert(key, entry);
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("sessions"), sessions);

    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.commit();
}

} // namespace session
