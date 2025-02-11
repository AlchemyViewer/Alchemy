
#include "linden_common.h"
#include "llapp.h"
#include "llassettype.h"
#include "lldir.h"
#include <boost/filesystem.hpp>
#include <boost/range/iterator_range.hpp>
#include <chrono>

#include "lldiskcache.h"

const std::string DISK_CACHE_DIR_NAME = "cache";

LLDiskCache::LLDiskCache()
{
}

void LLDiskCache::init(ELLPath location, const uintmax_t max_size_bytes, const bool enable_cache_debug_info, const bool cache_version_mismatch)
{
    mMaxSizeBytes = max_size_bytes;
    mEnableCacheDebugInfo = enable_cache_debug_info;
    mCacheDir = gDirUtilp->getExpandedFilename(location, DISK_CACHE_DIR_NAME);

    if (cache_version_mismatch)
    {
        clearCache(location, false);
    }

    createCache();
}


void LLDiskCache::createCache()
{
    LLFile::mkdir(mCacheDir);
    std::vector<std::string> uuidprefix = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c", "d", "e", "f" };
    for (auto& prefixchar : uuidprefix)
    {
        LLFile::mkdir(fmt::format("{}{}{}", mCacheDir, gDirUtilp->getDirDelimiter(), prefixchar));
    }
}

// WARNING: purge() is called by LLPurgeDiskCacheThread. As such it must
// NOT touch any LLDiskCache data without introducing and locking a mutex!

