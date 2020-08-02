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

#include "qbreakpad.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#ifdef Q_OS_WINDOWS
#include "windowsdllinterceptor.h"
#include <client/windows/handler/exception_handler.h>
#elif defined(Q_OS_LINUX)
#include <client/linux/handler/exception_handler.h>
#elif defined(Q_OS_MACOS)
#include <client/mac/handler/exception_handler.h>
#endif

namespace {

QScopedPointer<google_breakpad::ExceptionHandler> m_crashHandler;
QString m_reporterPath = {}, m_dumpDirPath = {}, m_logFilePath = {}, m_dumpFileArgument = {},
        m_logFileArgument = {}, m_dumpFileExtName = QString::fromUtf8(".dmp");
QStringList m_crashReporterArguments = {};
bool m_reportCrashesToSystem = false;

#ifdef Q_OS_WINDOWS
WindowsDllInterceptor m_kernel32Intercept;

using lpSetUnhandledExceptionFilterRedirection = LPTOP_LEVEL_EXCEPTION_FILTER(WINAPI *)(
    LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter);
lpSetUnhandledExceptionFilterRedirection m_pSetUnhandledExceptionFilterRedirection = nullptr;

bool m_blockUnhandledExceptionFilter = true;
LPTOP_LEVEL_EXCEPTION_FILTER m_previousUnhandledExceptionFilter = nullptr;

LPTOP_LEVEL_EXCEPTION_FILTER WINAPI
SetUnhandledExceptionFilterPatched(LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter)
{
    if (!m_blockUnhandledExceptionFilter) {
        // don't intercept
        return m_pSetUnhandledExceptionFilterRedirection(lpTopLevelExceptionFilter);
    }

    if (lpTopLevelExceptionFilter == m_previousUnhandledExceptionFilter) {
        // OK to swap back and forth between the previous filter
        m_previousUnhandledExceptionFilter = m_pSetUnhandledExceptionFilterRedirection(
            lpTopLevelExceptionFilter);
        return m_previousUnhandledExceptionFilter;
    }

    // intercept attempts to change the filter
    return nullptr;
}
#endif

#ifdef Q_OS_WINDOWS
bool DumpCallback(LPCWSTR _dump_dir,
                  LPCWSTR _minidump_id,
                  LPVOID context,
                  LPEXCEPTION_POINTERS exinfo,
                  MDRawAssertionInfo *assertion,
                  bool succeeded)
#elif defined(Q_OS_LINUX)
bool DumpCallback(const google_breakpad::MinidumpDescriptor &md, void *context, bool succeeded)
#elif defined(Q_OS_MACOS)
bool DumpCallback(const char *_dump_dir, const char *_minidump_id, void *context, bool succeeded)
#endif
{
    Q_UNUSED(context)
#ifdef Q_OS_WINDOWS
    Q_UNUSED(exinfo)
    Q_UNUSED(assertion)
#endif
    if (QFileInfo::exists(m_reporterPath)) {
#ifdef Q_OS_WINDOWS
        const QString dumpFilePath = QString::fromWCharArray(_dump_dir) + QDir::separator()
                                     + QString::fromWCharArray(_minidump_id) + m_dumpFileExtName;
#elif defined(Q_OS_LINUX)
        const QString dumpFilePath = QString::fromUtf8(md.path());
#elif defined(Q_OS_MACOS)
        const QString dumpFilePath = QString::fromUtf8(_dump_dir) + QDir::separator()
                                     + QString::fromUtf8(_minidump_id) + m_dumpFileExtName;
#endif
        if (!m_dumpFileArgument.isEmpty()) {
            m_crashReporterArguments.append(m_dumpFileArgument);
        }
        m_crashReporterArguments.append(QDir::toNativeSeparators(dumpFilePath));
        if (!m_logFileArgument.isEmpty() && !m_logFilePath.isEmpty()) {
            m_crashReporterArguments << m_logFileArgument
                                     << QDir::toNativeSeparators(m_logFilePath);
        }
        QProcess::startDetached(m_reporterPath, m_crashReporterArguments);
    }

    /*
    NO STACK USE, NO HEAP USE THERE !!!
    Creating QString's, using qDebug, etc. - everything is crash-unfriendly.
    */
    return m_reportCrashesToSystem ? succeeded : true;
}

#ifdef Q_OS_WINDOWS
VOID InvalidParameterHandlerFunc(
    LPCWSTR expression, LPCWSTR function, LPCWSTR file, UINT line, uintptr_t pReserved)
{
    Q_UNUSED(expression)
    Q_UNUSED(function)
    Q_UNUSED(file)
    Q_UNUSED(line)
    Q_UNUSED(pReserved)
    qDebug().noquote() << "InvalidParameterHandlerFunc";
    if (!google_breakpad::ExceptionHandler::WriteMinidump(m_dumpDirPath.toStdWString(),
                                                          DumpCallback,
                                                          nullptr)) {
        qWarning().noquote() << "Failed to write minidump.";
    }
    TerminateProcess(GetCurrentProcess(), EXIT_FAILURE);
}

VOID PurecallHandlerFunc()
{
    qDebug().noquote() << "PurecallHandlerFunc";
    if (!google_breakpad::ExceptionHandler::WriteMinidump(m_dumpDirPath.toStdWString(),
                                                          DumpCallback,
                                                          nullptr)) {
        qWarning().noquote() << "Failed to write minidump.";
    }
    TerminateProcess(GetCurrentProcess(), EXIT_FAILURE);
}
#endif

} // namespace

