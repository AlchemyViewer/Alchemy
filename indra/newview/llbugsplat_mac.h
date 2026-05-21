/**
 * @file   llbugsplat_mac.h
 * @brief  Cross-backend BugSplat crash-metadata helpers for macOS.
 *
 *         The fields, accessor, and helper functions declared here are
 *         consumed by both the legacy Cocoa app delegate
 *         (llappdelegate-objc.mm) and the SDL viewer
 *         (llappviewersdl.cpp), so they live outside
 *         llappviewermacosx.cpp (which is only compiled for the non-SDL
 *         backend).
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (c) 2026, Linden Research, Inc.
 * $/LicenseInfo$
 */

#if ! defined(LL_LLBUGSPLAT_MAC_H)
#define LL_LLBUGSPLAT_MAC_H

#include <string>

// Collected metadata about the *previous* viewer run, ready to be attached to
// the crash report submitted on this run. Populated lazily on first access.
struct CrashMetadata
{
    std::string logFilePathname;
    std::string attributesPathname;
    std::string userSettingsPathname;
    std::string accountSettingsPathname;
    std::string staticDebugPathname;
    std::string OSInfo;
    std::string agentFullname;
    std::string regionName;
    std::string fatalMessage;
    std::string secondLogFilePathname;
};

CrashMetadata& CrashMetadata_instance();

// Thin wrapper around LL_INFOS so the Obj-C++ delegate can log without
// pulling in the full LLError header.
void infos(const std::string& message);

// Wipe the dump-logs directory after a crash report is submitted (skipped on
// secondary viewer instances so they don't clobber the primary's logs).
void clearDumpLogsDir();

#endif // ! defined(LL_LLBUGSPLAT_MAC_H)
