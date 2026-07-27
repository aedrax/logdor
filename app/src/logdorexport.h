#ifndef LOGDOREXPORT_H
#define LOGDOREXPORT_H

#include <QtCore/qglobal.h>

// Symbols must be dllexport while building logdor_interface and dllimport
// when the app or a plugin compiles against it; unconditional Q_DECL_EXPORT
// breaks the MSVC link (LNK2019 on staticMetaObject and friends).
#if defined(LOGDOR_INTERFACE_LIBRARY)
#define LOGDOR_INTERFACE_EXPORT Q_DECL_EXPORT
#else
#define LOGDOR_INTERFACE_EXPORT Q_DECL_IMPORT
#endif

#endif // LOGDOREXPORT_H