void qbreakpad_initCrashHandler(const QString &value)
{
    if (!m_crashHandler.isNull() || value.isEmpty()) {
        return;
    }
    const QDir dir(value);
    if (!dir.exists()) {
        dir.mkpath(QChar::fromLatin1('.'));
    }
    m_dumpDirPath = QDir::toNativeSeparators(dir.canonicalPath());
#ifdef Q_OS_WINDOWS
    m_crashHandler.reset(
        new google_breakpad::ExceptionHandler(m_dumpDirPath.toStdWString(),
                                              nullptr,
                                              DumpCallback,
                                              nullptr,
                                              google_breakpad::ExceptionHandler::HANDLER_ALL));
    m_crashHandler->set_handle_debug_exceptions(true);

    _set_invalid_parameter_handler(InvalidParameterHandlerFunc);
    _set_purecall_handler(PurecallHandlerFunc);

    _CrtSetReportMode(_CRT_ASSERT, 0);

    m_blockUnhandledExceptionFilter = true;
    m_kernel32Intercept.Init(L"Kernel32");
    if (!m_kernel32Intercept.AddHook("SetUnhandledExceptionFilter",
                                     reinterpret_cast<intptr_t>(SetUnhandledExceptionFilterPatched),
                                     reinterpret_cast<void **>(
                                         &m_pSetUnhandledExceptionFilterRedirection))) {
        qWarning().noquote()
            << "SetUnhandledExceptionFilter hook failed; crash reporter is vulnerable.";
    }
#elif defined(Q_OS_LINUX)
    const google_breakpad::MinidumpDescriptor md(m_dumpDirPath.toStdString());
    m_crashHandler.reset(
        new google_breakpad::ExceptionHandler(md, nullptr, DumpCallback, nullptr, true, -1));
#elif defined(Q_OS_MACOS)
    m_crashHandler.reset(new google_breakpad::ExceptionHandler(m_dumpDirPath.toStdString(),
                                                               nullptr,
                                                               DumpCallback,
                                                               nullptr,
                                                               true,
                                                               0));
#endif
}

bool qbreakpad_writeMiniDump()
{
#ifdef Q_OS_WINDOWS
    const std::wstring path = m_dumpDirPath.toStdWString();
#else
    const std::string path = m_dumpDirPath.toStdString();
#endif
    const bool ret = google_breakpad::ExceptionHandler::WriteMinidump(path, DumpCallback, nullptr);
    if (!ret) {
        qWarning().noquote() << "Failed to write minidump.";
    }
    return ret;
}

void qbreakpad_setReporterPath(const QString &value)
{
    if (value.isEmpty()) {
        return;
    }
    const QString path = QDir::toNativeSeparators(value);
    if (m_reporterPath == path) {
        return;
    }
    m_reporterPath = path;
}

void qbreakpad_setReporterDumpFileArgument(const QString &value)
{
    if (m_dumpFileArgument != value) {
        m_dumpFileArgument = value;
    }
}

void qbreakpad_setReporterLogFileArgument(const QString &value)
{
    if (m_logFileArgument != value) {
        m_logFileArgument = value;
    }
}

void qbreakpad_setReporterCommonArguments(const QStringList &value)
{
    if (m_crashReporterArguments != value) {
        m_crashReporterArguments = value;
    }
}

void qbreakpad_setLogFilePath(const QString &value)
{
    if (value.isEmpty()) {
        return;
    }
    const QString path = QDir::toNativeSeparators(value);
    if (m_logFilePath == path) {
        return;
    }
    m_logFilePath = path;
}

void qbreakpad_setDumpFileExtName(const QString &value)
{
    if (value.isEmpty()) {
        return;
    }
    QString extName = value.toLower();
    if (!extName.startsWith(QChar::fromLatin1('.'))) {
        extName.prepend(QChar::fromLatin1('.'));
    }
    if (m_dumpFileExtName != extName) {
        m_dumpFileExtName = extName;
    }
}