// Interaction through the filesystem itself should be safe. Let’s say thread
// actually gone, the OS call from B to delete the file will fail since the OS
// will prevent this. B continues with the next file. If the file is already
// gone before A finally gets to open it, this operation will fail and the
// asset will have to be re-requested.
void LLDiskCache::purge()
{
    if (mReadOnly) return;

    if (mEnableCacheDebugInfo)
    {
        LL_INFOS() << "Total dir size before purge is " << dirFileSize(mCacheDir) << LL_ENDL;
    }

    boost::system::error_code ec;
    auto start_time = std::chrono::high_resolution_clock::now();

    typedef std::pair<std::time_t, std::pair<uintmax_t, boost::filesystem::path>> file_info_t;
    std::vector<file_info_t> file_info;

#if LL_WINDOWS
    boost::filesystem::path cache_path(ll_convert_string_to_wide(mCacheDir));
#else
    boost::filesystem::path cache_path(mCacheDir);
#endif
    if (boost::filesystem::is_directory(cache_path, ec) && !ec.failed())
    {
        boost::filesystem::recursive_directory_iterator dir_iter(cache_path, ec);
        if (!ec.failed())
        {
            for (auto& entry : boost::make_iterator_range(dir_iter, {}))
            {
                if(!LLApp::isRunning())
                {
                    return;
                }

                if (boost::filesystem::is_regular_file(entry, ec) && !ec.failed())
                {
                    if (entry.path().string().rfind(mCacheFilenameExt) != std::string::npos)
                    {
                        const uintmax_t file_size = boost::filesystem::file_size(entry, ec);
                        if (ec.failed())
                        {
                            LL_WARNS() << "Failed to read file size for cache file " << entry.path().string() << ": " << ec.message() << LL_ENDL;
                            continue;
                        }
                        const std::time_t file_time = boost::filesystem::last_write_time(entry, ec);
                        if (ec.failed())
                        {
                            LL_WARNS() << "Failed to read last write time for cache file " << entry.path().string() << ": " << ec.message() << LL_ENDL;
                            continue;
                        }

                        file_info.push_back(file_info_t(file_time, { file_size, entry.path() }));
                    }
                }
            }
        }
    }

    std::sort(file_info.begin(), file_info.end(), [](const file_info_t& x, const file_info_t& y)
    {
        return x.first > y.first;
    });

    LL_INFOS() << "Purging cache to a maximum of " << mMaxSizeBytes << " bytes" << LL_ENDL;

    if (mEnableCacheDebugInfo)
    {
        file_removed.reserve(file_info.size());
    }

    uintmax_t file_size_total = 0;
    for (const file_info_t& entry : file_info)
    {
        if (!LLApp::isRunning())
        {
            return;
        }

        file_size_total += entry.second.first;

        bool should_remove = file_size_total > mMaxSizeBytes;
        if (mEnableCacheDebugInfo)
        {
        if (should_remove)
        {
            boost::filesystem::remove(entry.second.second, ec);
            if (ec.failed())
            {
                LL_WARNS() << "Failed to delete cache file " << entry.second.second << ": " << ec.message() << LL_ENDL;
                continue;
            }
        }
    }

    if (mEnableCacheDebugInfo)
    {
        auto execute_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

        // Log afterward so it doesn't affect the time measurement
        // Logging thousands of file results can take hundreds of milliseconds
        for (size_t i = 0; i < file_info.size(); ++i)
        {
            if (!LLApp::isRunning())
            {
                return;
            }

            const file_info_t& entry = file_info[i];
            const bool removed = file_removed[i];
            const std::string action = removed ? "DELETE:" : "KEEP:";

            // have to do this because of LL_INFO/LL_END weirdness
            line << entry.second.first << "  ";
            line << entry.second.second;
            line << " (" << file_size_total << "/" << mMaxSizeBytes << ")";
            LL_INFOS() << line.str() << LL_ENDL;
        }

        LL_INFOS() << "Total dir size after purge is " << dirFileSize(mCacheDir) << LL_ENDL;
        LL_INFOS() << "Cache purge took " << execute_time << " ms to execute for " << file_info.size() << " files" << LL_ENDL;
    }
}

//static
const std::string LLDiskCache::assetTypeToString(LLAssetType::EType at)
{
    /**
     * Make use of the handy C++17  feature that allows
     * for inline initialization of an std::map<>
     */
    typedef std::map<LLAssetType::EType, std::string> asset_type_to_name_t;
    static asset_type_to_name_t asset_type_to_name =
    {
        { LLAssetType::AT_TEXTURE, "TEXTURE" },
        { LLAssetType::AT_SOUND, "SOUND" },
        { LLAssetType::AT_CALLINGCARD, "CALLINGCARD" },
        { LLAssetType::AT_LANDMARK, "LANDMARK" },
        { LLAssetType::AT_SCRIPT, "SCRIPT" },
        { LLAssetType::AT_CLOTHING, "CLOTHING" },
        { LLAssetType::AT_OBJECT, "OBJECT" },
        { LLAssetType::AT_NOTECARD, "NOTECARD" },
        { LLAssetType::AT_CATEGORY, "CATEGORY" },
        { LLAssetType::AT_LSL_TEXT, "LSL_TEXT" },
        { LLAssetType::AT_LSL_BYTECODE, "LSL_BYTECODE" },
        { LLAssetType::AT_TEXTURE_TGA, "TEXTURE_TGA" },
        { LLAssetType::AT_BODYPART, "BODYPART" },
        { LLAssetType::AT_SOUND_WAV, "SOUND_WAV" },
        { LLAssetType::AT_IMAGE_TGA, "IMAGE_TGA" },
        { LLAssetType::AT_IMAGE_JPEG, "IMAGE_JPEG" },
        { LLAssetType::AT_ANIMATION, "ANIMATION" },
        { LLAssetType::AT_GESTURE, "GESTURE" },
        { LLAssetType::AT_SIMSTATE, "SIMSTATE" },
        { LLAssetType::AT_LINK, "LINK" },
        { LLAssetType::AT_LINK_FOLDER, "LINK_FOLDER" },
        { LLAssetType::AT_MARKETPLACE_FOLDER, "MARKETPLACE_FOLDER" },
        { LLAssetType::AT_WIDGET, "WIDGET" },
        { LLAssetType::AT_PERSON, "PERSON" },
        { LLAssetType::AT_MESH, "MESH" },
        { LLAssetType::AT_SETTINGS, "SETTINGS" },
        { LLAssetType::AT_MATERIAL, "MATERIAL" },
        { LLAssetType::AT_GLTF, "GLTF" },
        { LLAssetType::AT_GLTF_BIN, "GLTF_BIN" },
        { LLAssetType::AT_UNKNOWN, "UNKNOWN" }
    };

    asset_type_to_name_t::iterator iter = asset_type_to_name.find(at);
    if (iter != asset_type_to_name.end())
    {
        return iter->second;
    }

    return std::string("UNKNOWN");
}

const boost::filesystem::path LLDiskCache::metaDataToFilepath(const LLUUID& id,
        LLAssetType::EType at)
{
    std::string uuidstr = id.asString();
    const auto& dirdelim = gDirUtilp->getDirDelimiter();
    std::string out_string = fmt::format(FMT_COMPILE("{:s}{:s}{}{}{}{}"), mCacheDir, dirdelim, std::string_view(&uuidstr[0], 1), dirdelim, uuidstr, mCacheFilenameExt);
#if LL_WINDOWS
    return boost::filesystem::path(ll_convert_string_to_wide(out_string));
#else
    return boost::filesystem::path(out_string);
#endif
}

// static
void LLDiskCache::updateFileAccessTime(const boost::filesystem::path& file_path)
{
    /**
     * Threshold in time_t units that is used to decide if the last access time
     * time of the file is updated or not. Added as a precaution for the concern
     * outlined in SL-14582  about frequent writes on older SSDs reducing their
     * lifespan. I think this is the right place for the threshold value - rather
     * than it being a pref - do comment on that Jira if you disagree...
     *
     * Let's start with 1 hour in time_t units and see how that unfolds
     */
    static const std::time_t time_threshold = 1 * 60 * 60;

    // current time
    const std::time_t cur_time = std::time(nullptr);

    boost::system::error_code ec;
    // file last write time
    const std::time_t last_write_time = boost::filesystem::last_write_time(file_path, ec);
    if (ec.failed())
    {
        LL_WARNS() << "Failed to read last write time for cache file " << file_path << ": " << ec.message() << LL_ENDL;
        return;
    }

    // delta between cur time and last time the file was written
    const std::time_t delta_time = cur_time - last_write_time;

    // we only write the new value if the time in time_threshold has elapsed
    // before the last one
    if (delta_time > time_threshold)
    {
        boost::filesystem::last_write_time(file_path, cur_time, ec);
    }

    if (ec.failed())
    {
        LL_WARNS() << "Failed to update last write time for cache file " << file_path << ": " << ec.message() << LL_ENDL;
    }
}

const std::string LLDiskCache::getCacheInfo()
{
    uintmax_t cache_used_mb = dirFileSize(mCacheDir) / (1024U * 1024U);

    uintmax_t max_in_mb = mMaxSizeBytes / (1024U * 1024U);
    F64 percent_used = ((F64)cache_used_mb / (F64)max_in_mb) * 100.0;

    return llformat("%juMB / %juMB (%.1f%% used)", cache_used_mb, max_in_mb, percent_used);
}

void LLDiskCache::clearCache(ELLPath location, bool recreate_cache)
{
    if (!mReadOnly)
    {
        std::string disk_cache_dir = gDirUtilp->getExpandedFilename(location, DISK_CACHE_DIR_NAME);

        const char* subdirs = "0123456789abcdef";
        std::string delem = gDirUtilp->getDirDelimiter();
        std::string mask = "*";
        for (S32 i = 0; i < 16; i++)
        {
            std::string dirname = disk_cache_dir + delem + subdirs[i];
            LL_INFOS() << "Deleting files in directory: " << dirname << LL_ENDL;
            gDirUtilp->deleteDirAndContents(dirname);
#if LL_WINDOWS
            // Texture cache can be large and can take a while to remove
            // assure OS that processes is alive and not hanging
            MSG msg;
            PeekMessage(&msg, 0, 0, 0, PM_NOREMOVE | PM_NOYIELD);
#endif
        }
        gDirUtilp->deleteFilesInDir(disk_cache_dir, mask);
        if (recreate_cache)
        {
            createCache();
        }
    }
}

void LLDiskCache::removeOldVFSFiles()
{
    //VFS files won't be created, so consider removing this code later
    static const char CACHE_FORMAT[] = "inv.llsd";
    static const char DB_FORMAT[] = "db2.x";

    boost::system::error_code ec;
#if LL_WINDOWS
    std::wstring cache_path(ll_convert_string_to_wide(gDirUtilp->getExpandedFilename(LL_PATH_CACHE, "")));
#else
    std::string cache_path(gDirUtilp->getExpandedFilename(LL_PATH_CACHE, ""));
#endif
    if (boost::filesystem::is_directory(cache_path, ec) && !ec.failed())
    {
        boost::filesystem::directory_iterator iter(cache_path, ec);
            }
            iter.increment(ec);
        }
    }
}

