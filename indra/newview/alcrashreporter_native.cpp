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
#if LL_LINUX
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <unistd.h>
#endif
#include <sentry.h>

#include "llagent.h"
#include "llappviewer.h"
#include "lldir.h"
#include "llerrorcontrol.h"
#include "llmemory.h"
#include "llstartup.h"
#include "llstring.h"
#include "llthread.h"
#include "lluuid.h"
#include "llversioninfo.h"
#include "llviewerregion.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iterator>

namespace
{
    bool sEngaged = false;

    // Raised while a signal handler of ours calls into the SDK, whose
    // warnings would otherwise reach the log's locks from inside it.
    std::atomic<bool> sSdkLogMuted{ false };

    // The SDK's own log, into ours: what it says about the handler, the
    // Windows Error Reporting module and the transport is otherwise unseen.
    // Its debug level is the "Sentry" tag at DEBUG; its warnings are
    // advisory (one per thread whose stack it cannot reserve a guarantee
    // on) and go at INFO, so only its errors are ours.
    void sdk_log(sentry_level_t level, const char* message, va_list args, void*)
    {
        if (sSdkLogMuted.load(std::memory_order_relaxed))
        {
            return;
        }
        char line[1024];
        vsnprintf(line, sizeof(line), message, args);
        switch (level)
        {
            case SENTRY_LEVEL_DEBUG:
                LL_DEBUGS("Sentry") << line << LL_ENDL;
                break;
            case SENTRY_LEVEL_INFO:
            case SENTRY_LEVEL_WARNING:
                LL_INFOS("Sentry") << line << LL_ENDL;
                break;
            default:
                LL_WARNS("Sentry") << line << LL_ENDL;
                break;
        }
    }

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

    void set_event_tag(sentry_value_t event, const char* key, std::string_view value)
    {
        sentry_value_t tags = sentry_value_get_by_key(event, "tags");
        if (sentry_value_is_null(tags))
        {
            tags = sentry_value_new_object();
            sentry_value_set_by_key(event, "tags", tags);
        }
        sentry_value_set_by_key(tags, key, sentry_value_new_string_n(value.data(), value.size()));
    }

    void set_event_tag(sentry_value_t event, const char* key, U64 value)
    {
        char digits[24];
        const char* end = std::to_chars(digits, digits + sizeof(digits), value).ptr;
        set_event_tag(event, key, std::string_view(digits, end - digits));
    }

    // The name of the agent's region, for the location a crash report
    // carries, kept where the crash callback can read it without following
    // a pointer into the scene. The main thread writes the half the callback
    // is not reading, then publishes it.
    char sRegionNames[2][256] = {};
    std::atomic<U32> sRegionNameHalf{ 0 };

    void remember_region()
    {
        const LLViewerRegion* region = gAgent.getRegion();
        const std::string_view name = region ? std::string_view(region->getName()) : std::string_view();
        const U32 half = sRegionNameHalf.load(std::memory_order_relaxed) ^ 1;
        const size_t length = std::min(name.size(), sizeof(sRegionNames[half]) - 1);
        memcpy(sRegionNames[half], name.data(), length);
        sRegionNames[half][length] = '\0';
        sRegionNameHalf.store(half, std::memory_order_release);
    }

    // Runs in the crashing process under crashpad, once the scope is on the
    // event: what is only known now goes on the event itself, and the marker
    // the next launch reads is written here. On Linux this is crashpad's
    // signal handler, on the crashing thread and a small alternate stack; on
    // Windows an exception filter. The crash may have interrupted whatever
    // held the heap or a lock, so nothing here allocates or locks: the
    // region's name and the marker were set aside while the viewer ran, the
    // buffers are static, and the values made are sentry's, which on Linux
    // come from an allocator of its own once crashed.
    sentry_value_t on_crash(const sentry_ucontext_t*, sentry_value_t event, sentry_hint_t*, void*)
    {
        LLAppViewer* app = LLAppViewer::instance();

        if (gAgent.getRegion())
        {
            // Where the agent was last placed, at most a frame ago.
            static char location[sizeof(sRegionNames[0]) + 64];
            const char* region = sRegionNames[sRegionNameHalf.load(std::memory_order_acquire)];
            const size_t length = ALCrashReporter::locationTag(location, sizeof(location), region,
                                                               gAgent.getFrameAgent().getOrigin());
            set_event_tag(event, "location", std::string_view(location, length));
        }

        const auto [prefix, state] = app->getMainloopWatchdogStateParts();
        if (!prefix.empty() || !state.empty())
        {
            static char watchdog[256];
            const size_t prefix_length = std::min(prefix.size(), sizeof(watchdog));
            const size_t state_length = std::min(state.size(), sizeof(watchdog) - prefix_length);
            memcpy(watchdog, prefix.data(), prefix_length);
            memcpy(watchdog + prefix_length, state.data(), state_length);
            set_event_tag(event, "watchdog_state", std::string_view(watchdog, prefix_length + state_length));
        }

        const U32 available_kb = LLMemory::getAvailableMemKB().value();
        if (available_kb != U32_MAX)
        {
            set_event_tag(event, "mem_allocated_kb", LLMemory::getAllocatedMemKB().value());
            set_event_tag(event, "mem_available_kb", available_kb);
            set_event_tag(event, "mem_max_physical_kb", LLMemory::getMaxMemKB().value());
#if LL_WINDOWS
            set_event_tag(event, "mem_available_commit_mb", LLMemory::getAvailableCommitMemMB().value());
#endif
        }

        app->createCrashMarker(app->logoutRequestSent() ? LAST_EXEC_LOGOUT_CRASH : LAST_EXEC_OTHER_CRASH);

        return event;
    }

