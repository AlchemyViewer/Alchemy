/**
 * @file   llbugsplat_mac.cpp
 * @brief  Cross-backend BugSplat crash-metadata helpers for macOS.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (c) 2026, Linden Research, Inc.
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "llbugsplat_mac.h"

#include "llappviewer.h"
#include "lldir.h"
#include "lldiriterator.h"
#include "llerror.h"
#include "llerrorcontrol.h"
#include "llfile.h"
#include "llsdserialize.h"
#include "llstring.h"
#include "llxmlnode.h"

#include <fstream>

namespace
{

// The BugsplatMac API is structured as a number of different method
// overrides, each returning a different piece of metadata. But since we
// obtain such metadata by opening and parsing a file, it seems ridiculous to
// reopen and reparse it for every individual string desired. What we want is
// to open and parse the file once, retaining the data for subsequent
// requests. That's why this is an LLSingleton.
class CrashMetadataSingleton: public CrashMetadata, public LLSingleton<CrashMetadataSingleton>
{
    LLSINGLETON(CrashMetadataSingleton);

    std::string get_metadata(const LLSD& info, const LLSD::String& key) const
    {
        std::string data(info[key].asString());
        LL_INFOS("Bugsplat") << "  " << key << "='" << data << "'" << LL_ENDL;
        return data;
    }
};

CrashMetadataSingleton::CrashMetadataSingleton()
{
    // Note: we depend on being able to read the static_debug_info.log file
    // from the *previous* run before we overwrite it with the new one for
    // *this* run. LLAppViewer initialization must happen in the Right Order.
    staticDebugPathname = *LLAppViewer::instance()->getStaticDebugFile();
    std::ifstream static_file(staticDebugPathname);
    LLSD info;
    if (! static_file.is_open())
    {
        LL_WARNS("Bugsplat") << "Can't open '" << staticDebugPathname
                   << "'; no metadata about previous run" << LL_ENDL;
    }
    else if (! LLSDSerialize::deserialize(info, static_file, LLSDSerialize::SIZE_UNLIMITED))
    {
        LL_WARNS("Bugsplat") << "Can't parse '" << staticDebugPathname
                   << "'; no metadata about previous run" << LL_ENDL;
    }
    else
    {
        LL_INFOS("Bugsplat") << "Previous run metadata from '" << staticDebugPathname << "':" << LL_ENDL;
        logFilePathname      = get_metadata(info, "SLLog");
        userSettingsPathname = get_metadata(info, "SettingsFilename");
        accountSettingsPathname = get_metadata(info, "PerAccountSettingsFilename");
        OSInfo               = get_metadata(info, "OSInfo");
        agentFullname        = get_metadata(info, "LoginName");
        // Translate underscores back to spaces
        LLStringUtil::replaceChar(agentFullname, '_', ' ');
        regionName           = get_metadata(info, "CurrentRegion");
        fatalMessage         = get_metadata(info, "FatalMessage");

        if (gDirUtilp->fileExists(gDirUtilp->getDumpLogsDirPath()))
        {
            LLDirIterator file_iter(gDirUtilp->getDumpLogsDirPath(), "*.log");
            std::string file_name;
            bool found = true;
            while (found)
            {
                if ((found = file_iter.next(file_name)))
                {
                    std::string log_filename = gDirUtilp->getDumpLogsDirPath(file_name);
                    if (LLError::logFileName() != log_filename)
                    {
                        secondLogFilePathname = log_filename;
                    }
                }
            }
        }

        // Populate bugsplat attributes
        LLXMLNodePtr out_node = new LLXMLNode("XmlCrashContext", false);

        out_node->createChild("OS", false)->setValue(OSInfo);
        out_node->createChild("AppState", false)->setValue(info["StartupState"].asString());
        out_node->createChild("GraphicsCard", false)->setValue(info["GraphicsCard"].asString());
        out_node->createChild("GLVersion", false)->setValue(info["GLInfo"]["GLVersion"].asString());
        out_node->createChild("GLRenderer", false)->setValue(info["GLInfo"]["GLRenderer"].asString());
        out_node->createChild("RAM", false)->setValue(info["RAMInfo"]["Physical"].asString());

        if (!out_node->isNull())
        {
            attributesPathname = gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, "CrashContext.xml");
            LLFILE* fp = LLFile::fopen(attributesPathname, "w");

            if (fp != NULL)
            {
                LLXMLNode::writeHeaderToFile(fp);
                out_node->writeToFile(fp);

                fclose(fp);
            }
        }
    }
}

} // namespace

// Avoid having to compile all of our LLSingleton machinery in Objective-C++.
CrashMetadata& CrashMetadata_instance()
{
    return CrashMetadataSingleton::instance();
}

void infos(const std::string& message)
{
    LL_INFOS("InitOSX", "Bugsplat") << message << LL_ENDL;
}

void clearDumpLogsDir()
{
    if (!LLAppViewer::instance()->isSecondInstance())
    {
        gDirUtilp->deleteDirAndContents(gDirUtilp->getDumpLogsDirPath());
    }
}
