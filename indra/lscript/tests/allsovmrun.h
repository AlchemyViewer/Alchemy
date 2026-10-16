/**
 * @file allsovmrun.h
 * @brief LL's LSO VM run on a script in a test, with llSay heard
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

#include "../allslreference.h"
#include "../lscript_execute.h"
#include "../lscript_library.h"
#include "allsolibrary.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace ll_test_lso
{
    // A library function answered by a stand-in while this lives, and as it
    // was after, however the test ends: the library is the process's. Put in
    // after a TestLibrary, which would otherwise answer for it.
    struct StandIn
    {
        using exec_t = decltype(LLScriptLibraryFunction::mExecFunc);
        StandIn(LLScriptLibraryFunction& function, exec_t by)
            : mFunction(function),
              mWas(function.mExecFunc)
        {
            mFunction.mExecFunc = by;
        }
        ~StandIn() { mFunction.mExecFunc = mWas; }
        StandIn(const StandIn&)            = delete;
        StandIn& operator=(const StandIn&) = delete;
        LLScriptLibraryFunction& mFunction;
        const exec_t             mWas;
    };

    // An event's arguments, in order, made field by field when the event is:
    // LLScriptLibData assigned copies its strings' pointers, so one assigned
    // from a temporary would hold what the temporary freed.
    class EventArgs
    {
    public:
        EventArgs& integer(S32 value)
        {
            mFill.push_back([value](LLScriptLibData& d) { d.mType = LST_INTEGER; d.mInteger = value; });
            return *this;
        }
        EventArgs& floating(F32 value)
        {
            mFill.push_back([value](LLScriptLibData& d) { d.mType = LST_FLOATINGPOINT; d.mFP = value; });
            return *this;
        }
        EventArgs& string(const std::string& value)
        {
            mFill.push_back([value](LLScriptLibData& d) { d.mType = LST_STRING; d.mString = copy(value); });
            return *this;
        }
        EventArgs& key(const LLUUID& value)
        {
            mFill.push_back([value](LLScriptLibData& d) { d.mType = LST_KEY; d.mKey = copy(value.asString()); });
            return *this;
        }
        EventArgs& vector(const LLVector3& value)
        {
            mFill.push_back([value](LLScriptLibData& d) { d.mType = LST_VECTOR; d.mVec = value; });
            return *this;
        }

        // Owned by the VM once added, which deletes the arguments with it.
        // The VM reads them up to one of no type, which the simulator ends
        // them with too: one more, left as made.
        LLScriptDataCollection* make(LSCRIPTStateEventType type) const
        {
            LLScriptLibData* data = new LLScriptLibData[mFill.size() + 1];
            for (size_t i = 0; i < mFill.size(); ++i)
            {
                mFill[i](data[i]);
            }
            return new LLScriptDataCollection(type, data);
        }

    private:
        static char* copy(const std::string& text)
        {
            char* out = new char[text.size() + 1];
            std::memcpy(out, text.c_str(), text.size() + 1);
            return out;
        }

        std::vector<std::function<void(LLScriptLibData&)>> mFill;
    };

    struct Event
    {
        LSCRIPTStateEventType type;
        EventArgs             args;
    };

    // What a run said, the fault that stopped it if one did, how many
    // events it handled, and whether it was stopped by the step cap instead
    // of settling: a script that spins, which no test's should unless it
    // says so.
    struct Run
    {
        std::vector<std::string> said;
        std::string              error;
        U32                      events = 0;
        bool                     capped = false;
    };

    // Whether a run that hits the step cap fails the test: it does, unless
    // the test says the script spins.
    enum class Spin
    {
        Fails,
        Allowed
    };

    // How many of the VM's steps state_entry and each event after it get:
    // more than any test's script needs, so reaching it is a script that
    // never settles.
    constexpr U32 SETTLE_STEPS = 1000000;

    // The script compiled to LSO, which REQUIREs that it compiles.
    inline std::vector<U8> compileLSO(const std::string& text)
    {
        const ALLSLReference::Result lso = ALLSLReference::compile(text, ALLSLReference::Target::LSO);
        INFO("LSO: " << lso.messages);
        REQUIRE(lso.ok);
        return lso.image;
    }

    // The VM run an instruction at a time until it is idle -- no handler
    // running, no state change pending, nothing queued, and no event raised
    // that the state handles (one it does not, such as a state_entry it has
    // no handler for, stays raised) -- or has run a fault, or SETTLE_STEPS
    // instructions have gone by. Not by runQuanta, which spins out the whole
    // of its time slice once a fault stops the script. True when the VM
    // settled or faulted; false when the cap stopped it, which a test takes
    // as a script that hangs unless it expects one.
    [[nodiscard]] inline bool settle(LLScriptExecuteLSL2& vm, const char*& error, U32& events)
    {
        for (U32 step = 0; step < SETTLE_STEPS && !error; ++step)
        {
            vm.runInstructions(FALSE, LLUUID::null, &error, events, 1.f);
            if (vm.isFinished() && !vm.isStateChangePending() && vm.getEventCount() == 0 &&
                (vm.getCurrentEvents() & vm.getEventHandlers()) == 0)
            {
                return true;
            }
        }
        return error != nullptr;
    }

    // An LSO image run: state_entry, and then each event added and run in
    // turn, each until the VM settles, with the test library. A run the
    // step cap stopped is capped, handles no later event, and fails the
    // test unless spin says the script is expected to.
    inline Run runLSOImage(const std::vector<U8>& image, const std::vector<Event>& events = {}, Spin spin = Spin::Fails)
    {
        Run run;
        gSaid.clear();
        {
            const TestLibrary   library;
            LLScriptExecuteLSL2 vm(image.data(), static_cast<U32>(image.size()));
            const char*         error = nullptr;
            run.capped                = !settle(vm, error, run.events);
            for (const Event& event : events)
            {
                if (error || run.capped)
                {
                    break;
                }
                vm.addEvent(event.args.make(event.type));
                run.capped = !settle(vm, error, run.events);
            }
            run.error = error ? error : "";
        }
        run.said = std::move(gSaid);
        gSaid.clear();
        if (spin == Spin::Fails)
        {
            INFO("the VM ran " << SETTLE_STEPS << " instructions without settling: the script hangs");
            CHECK_FALSE(run.capped);
        }
        return run;
    }

    // The script compiled to LSO and run as above.
    inline Run runLSO(const std::string& text, const std::vector<Event>& events = {}, Spin spin = Spin::Fails)
    {
        return runLSOImage(compileLSO(text), events, spin);
    }
}