    // Inside sentry_init, for each crash a run that has ended left unsent in
    // the database, which every instance shares: the event it was filed as,
    // which feedback about the crash names when the run is the previous one.
    void on_crashed_last_run(const sentry_envelope_t* envelope, void*)
    {
        const sentry_value_t event = sentry_envelope_get_event(envelope);
        const char* id = sentry_value_as_string(sentry_value_get_by_key(event, "event_id"));
        const char* run_id =
            sentry_value_as_string(sentry_value_get_by_key(sentry_value_get_by_key(event, "tags"), "run_id"));
        ALCrashReporter::foundCrashReport(id ? id : "", run_id ? run_id : "");
    }

    // A freeze ends the run that reports it, so its event is recorded for the
    // next run, which is where the user can say what happened. A second
    // instance records nothing: the record is the first instance's, as the
    // markers are.
    void record_freeze(const sentry_uuid_t& id)
    {
        if (sentry_uuid_is_nil(&id) || LLAppViewer::instance()->isSecondInstance())
        {
            return;
        }
        char text[37];
        sentry_uuid_as_string(&id, text);
        ALCrashReporter::recordReport(ALCrashReporter::reportRecordFile(),
                                      { "freeze", ALCrashReporter::compactEventId(text) });
    }

#if LL_LINUX
    // A freeze report's stack, where there is no minidump of a running
    // process to send: the hung main thread is sent a real-time signal of
    // its own, and the handler walks the stack from where the signal
    // interrupted it, with the SDK's unwinder, into frames set aside for it,
    // while the watchdog waits on the semaphore it posts.
    constexpr size_t FROZEN_FRAMES_MAX = 128;
    void* sFrozenFrames[FROZEN_FRAMES_MAX];
    size_t sFrozenFrameCount = 0;
    sem_t sFrozenWalked;
    pthread_t sMainThread;
    pid_t sMainThreadId = 0;
    int sFrozenSignal = 0;

    void walk_frozen_stack(int signum, siginfo_t* info, void* context)
    {
        const int saved_errno = errno;
        sentry_ucontext_t uctx = {};
        uctx.signum = signum;
        uctx.siginfo = info;
        uctx.user_context = static_cast<ucontext_t*>(context);
        sSdkLogMuted.store(true);
        sFrozenFrameCount = sentry_unwind_stack_from_ucontext(&uctx, sFrozenFrames, FROZEN_FRAMES_MAX);
        sSdkLogMuted.store(false);
        sem_post(&sFrozenWalked);
        errno = saved_errno;
    }

    // From the main thread, once. The signal is the highest real-time one
    // with no handler on it, passing over LLApp's smackdown and heartbeat
    // and SIGRTMIN + 4, which the SDK's own app-hang tracking takes when on.
    void prepare_frozen_walk()
    {
        if (sFrozenSignal || !on_main_thread())
        {
            return;
        }

        // The unwinder sets itself up and caches what it reads of the
        // loaded objects on first use: here, rather than in the handler.
        void* warm[8];
        sentry_unwind_stack(nullptr, warm, std::size(warm));

        for (int signum = SIGRTMAX; signum >= SIGRTMIN; --signum)
        {
            if (signum == LL_SMACKDOWN_SIGNAL || signum == LL_HEARTBEAT_SIGNAL || signum == SIGRTMIN + 4)
            {
                continue;
            }
            struct sigaction current = {};
            if (sigaction(signum, nullptr, &current) != 0 || current.sa_handler != SIG_DFL)
            {
                continue;
            }
            if (sem_init(&sFrozenWalked, 0, 0) != 0)
            {
                return;
            }
            struct sigaction action = {};
            action.sa_sigaction = walk_frozen_stack;
            sigemptyset(&action.sa_mask);
            action.sa_flags = SA_SIGINFO | SA_RESTART;
            if (sigaction(signum, &action, nullptr) != 0)
            {
                sem_destroy(&sFrozenWalked);
                return;
            }
            sMainThread = pthread_self();
            sMainThreadId = gettid();
            sFrozenSignal = signum;
            return;
        }
        LL_WARNS("CrashReporter") << "No real-time signal free; freeze reports carry no stack" << LL_ENDL;
    }

