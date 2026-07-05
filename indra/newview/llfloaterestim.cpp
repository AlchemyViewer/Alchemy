/**
 * @file llfloaterestim.cpp
 * @brief E-Stim Device Controls UI Floater implementation
 */

#include "llviewerprecompiledheaders.h"
#include "llfloaterestim.h"
#include "llestimwsmgr.h"
#include "llbutton.h"
#include "lltextbox.h"
#include "llscrolllistctrl.h"
#include "llsliderctrl.h"

LLFloaterEstim::LLFloaterEstim(const LLSD& key)
    : LLFloater(key)
{
}

bool LLFloaterEstim::postBuild()
{
    mTriggerList = getChild<LLScrollListCtrl>("trigger_list");
    mSensorList = getChild<LLScrollListCtrl>("sensor_list");

    LLButton* panic_btn = getChild<LLButton>("panic_button");
    if (panic_btn) panic_btn->setCommitCallback(boost::bind(&LLFloaterEstim::onPanicPressed, this));

    LLButton* test_a_btn = getChild<LLButton>("test_a_btn");
    if (test_a_btn) test_a_btn->setCommitCallback(boost::bind(&LLFloaterEstim::onTestAPressed, this));

    LLButton* test_b_btn = getChild<LLButton>("test_b_btn");
    if (test_b_btn) test_b_btn->setCommitCallback(boost::bind(&LLFloaterEstim::onTestBPressed, this));

    LLButton* clear_btn = getChild<LLButton>("clear_btn");
    if (clear_btn) clear_btn->setCommitCallback(boost::bind(&LLFloaterEstim::onClearTriggersPressed, this));

    LLSliderCtrl* slider_a = getChild<LLSliderCtrl>("max_a_slider");
    if (slider_a) slider_a->setCommitCallback(boost::bind(&LLFloaterEstim::onSliderAModified, this, _1));

    LLSliderCtrl* slider_b = getChild<LLSliderCtrl>("max_b_slider");
    if (slider_b) slider_b->setCommitCallback(boost::bind(&LLFloaterEstim::onSliderBModified, this, _1));

    if (auto server = LLEstimWSServer::getInstance())
    {
        if (slider_a) slider_a->setValue((F32)server->getMaxIntensityA());
        if (slider_b) slider_b->setValue((F32)server->getMaxIntensityB());
    }

    return true;
}

void LLFloaterEstim::onPanicPressed()
{
    if (auto server = LLEstimWSServer::getInstance())
    {
        server->panicStop();
    }
}

void LLFloaterEstim::onTestAPressed()
{
    if (auto server = LLEstimWSServer::getInstance())
    {
        server->testChannelA();
    }
}

void LLFloaterEstim::onTestBPressed()
{
    if (auto server = LLEstimWSServer::getInstance())
    {
        server->testChannelB();
    }
}

void LLFloaterEstim::onClearTriggersPressed()
{
    if (auto server = LLEstimWSServer::getInstance())
    {
        server->clearTriggers();
        updateTriggerList();
    }
}

void LLFloaterEstim::onSliderAModified(LLUICtrl* ctrl)
{
    if (auto server = LLEstimWSServer::getInstance())
    {
        server->setMaxIntensityA((U32)ctrl->getValue().asInteger());
    }
}

void LLFloaterEstim::onSliderBModified(LLUICtrl* ctrl)
{
    if (auto server = LLEstimWSServer::getInstance())
    {
        server->setMaxIntensityB((U32)ctrl->getValue().asInteger());
    }
}

void LLFloaterEstim::draw()
{
    LLFloater::draw();

    auto server = LLEstimWSServer::getInstance();
    if (!server)
    {
        return;
    }

    LLTextBox* status_txt = getChild<LLTextBox>("status_label");
    if (status_txt)
    {
        status_txt->setText(LLStringExplicit(server->isConnected() ? "Connection: Connected" : "Connection: Disconnected"));
    }

    LLTextBox* battery_txt = getChild<LLTextBox>("battery_label");
    if (battery_txt)
    {
        battery_txt->setText(LLStringExplicit("Coyote Battery: " + std::to_string((int)server->getCoyoteBattery()) + "%"));
    }

    LLTextBox* load_a_txt = getChild<LLTextBox>("load_a_label");
    if (load_a_txt)
    {
        load_a_txt->setText(LLStringExplicit(server->getLoadA() ? "Channel A Load: Connected" : "Channel A Load: Disconnected"));
    }

    LLTextBox* load_b_txt = getChild<LLTextBox>("load_b_label");
    if (load_b_txt)
    {
        load_b_txt->setText(LLStringExplicit(server->getLoadB() ? "Channel B Load: Connected" : "Channel B Load: Disconnected"));
    }

    updateTriggerList();
    updateSensorList();
}

void LLFloaterEstim::updateTriggerList()
{
    if (!mTriggerList)
    {
        return;
    }

    auto server = LLEstimWSServer::getInstance();
    if (!server)
    {
        return;
    }

    mTriggerList->clearRows();

    const auto& triggers = server->getTriggers();
    for (const auto& rule : triggers)
    {
        LLSD element;
        element["columns"][0]["column"] = "rule";
        element["columns"][0]["type"]   = "text";
        element["columns"][0]["value"]  = rule.sensor + "." + rule.axis + " " + rule.op + " " + std::to_string((int)rule.threshold) + " -> " + rule.action;
        mTriggerList->addElement(element);
    }
}

void LLFloaterEstim::updateSensorList()
{
    if (!mSensorList)
    {
        return;
    }

    auto server = LLEstimWSServer::getInstance();
    if (!server)
    {
        return;
    }

    mSensorList->clearRows();

    const auto& sensor_values = server->getSensorValues();
    for (const auto& sensor_pair : sensor_values)
    {
        const std::string& sensor_name = sensor_pair.first;
        const auto& axes = sensor_pair.second;

        std::string val_str = "";
        for (const auto& axis_pair : axes)
        {
            if (!val_str.empty())
            {
                val_str += ", ";
            }
            if (axis_pair.first == "button")
            {
                val_str += "Button: " + std::string(axis_pair.second > 0 ? "Pressed" : "Released");
            }
            else if (axis_pair.first == "accel")
            {
                val_str += "Accel: " + llformat("%.1f", axis_pair.second);
            }
            else
            {
                std::string axis_name = axis_pair.first;
                if (!axis_name.empty()) axis_name[0] = std::toupper(axis_name[0]);
                val_str += axis_name + ": " + llformat("%.1f", axis_pair.second);
            }
        }

        LLSD element;
        element["columns"][0]["column"] = "sensor";
        element["columns"][0]["type"]   = "text";
        element["columns"][0]["value"]  = sensor_name;

        element["columns"][1]["column"] = "values";
        element["columns"][1]["type"]   = "text";
        element["columns"][1]["value"]  = val_str;

        mSensorList->addElement(element);
    }
}