uintmax_t LLDiskCache::dirFileSize(const std::string dir)
{
    uintmax_t total_file_size = 0;

    /**
     * There may be a better way that works directly on the folder (similar to
     * right clicking on a folder in the OS and asking for size vs right clicking
     * it should be okay. Note that's it's only currently used for logging/debugging
     * so if performance is ever an issue, optimizing this or removing it altogether,
     * is an easy win.
     */
    boost::system::error_code ec;
#if LL_WINDOWS
    boost::filesystem::path dir_path(ll_convert_string_to_wide(dir));
#else
    boost::filesystem::path dir_path(dir);
#endif
    if (boost::filesystem::is_directory(dir_path, ec) && !ec.failed())
    {
        boost::filesystem::recursive_directory_iterator dir_iter(dir_path,ec);
        if (!ec.failed())
        {
            for (auto& entry : boost::make_iterator_range(dir_iter, {}))
            {
                ec.clear();
                if (boost::filesystem::is_regular_file(entry, ec) && !ec.failed())
                {
                    if (entry.path().string().rfind(mCacheFilenameExt) != std::string::npos)
                    {
                        uintmax_t file_size = boost::filesystem::file_size(entry, ec);
                        if (ec.failed())
                        {
                            LL_WARNS() << "Failed to get file size for cache file " << entry.path().string() << " : " << ec.message() << LL_ENDL;
                            continue;
                        }
                        total_file_size += file_size;
                    }
                }
            }
        }
    }

    return total_file_size;
}

