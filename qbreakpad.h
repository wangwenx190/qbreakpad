/*
 * MIT License
 *
 * Copyright (C) 2020 by wangwenx190 (Yuhang Zhao)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include "qbreakpad_global.h"
#include <QStringList>

#ifdef __cplusplus
extern "C" {
#endif

QBREAKPAD_EXPORT void qbreakpad_initCrashHandler(const QString &value);
QBREAKPAD_EXPORT bool qbreakpad_writeMiniDump();
QBREAKPAD_EXPORT void qbreakpad_setReporterPath(const QString &value);
QBREAKPAD_EXPORT void qbreakpad_setReporterCommonArguments(const QStringList &value);
QBREAKPAD_EXPORT void qbreakpad_setReporterDumpFileArgument(const QString &value);
QBREAKPAD_EXPORT void qbreakpad_setReporterLogFileArgument(const QString &value);
QBREAKPAD_EXPORT void qbreakpad_setLogFilePath(const QString &value);
QBREAKPAD_EXPORT void qbreakpad_setDumpFileExtName(const QString &value);

#ifdef __cplusplus
}
#endif
