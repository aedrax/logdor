#ifndef FORMATCATALOG_H
#define FORMATCATALOG_H

#include <logdor/FormatParser.h>

#include <QStringList>

#include <memory>

/**
 * All available parsers: builtins plus declarative specs from the bundled
 * formats directory (next to the binaries) and the user's data directory
 * (~/.local/share/logdor/formats on Linux — drop a .json there to add a
 * format without compiling; user specs override bundled ids). Invalid spec
 * files are skipped and reported via @p warnings. Value-returning by design.
 */
Q_DECL_EXPORT QList<std::shared_ptr<const logdor::FormatParser>>
loadAllParsers(QStringList* warnings = nullptr);

#endif // FORMATCATALOG_H
