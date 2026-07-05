/**
 * @file llestimrlvhandler.h
 * @brief RLV handler for native e-stim commands
 */

#pragma once

#include "rlvcommon.h"

class LLEstimRLVHandler : public RlvExtCommandHandler
{
public:
    LLEstimRLVHandler() = default;
    ~LLEstimRLVHandler() override = default;

    bool onForceCommand(const RlvCommand& rlvCmd, ERlvCmdRet& cmdRet) override;
    bool onAddRemCommand(const RlvCommand& rlvCmd, ERlvCmdRet& cmdRet) override;
    bool onReplyCommand(const RlvCommand& rlvCmd, ERlvCmdRet& cmdRet) override;
    bool onClearCommand(const RlvCommand& rlvCmd, ERlvCmdRet& cmdRet) override;
};
