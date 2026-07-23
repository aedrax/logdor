#include "formatcatalog.h"

#include <logdor/DeclarativeParser.h>
#include <logdor/FormatRegistry.h>
#include <logdor/FormatSpec.h>

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

QList<std::shared_ptr<const logdor::FormatParser>>
loadAllParsers(QStringList* warnings)
{
    auto parsers = logdor::builtinParsers();

    // Bundled specs live in <builddir>/formats during development and in
    // share/logdor/formats when installed; user specs override bundled ids.
    const QStringList dirs = {
        QDir(QCoreApplication::applicationDirPath()).filePath("../formats"),
        QDir(QCoreApplication::applicationDirPath()).filePath("../share/logdor/formats"),
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/formats"),
    };

    QList<logdor::FormatSpecError> errors;
    const auto specs = logdor::loadFormatSpecs(dirs, &errors);
    for (const auto& error : errors) {
        if (warnings)
            warnings->append(QStringLiteral("%1: %2").arg(error.sourcePath,
                                                          error.message));
        qWarning() << "format spec" << error.sourcePath << ":" << error.message;
    }
    for (const auto& spec : specs)
        parsers.append(std::make_shared<const logdor::DeclarativeParser>(spec));
    return parsers;
}