    // The main thread's stack on the event, as the thread the report is
    // about. A thread in an uninterruptible wait in the kernel, or with the
    // signal blocked, never runs the handler, and the wait gives up.
    bool add_frozen_thread(sentry_value_t event)
    {
        if (!sFrozenSignal)
        {
            return false;
        }
        // A handler installed over ours since would take the signal, or
        // leave its default, which ends the process.
        struct sigaction current = {};
        if (sigaction(sFrozenSignal, nullptr, &current) != 0 || current.sa_sigaction != walk_frozen_stack)
        {
            return false;
        }

        while (sem_trywait(&sFrozenWalked) == 0)
        {
        }
        if (pthread_kill(sMainThread, sFrozenSignal) != 0)
        {
            return false;
        }
        timespec deadline = {};
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += 2;
        int waited;
        do
        {
            waited = sem_timedwait(&sFrozenWalked, &deadline);
        } while (waited != 0 && errno == EINTR);
        if (waited != 0 || sFrozenFrameCount == 0)
        {
            return false;
        }

        sentry_value_t thread = sentry_value_new_thread(sMainThreadId, "main");
        sentry_value_set_by_key(thread, "crashed", sentry_value_new_bool(true));
        sentry_value_set_by_key(thread, "main", sentry_value_new_bool(true));
        sentry_value_set_stacktrace(thread, sFrozenFrames, sFrozenFrameCount);
        sentry_event_add_thread(event, thread);
        return true;
    }
#endif

    bool engage()
    {
        LLAppViewer* app = LLAppViewer::instance();
        const ALCrashReporter::Release release = ALCrashReporter::release();

        sentry_options_t* options = sentry_options_new();
        sentry_options_set_dsn(options, AL_SENTRY_DSN);
        sentry_options_set_release(options, release.name.c_str());
        sentry_options_set_environment(options, release.environment.c_str());
        sentry_options_set_dist(options, release.dist.c_str());
        set_handler_path(options, gDirUtilp->getExpandedFilename(LL_PATH_EXECUTABLE, HANDLER_NAME));
        set_database_path(options, gDirUtilp->getExpandedFilename(LL_PATH_LOGS, "sentry"));
        add_attachment(options, LLError::logFileName());
        add_attachment(options, *app->getStaticDebugFile());
        add_attachment(options, gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, "settings.xml"));
        sentry_options_set_on_crash(options, on_crash, nullptr);
        sentry_options_set_on_crashed_last_run(options, on_crashed_last_run, nullptr);
        sentry_options_set_debug(options, 1);
        sentry_options_set_logger(options, sdk_log, nullptr);
        sentry_options_set_logger_enabled_when_crashed(options, 0);

        if (sentry_init(options) != 0)
        {
            LL_WARNS("CrashReporter") << "Sentry did not start; crashes go unreported" << LL_ENDL;
            follow_wer(false);
            return false;
        }
        sEngaged = true;
        follow_wer(true);

        for (const auto& [key, value] : ALCrashReporter::commonTags())
        {
            ALCrashReporter::setTag(key, value);
        }

        // The region's name follows the agent, and the region's own renames.
        static bool following_region = false;
        if (!following_region)
        {
            following_region = true;
            gAgent.addRegionChangedCallback(&remember_region);
            LLViewerRegion::setRegionInfoChangedCallback([](LLViewerRegion*) { remember_region(); });
        }
        remember_region();
#if LL_LINUX
        prepare_frozen_walk();
#endif

        LL_INFOS("CrashReporter") << "Sentry engaged for " << LLVersionInfo::instance().getChannelAndVersion() << LL_ENDL;
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
        record_freeze(id);
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
    // Without a dump, the report is what the watchdog saw, and on Linux the
    // main thread's stack, asked of it.
    sentry_value_t event = sentry_value_new_message_event(SENTRY_LEVEL_FATAL, "watchdog", description.c_str());
    set_event_tag(event, "watchdog_state", LLAppViewer::instance()->getMainloopWatchdogState());
    set_event_tag(event, "app_state", LLStartUp::getStartupStateString());
#if LL_LINUX
    const bool with_stack = add_frozen_thread(event);
#else
    const bool with_stack = false;
#endif
    record_freeze(sentry_capture_event(event));
    LL_INFOS("CrashReporter") << (with_stack ? "Freeze reported with the main thread's stack"
                                             : "Freeze reported as an event") << LL_ENDL;
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
