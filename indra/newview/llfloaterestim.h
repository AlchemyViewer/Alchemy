/**
 * @file llfloaterestim.h
 * @brief E-Stim Device Controls UI Floater header
 */

#pragma once

#include "llfloater.h"
#include "llscrolllistctrl.h"

class LLFloaterEstim : public LLFloater
{
public:
    LLFloaterEstim(const LLSD& key);
    ~LLFloaterEstim() override = default;

    bool postBuild() override;
    void draw() override;

private:
    void onPanicPressed();
    void onTestAPressed();
    void onTestBPressed();
    void onClearTriggersPressed();
    void onSliderAModified(LLUICtrl* ctrl);
    void onSliderBModified(LLUICtrl* ctrl);
    void updateTriggerList();
    void updateSensorList();

    LLScrollListCtrl* mTriggerList{ nullptr };
    LLScrollListCtrl* mSensorList{ nullptr };
};
