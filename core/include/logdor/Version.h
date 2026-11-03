#pragma once

#include <QString>

namespace logdor {

// The Logdor version, from project() in the top-level CMakeLists.txt.
QString coreVersion();

// The text of Help > About in the GUI and in logdor-tui. With @p html, the
// download URL is a link and lines end with <br>.
QString aboutText(bool html = false);

} // namespace logdor
