#include "logdor/Version.h"

namespace logdor {

QString coreVersion()
{
    return QStringLiteral(LOGDOR_VERSION);
}

QString aboutText(bool html)
{
    const QString url = QStringLiteral("https://github.com/aedrax/logdor/releases");
    const QString link = html ? QStringLiteral("<a href=\"%1\">%1</a>").arg(url) : url;
    const QString br = html ? QStringLiteral("<br>") : QStringLiteral("\n");
    return QStringLiteral("Logdor %1%2%2Made by Paul Sorensen.%2%2Download: %3")
        .arg(coreVersion(), br, link);
}

} // namespace logdor
