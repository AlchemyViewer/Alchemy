/**
 * @file allsofuzz.h
 * @brief What the lscript fuzzers share: a script compiled by LL's compiler
 * and run by LL's VM, capped
 *
 * Copyright (c) 2026, Alchemy Viewer Project.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-licence.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 *
 */

#pragma once

#include "linden_common.h"

#include "../allslreference.h"
#include "../lscript_byteformat.h"
#include "../lscript_execute.h"
#include "../tests/allsolibrary.h"
#include "llerrorcontrol.h"
#include "llrand.h"

#include <cstring>
#include <memory>
#include <string>

namespace ll_fuzz_lso
{
    // Once for the process: warnings quiet, and the test library in for
    // good. An LL_ERRS is left to crash, as it would the grid's: a script's
    // author can raise one from what they write, so it is a finding.
    inline void setUp()
    {
        static std::unique_ptr<ll_test_lso::TestLibrary> library;
        if (library)
        {
            return;
        }
        LLError::initForApplication("", "", true);
        LLError::setDefaultLevel(LLError::LEVEL_ERROR);
        library = std::make_unique<ll_test_lso::TestLibrary>();
    }

    // How many of the VM's steps state_entry and each event after it get:
    // more than any script the seeds hold needs, and a bound on one that
    // never stops.
    constexpr U32 STEPS = 50000;

    // Until idle, as the tests' settle() is (tests/allsovmrun.h): true when
    // the VM settled or faulted, false when STEPS ran out, a script that
    // spins.
    [[nodiscard]] inline bool steps(LLScriptExecuteLSL2& vm, const char*& error, U32& events)
    {
        for (U32 i = 0; i < STEPS && !error; ++i)
        {
            vm.runInstructions(FALSE, LLUUID::null, &error, events, 1.f);
            if (vm.isFinished() && !vm.isStateChangePending() && vm.getEventCount() == 0 && (vm.getCurrentEvents() & vm.getEventHandlers()) == 0)
            {
                return true;
            }
        }
        return error != nullptr;
    }

    // An event's arguments, ended by one of no type as the VM reads them.
    inline LLScriptLibData* arguments(const LSCRIPTType* types, size_t count)
    {
        LLScriptLibData* data = new LLScriptLibData[count + 1];
        for (size_t i = 0; i < count; ++i)
        {
            LLScriptLibData& d = data[i];
            d.mType            = types[i];
            switch (types[i])
            {
            case LST_INTEGER: d.mInteger = 3; break;
            case LST_FLOATINGPOINT: d.mFP = 1.5f; break;
            case LST_STRING: d.mString = new char[3]{ 'h', 'i', 0 }; break;
            case LST_KEY:
            {
                static const char* const ID = "00000000-0000-0000-0000-00000000000a";
                d.mKey                      = new char[std::strlen(ID) + 1];
                std::strcpy(d.mKey, ID);
                break;
            }
            case LST_VECTOR: d.mVec.setVec(1.f, 2.f, 3.f); break;
            case LST_QUATERNION: d.mQuat = LLQuaternion(); break;
            default: break;
            }
        }
        return data;
    }

    // The events a run sends, each where the state it is in has a handler
    // for it, with the arguments the grid gives each.
    struct Kind
    {
        LSCRIPTStateEventType type;
        LSCRIPTType           args[4];
        size_t                count;
    };
    inline constexpr Kind KINDS[] = {
        { LSTT_TOUCH_START, { LST_INTEGER }, 1 },
        { LSTT_TIMER, {}, 0 },
        { LSTT_CHAT, { LST_INTEGER, LST_STRING, LST_KEY, LST_STRING }, 4 },
        { LSTT_LINK_MESSAGE, { LST_INTEGER, LST_INTEGER, LST_STRING, LST_KEY }, 4 },
        { LSTT_AT_TARGET, { LST_INTEGER, LST_VECTOR, LST_VECTOR }, 3 },
        { LSTT_AT_ROT_TARGET, { LST_INTEGER, LST_QUATERNION, LST_QUATERNION }, 3 },
        { LSTT_MONEY, { LST_KEY, LST_INTEGER }, 2 },
        { LSTT_COLLISION_START, { LST_INTEGER }, 1 },
        { LSTT_TOUCH_START, { LST_INTEGER }, 1 },
    };

    // What came of a script: what the compiler said if it would not compile
    // it, the fault that ended its run if one did, and whether the run was
    // stopped by its steps running out rather than settling. A script that
    // spins is no finding -- its author may write one, and the grid gives
    // it time slices for ever -- but a run that was cut short is not a clean
    // one, and is said so.
    struct Outcome
    {
        bool        compiled = false;
        std::string messages;
        std::string fault;
        bool        capped = false;
    };

    // The image run: state_entry, then each of KINDS its state handles,
    // each until the VM settles or its steps run out, after which no more
    // are sent. The fault that ended it, if one did, and whether the steps
    // ran out, into the outcome.
    inline void run(const U8* image, U32 size, Outcome& outcome)
    {
        ll_rand_seed(0);
        LLScriptExecuteLSL2 vm(image, size);
        const char*         error  = nullptr;
        U32                 events = 0;
        outcome.capped             = !steps(vm, error, events);
        for (const Kind& kind : KINDS)
        {
            if (error || outcome.capped)
            {
                break;
            }
            if (vm.getEventHandlers() & LSCRIPTStateBitField[kind.type])
            {
                vm.addEvent(new LLScriptDataCollection(kind.type, arguments(kind.args, kind.count)));
                outcome.capped = !steps(vm, error, events);
            }
        }
        ll_test_lso::gSaid.clear();
        outcome.fault = error ? error : "";
    }

    // The script as the grid takes it: compiled to LSO and run, and compiled
    // to CIL, which no VM here runs but whose emitting is LL's code too.
    inline Outcome compileAndRun(const std::string& text)
    {
        Outcome                      outcome;
        const ALLSLReference::Result lso = ALLSLReference::compile(text, ALLSLReference::Target::LSO);
        outcome.compiled                 = lso.ok;
        if (!lso.ok)
        {
            outcome.messages = lso.messages;
            return outcome;
        }
        run(lso.image.data(), static_cast<U32>(lso.image.size()), outcome);
        ALLSLReference::compile(text, ALLSLReference::Target::CIL);
        return outcome;
    }
}
