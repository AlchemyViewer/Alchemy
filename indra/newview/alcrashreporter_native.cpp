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
#endif
#include <sentry.h>

#include "llagent.h"
#include "llapp.h"
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
}

bool ALCrashReporter::init()
{
#if !LL_SEND_CRASH_REPORTS
    return false;
#else
    if (LLApp::isCrashloggerDisabled())
    {
        LL_INFOS("CrashReporter") << "Crash reporting is off by setting" << LL_ENDL;
        return false;
    }

    LLAppViewer* app = LLAppViewer::instance();
    const LLVersionInfo& version = LLVersionInfo::instance();

    sentry_options_t* options = sentry_options_new();
    sentry_options_set_dsn(options, AL_SENTRY_DSN);
    sentry_options_set_release(options, releaseName(version.getMajor(), version.getMinor(),
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
        return false;
    }
    sEngaged = true;

    sentry_set_tag("os", LLOSInfo::instance().getOSStringSimple().c_str());
    sentry_set_tag("second_instance", app->isSecondInstance() ? "true" : "false");
    sentry_set_tag("app_state", LLStartUp::getStartupStateString().c_str());

    LL_INFOS("CrashReporter") << "Sentry engaged for " << version.getChannelAndVersion() << LL_ENDL;
    return true;
#endif
}

void ALCrashReporter::shutdown()
{
    if (sEngaged)
    {
        sentry_close();
        sEngaged = false;
    }
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

void ALCrashReporter::attach(const std::string& path)
{
    if (sEngaged)
    {
        attach_now(path);
    }
}

bool ALCrashReporter::reportFreeze(const std::string& description)
{
    if (!sEngaged)
    {
        return false;
    }
    sentry_value_t event = sentry_value_new_message_event(SENTRY_LEVEL_FATAL, "watchdog", description.c_str());
    set_event_tag(event, "watchdog_state", LLAppViewer::instance()->getMainloopWatchdogState());
    set_event_tag(event, "app_state", LLStartUp::getStartupStateString());
    sentry_capture_event(event);
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
