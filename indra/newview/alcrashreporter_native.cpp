/**
 * @file alcrashreporter_native.cpp
 * @brief sentry-native with the crashpad backend, on Windows and Linux
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "alcrashreporter.h"

#if LL_WINDOWS
#include "llwin32headers.h"
#include "llappviewerwin32.h"
#include <dbghelp.h>
#endif
#include <sentry.h>

#include "llagent.h"
#include "llappviewer.h"
#include "lldir.h"
#include "llerrorcontrol.h"
#include "llmemory.h"
#include "llstartup.h"
#include "llstring.h"
#include "llsys.h"
#include "lluuid.h"
#include "llversioninfo.h"
#include "llviewerregion.h"

namespace
{
    bool sEngaged = false;

#if LL_WINDOWS
    constexpr const char* HANDLER_NAME = "crashpad_handler.exe";

    void set_handler_path(sentry_options_t* options, const std::string& path)
    {
        sentry_options_set_handler_pathw(options, ll_convert<std::wstring>(path).c_str());
    }

    void set_database_path(sentry_options_t* options, const std::string& path)
    {
        sentry_options_set_database_pathw(options, ll_convert<std::wstring>(path).c_str());
    }

    void add_attachment(sentry_options_t* options, const std::string& path)
    {
        sentry_options_add_attachmentw(options, ll_convert<std::wstring>(path).c_str());
    }

    void attach_now(const std::string& path)
    {
        sentry_attach_filew(ll_convert<std::wstring>(path).c_str());
    }
#else
    constexpr const char* HANDLER_NAME = "crashpad_handler";

    void set_handler_path(sentry_options_t* options, const std::string& path)
    {
        sentry_options_set_handler_path(options, path.c_str());
    }

    void set_database_path(sentry_options_t* options, const std::string& path)
    {
        sentry_options_set_database_path(options, path.c_str());
    }

    void add_attachment(sentry_options_t* options, const std::string& path)
    {
        sentry_options_add_attachment(options, path.c_str());
    }

    void attach_now(const std::string& path)
    {
        sentry_attach_file(path.c_str());
    }
#endif

    // Windows Error Reporting is how the handler receives fast-fail crashes;
    // without a reporter engaged, the viewer stays excluded from it.
    void follow_wer(bool engaged)
    {
#if LL_WINDOWS
        LLAppViewerWin32::setWinErrorReportingExcluded(!engaged);
#endif
    }

    void set_event_tag(sentry_value_t event, const char* key, const std::string& value)
    {
        sentry_value_t tags = sentry_value_get_by_key(event, "tags");
        if (sentry_value_is_null(tags))
        {
            tags = sentry_value_new_object();
            sentry_value_set_by_key(event, "tags", tags);
        }
        sentry_value_set_by_key(tags, key, sentry_value_new_string(value.c_str()));
    }

    // Runs in the crashing process under crashpad, once the scope is on the
    // event: what is only known now goes on the event itself, and the marker
    // the next launch reads is written here.
    sentry_value_t on_crash(const sentry_ucontext_t*, sentry_value_t event, void*)
    {
        LLAppViewer* app = LLAppViewer::instance();

        if (LLViewerRegion* region = gAgent.getRegion())
        {
            set_event_tag(event, "location",
                          ALCrashReporter::locationTag(region->getName(), gAgent.getPositionAgent()));
        }

        std::string watchdog = app->getMainloopWatchdogState();
        if (!watchdog.empty())
        {
            set_event_tag(event, "watchdog_state", watchdog);
        }

        const U32 available_kb = LLMemory::getAvailableMemKB().value();
        if (available_kb != U32_MAX)
        {
            set_event_tag(event, "mem_allocated_kb", std::to_string(LLMemory::getAllocatedMemKB().value()));
            set_event_tag(event, "mem_available_kb", std::to_string(available_kb));
            set_event_tag(event, "mem_max_physical_kb", std::to_string(LLMemory::getMaxMemKB().value()));
            set_event_tag(event, "mem_available_commit_mb", std::to_string(LLMemory::getAvailableCommitMemMB().value()));
        }

        if (!app->isSecondInstance() && !app->errorMarkerExists())
        {
            app->createErrorMarker(app->logoutRequestSent() ? LAST_EXEC_LOGOUT_CRASH : LAST_EXEC_OTHER_CRASH);
        }

        return event;
    }

    bool engage()
    {
        LLAppViewer* app = LLAppViewer::instance();
        const LLVersionInfo& version = LLVersionInfo::instance();

        sentry_options_t* options = sentry_options_new();
        sentry_options_set_dsn(options, AL_SENTRY_DSN);
        sentry_options_set_release(options, ALCrashReporter::releaseName(version.getMajor(), version.getMinor(),
                                                                         version.getPatch(), version.getBuild()).c_str());
        sentry_options_set_environment(options, version.getChannel().c_str());
        sentry_options_set_dist(options, std::to_string(version.getBuild()).c_str());
        set_handler_path(options, gDirUtilp->getExpandedFilename(LL_PATH_EXECUTABLE, HANDLER_NAME));
        set_database_path(options, gDirUtilp->getExpandedFilename(LL_PATH_LOGS, "sentry"));
        add_attachment(options, LLError::logFileName());
        add_attachment(options, *app->getStaticDebugFile());
        add_attachment(options, gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, "settings.xml"));
        sentry_options_set_on_crash(options, on_crash, nullptr);

        if (sentry_init(options) != 0)
        {
            LL_WARNS("CrashReporter") << "Sentry did not start; crashes go unreported" << LL_ENDL;
            follow_wer(false);
            return false;
        }
        sEngaged = true;
        follow_wer(true);

        sentry_set_tag("run_id", ALCrashReporter::runId().c_str());
        sentry_set_tag("os", LLOSInfo::instance().getOSStringSimple().c_str());
        sentry_set_tag("second_instance", app->isSecondInstance() ? "true" : "false");
        sentry_set_tag("app_state", LLStartUp::getStartupStateString().c_str());

        LL_INFOS("CrashReporter") << "Sentry engaged for " << version.getChannelAndVersion() << LL_ENDL;
        return true;
    }

    void close_sdk()
    {
        if (sEngaged)
        {
            sentry_close();
            sEngaged = false;
        }
    }
}

bool ALCrashReporter::init()
{
#if !LL_SEND_CRASH_REPORTS
    return false;
#else
    if (!consentRecorded(consentSentinel()))
    {
        LL_INFOS("CrashReporter") << "No consent to crash reports recorded; none are sent until it is" << LL_ENDL;
        follow_wer(false);
        return false;
    }
    return engage();
#endif
}

void ALCrashReporter::refreshConsent(bool allowed)
{
    recordConsent(consentSentinel(), allowed);
#if LL_SEND_CRASH_REPORTS
    if (allowed && !sEngaged)
    {
        engage();
    }
    else if (!allowed && sEngaged)
    {
        close_sdk();
        follow_wer(false);
        LL_INFOS("CrashReporter") << "Consent to crash reports withdrawn; Sentry closed" << LL_ENDL;
    }
#endif
}

void ALCrashReporter::shutdown()
{
    close_sdk();
}

bool ALCrashReporter::isEngaged()
{
    return sEngaged;
}

void ALCrashReporter::setUser(const LLUUID& id, const std::string& name)
{
    if (!sEngaged)
    {
        return;
    }
    sentry_value_t user = sentry_value_new_object();
    sentry_value_set_by_key(user, "id", sentry_value_new_string(id.asString().c_str()));
    sentry_value_set_by_key(user, "username", sentry_value_new_string(name.c_str()));
    sentry_set_user(user);
}

void ALCrashReporter::setTag(std::string_view key, std::string_view value)
{
    if (sEngaged)
    {
        sentry_set_tag_n(key.data(), key.size(), value.data(), value.size());
    }
}

void ALCrashReporter::setContext(std::string_view name,
                                 std::initializer_list<std::pair<std::string_view, std::string_view>> values)
{
    if (!sEngaged)
    {
        return;
    }
    sentry_value_t context = sentry_value_new_object();
    for (const auto& [key, value] : values)
    {
        sentry_value_set_by_key_n(context, key.data(), key.size(),
                                  sentry_value_new_string_n(value.data(), value.size()));
    }
    sentry_set_context_n(name.data(), name.size(), context);
}

void ALCrashReporter::attach(const std::string& path)
{
    if (sEngaged)
    {
        attach_now(path);
    }
}

namespace
{
#if LL_WINDOWS
    // The frozen thread is the one worth reading, and it is not this one: a
    // minidump of the whole process, taken from the watchdog's thread, is
    // filed as the event, so every thread's stack is there.
    bool report_freeze_as_minidump(const std::string& description)
    {
        const std::string path = gDirUtilp->getExpandedFilename(LL_PATH_LOGS, "freeze.dmp");
        const std::wstring wide_path = ll_convert<std::wstring>(path);

        HANDLE file = CreateFileW(wide_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return false;
        }
        const MINIDUMP_TYPE type = static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo);
        const BOOL written = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type,
                                               nullptr, nullptr, nullptr);
        CloseHandle(file);
        if (!written)
        {
            LLFile::remove(path);
            return false;
        }

        sentry_value_t watchdog = sentry_value_new_object();
        sentry_value_set_by_key(watchdog, "description", sentry_value_new_string(description.c_str()));
        sentry_value_set_by_key(watchdog, "state",
                                sentry_value_new_string(LLAppViewer::instance()->getMainloopWatchdogState().c_str()));
        sentry_set_context("watchdog", watchdog);
        sentry_set_tag("freeze", "true");

        // The dump is read into the report as it is captured.
        const sentry_uuid_t id = sentry_capture_minidumpw(wide_path.c_str());

        sentry_remove_tag("freeze");
        sentry_remove_context("watchdog");
        LLFile::remove(path);
        return !sentry_uuid_is_nil(&id);
    }
#endif
}

bool ALCrashReporter::reportFreeze(const std::string& description)
{
    if (!sEngaged)
    {
        return false;
    }
#if LL_WINDOWS
    if (report_freeze_as_minidump(description))
    {
        LL_INFOS("CrashReporter") << "Freeze reported with a minidump of every thread" << LL_ENDL;
        return true;
    }
    LL_WARNS("CrashReporter") << "No minidump for the freeze; reporting the watchdog's own stack" << LL_ENDL;
#endif
    // Without a dump, the report is the watchdog's own stack and what it saw.
    sentry_value_t event = sentry_value_new_message_event(SENTRY_LEVEL_FATAL, "watchdog", description.c_str());
    set_event_tag(event, "watchdog_state", LLAppViewer::instance()->getMainloopWatchdogState());
    set_event_tag(event, "app_state", LLStartUp::getStartupStateString());
    sentry_capture_event(event);
    LL_INFOS("CrashReporter") << "Freeze reported as an event" << LL_ENDL;
    return true;
}

bool ALCrashReporter::handleException(void* exception_pointers)
{
#if LL_WINDOWS
    if (!sEngaged)
    {
        return false;
    }
    sentry_ucontext_t context = {};
    context.exception_ptrs = *static_cast<EXCEPTION_POINTERS*>(exception_pointers);
    sentry_handle_exception(&context);
    return true;
#else
    return false;
#endif
}

void ALCrashReporter::fatal(std::string_view kind, const std::string& message)
{
    setTag("fatal", kind);
    setContext("fatal", {{"kind", kind}, {"message", message}});

#if LL_WINDOWS
    // Handed to the reporter as the exception abort() raised before
    // fast-fail existed, with this thread's context, so the dump is a crash
    // here; the handler does not return. Without a reporter engaged it goes
    // the usual unhandled route, and the fast-fail is the stop behind that.
    CONTEXT context = {};
    RtlCaptureContext(&context);
    EXCEPTION_RECORD record = {};
    record.ExceptionCode = FATAL_APP_EXIT;
    record.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
    record.ExceptionAddress = _ReturnAddress();
    EXCEPTION_POINTERS pointers = { &record, &context };
    handleException(&pointers);
    RaiseException(FATAL_APP_EXIT, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    __fastfail(FAST_FAIL_FATAL_APP_EXIT);
#else
    // SIGABRT is crashpad's.
    std::abort();
#endif
}
