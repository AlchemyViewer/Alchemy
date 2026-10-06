/**
 * @file allooknotecard.cpp
 * @brief A Lightbox Look given as a notecard: one made in inventory from the settings, and one read from inventory into the Looks.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Alchemy Viewer Project.
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

#include "allooknotecard.h"

#include "allookformat.h"
#include "alscriptworkspace.h"

#include "llagent.h"
#include "llagentui.h"
#include "lldir.h"
#include "llfile.h"
#include "llfloaterperms.h"
#include "llinventorymodel.h"
#include "llinventorypanel.h"
#include "llnotecard.h"
#include "llnotificationsutil.h"
#include "lltrans.h"
#include "llversioninfo.h"
#include "llviewerinventory.h"

// The format's own limit stands in for the notecard's so that the format
// can be tested without the inventory library; this is where the two are
// held to the same number.
static_assert(ALLookFormat::NOTECARD_BYTE_LIMIT == static_cast<size_t>(LLNotecard::MAX_SIZE),
              "ALLookFormat::NOTECARD_BYTE_LIMIT must equal LLNotecard::MAX_SIZE");

namespace
{
    // The names no Look may be saved under: the default preset's, which
    // savePreset refuses, in English and as translated.
    bool isReservedLookName(const std::string& name)
    {
        return name == PRESETS_DEFAULT || name == LLTrans::getString(PRESETS_DEFAULT);
    }

    // The name a Look from outside is saved under: the first of its own
    // name, "name (2)", "name (3)" and so on that is free, or that an
    // identical Look already holds. Empty where none could be found, which
    // takes a thousand Looks of one name.
    std::string chooseImportedLookName(const std::string& wanted, const LLSD& settings, bool& already_had)
    {
        LLPresetsManager& presets = LLPresetsManager::instance();
        already_had               = false;
        for (S32 n = 1; n < 1000; ++n)
        {
            const std::string candidate = ALLookFormat::numberedLookName(wanted, n);
            if (candidate.empty())
            {
                return std::string();
            }
            if (isReservedLookName(candidate))
            {
                continue;
            }
            if (!presets.presetExists(PRESETS_LOOKS, candidate))
            {
                return candidate;
            }
            if (presets.lookHoldsSameSettings(candidate, settings))
            {
                already_had = true;
                return candidate;
            }
        }
        return std::string();
    }
} // namespace

void ALLookNotecard::shareLookAsNotecard(const std::string& look_name, const LLSD& look_file_params)
{
    ALLookFormat::SharedLook look;
    look.metadata.name = look_name;
    LLAgentUI::buildFullname(look.metadata.author);
    look.metadata.viewer = LLVersionInfo::instance().getChannelAndVersion();
    look.metadata.date   = LLDate::now();
    look.settings        = ALLookFormat::settingsWithoutDescriptions(look_file_params);

    LLSD args;
    args["NAME"] = look_name;

    // Refused before anything is made, so that nothing empty is left where
    // the upload would have refused it.
    std::string text;
    if (!ALLookFormat::composeNotecardText(look, text))
    {
        args["SIZE"]  = static_cast<S32>(text.size());
        args["LIMIT"] = static_cast<S32>(LLNotecard::MAX_SIZE);
        LLNotificationsUtil::add("LightBoxLookTooLarge", args);
        return;
    }

    const std::string notecard_name = ALLookFormat::notecardNameForLook(look_name);
    args["NOTECARD"]                = notecard_name;

    // The account's default permissions for a notecard, as every other way
    // of making one gives -- but always with Copy and Transfer for the
    // next owner. A notecard is made here to be given, and one without
    // Transfer cannot be; and whoever receives it can only read it with
    // Copy (ALScriptWorkspace::load, as the notecard window has always
    // had it), so without Copy the Look could not be imported at all.
    // Modify follows the defaults: a Look is read, not edited, to be used.
    const U32 next_owner_perms = LLFloaterPerms::getNextOwnerPerms("Notecards") | PERM_COPY | PERM_TRANSFER;

    std::string error;
    const bool  sent = ALScriptWorkspace::getInstance()->createNotecardWithText(
        notecard_name, LLTrans::getString("LightBoxLookNotecardDescription"), gInventory.findCategoryUUIDForType(LLFolderType::FT_NOTECARD),
        text, next_owner_perms,
        [args](const ALScriptCompileResult& result) {
            LLSD said = args;
            if (!result.success)
            {
                said["REASON"] = result.error;
                LLNotificationsUtil::add("LightBoxLookNotShared", said);
                return;
            }
            LLNotificationsUtil::add("LightBoxLookShared", said);
            // Shown only now that its text is up: shown as it was made, an
            // empty notecard could have been given before it was written.
            LLInventoryPanel::openInventoryPanelAndSetSelection(true, result.ref.item, false, false, false);
        },
        error);
    if (!sent)
    {
        args["REASON"] = error;
        LLNotificationsUtil::add("LightBoxLookNotShared", args);
    }
}

void ALLookNotecard::importLookFromNotecardText(const std::string& text, const std::string& notecard_name, imported_callback_t done)
{
    ImportedLook             imported;
    ALLookFormat::SharedLook look;
    if (!ALLookFormat::parseNotecardText(text, look))
    {
        imported.failure = ImportedLook::Failure::NotALook;
        done(imported);
        return;
    }

    // Its own name; the notecard's where the text has none; and a name of
    // the viewer's where neither does, or where the name is one no Look
    // may have.
    std::string wanted = look.metadata.name;
    if (wanted.empty())
    {
        wanted = ALLookFormat::lookNameFromNotecardName(notecard_name);
    }
    if (wanted.empty() || isReservedLookName(wanted))
    {
        wanted = LLTrans::getString("LightBoxLookImportFallbackName");
    }
    imported.wantedName = wanted;
    imported.lookName   = chooseImportedLookName(wanted, look.settings, imported.alreadyHad);
    if (imported.lookName.empty())
    {
        imported.failure = ImportedLook::Failure::NotSaved;
        done(imported);
        return;
    }

    if (!imported.alreadyHad && !LLPresetsManager::instance().saveSharedLook(imported.lookName, look.settings, imported.counts))
    {
        imported.failure = ImportedLook::Failure::NotSaved;
        done(imported);
        return;
    }

    // A LUT the Look names that is not here is shown without, silently, by
    // the renderer; say so instead, by the bare name the setting was cut to.
    if (look.settings.has("RenderColorGradeLUT"))
    {
        const std::string lut = ALLookFormat::lutFileNameOnly(look.settings["RenderColorGradeLUT"]["Value"].asString());
        if (!lut.empty() && !colorLutFileIsInstalled(lut))
        {
            imported.missingLut = lut;
        }
    }
    done(imported);
}

void ALLookNotecard::importLookFromInventoryNotecard(const LLUUID& item_id, imported_callback_t done)
{
    const LLViewerInventoryItem* item = gInventory.getItem(item_id);
    if (!item || item->getType() != LLAssetType::AT_NOTECARD)
    {
        ImportedLook failed;
        failed.failure = ImportedLook::Failure::Unreadable;
        failed.reason  = LLTrans::getString("WorkspaceNoSuchItem");
        done(failed);
        return;
    }
    if (item->getAssetUUID().isNull())
    {
        // Made and never saved: there is nothing in it to read.
        ImportedLook failed;
        failed.failure = ImportedLook::Failure::NotALook;
        done(failed);
        return;
    }
    const std::string name = item->getName();
    ALScriptWorkspace::getInstance()->load(ALScriptRef(LLUUID::null, item_id), [name, done](const ALScriptLoaded& loaded) {
        if (!loaded.error.empty())
        {
            ImportedLook failed;
            failed.failure = ImportedLook::Failure::Unreadable;
            failed.reason  = loaded.error;
            done(failed);
            return;
        }
        importLookFromNotecardText(loaded.text, name, done);
    });
}

bool ALLookNotecard::colorLutFileIsInstalled(const std::string& lut_name)
{
    // The same two places, in the same order, as LLPipeline::setupGradingLUT.
    return LLFile::isfile(gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, "colorlut", lut_name)) ||
           LLFile::isfile(gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS, "colorlut", lut_name));
}
