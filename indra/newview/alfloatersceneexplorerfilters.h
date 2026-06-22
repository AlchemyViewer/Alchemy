/**
 * @file alfloatersceneexplorerfilters.h
 * @brief Companion filters floater for the Scene Explorer (the inventory
 *        filter-finder pattern): the full predicate set, too large for the
 *        explorer's quick bar.
 *
 * Copyright (c) 2026, Rye Mutt <rye@alchemyviewer.org>
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-licence.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 *
 */
#ifndef AL_FLOATERSCENEEXPLORERFILTERS_H
#define AL_FLOATERSCENEEXPLORERFILTERS_H

#include "llfloater.h"

class ALFloaterSceneExplorerFilters final : public LLFloater
{
    friend class LLFloaterReg;
public:
    bool postBuild() override;
    void onOpen(const LLSD& key) override;

    // Persisted settings -> controls (also called by the explorer's Reset).
    void refreshFromSettings();

private:
    ALFloaterSceneExplorerFilters(const LLSD& key);
    ~ALFloaterSceneExplorerFilters() override = default;

    // Controls -> persisted settings -> explorer filter refresh. The settings
    // are the single source of truth shared with the explorer's quick bar.
    void onCommitAny();
    void onClickReset();
};

#endif // AL_FLOATERSCENEEXPLORERFILTERS_H
