#pragma once

#include <QtCore/qglobal.h>

#ifndef QBREAKPAD_EXPORT
#ifdef QBREAKPAD_STATIC
#define QBREAKPAD_EXPORT
#else
#ifdef QBREAKPAD_BUILD_LIBRARY
#define QBREAKPAD_EXPORT Q_DECL_EXPORT
#else
#define QBREAKPAD_EXPORT Q_DECL_IMPORT
#endif
#endif
#endif
