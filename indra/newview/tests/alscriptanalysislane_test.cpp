/**
 * @file alscriptanalysislane_test.cpp
 * @brief One script engine's analysis thread, over analyzers made to wait.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
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

#include "linden_common.h"

#include "../alscriptanalysislane.h"
#include "../alscriptanalyzers.h"

#include "../test/lltut.h"

#include "Luau/Cancellation.h"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace tut
{
    // What the lanes answered, in the order they did, from their threads.
    struct Answers
    {
        std::mutex               mutex;
        std::condition_variable  changed;
        std::vector<std::string> said;

        void add(std::string one)
        {
            {
                const std::lock_guard<std::mutex> lock(mutex);
                said.push_back(std::move(one));
            }
            changed.notify_all();
        }

        // Whether `count` answers came within a few seconds.
        bool waitFor(size_t count)
        {
            std::unique_lock<std::mutex> lock(mutex);
            return changed.wait_for(lock, std::chrono::seconds(5), [&] { return said.size() >= count; });
        }

        std::string all()
        {
            const std::lock_guard<std::mutex> lock(mutex);
            std::string out;
            for (const std::string& one : said)
            {
                out += (out.empty() ? "" : " ") + one;
            }
            return out;
        }
    };

    // An analyzer that answers each question with its id, and holds a
    // question of the script "slow" until it is let go or, where `stops`,
    // stopped: what a long check is to whatever waits behind it.
    class Held final : public ALScriptAnalyzer
    {
    public:
        struct Gate
        {
            std::mutex              mutex;
            std::condition_variable changed;
            bool                    open    = false;
            // Held each time it is asked, not the first time alone: a check
            // that is slow however often it is started over.
            bool                    always  = false;
            int                     entered = 0;

            void release()
            {
                {
                    const std::lock_guard<std::mutex> lock(mutex);
                    open = true;
                }
                changed.notify_all();
            }

            bool waitEntered(int times = 1)
            {
                std::unique_lock<std::mutex> lock(mutex);
                return changed.wait_for(lock, std::chrono::seconds(5), [&] { return entered >= times; });
            }

            int enteredNow()
            {
                const std::lock_guard<std::mutex> lock(mutex);
                return entered;
            }
        };

        Held(std::shared_ptr<Gate> gate, bool stops)
        :   mGate(std::move(gate))
        ,   mStops(stops)
        {
        }

        void answer(const Request& request, const std::string&, const Setup& setup, Result&) override
        {
            mStopped = false;
            if (request.id != "slow")
            {
                return;
            }
            std::unique_lock<std::mutex> lock(mGate->mutex);
            ++mGate->entered;
            mGate->changed.notify_all();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            // Held once, unless always: run again, it answers at once.
            while (!mGate->open && (mGate->always || mGate->entered == 1) && std::chrono::steady_clock::now() < deadline)
            {
                if (mStops && setup.stop && setup.stop->requested())
                {
                    mStopped = true;
                    return;
                }
                mGate->changed.wait_for(lock, std::chrono::milliseconds(5));
            }
        }
        bool stopped() const override { return mStopped; }
        void forgetStop() override { mStopped = false; }

    private:
        std::shared_ptr<Gate> mGate;
        bool                  mStops   = false;
        bool                  mStopped = false;
    };

    struct alscriptanalysislane_data
    {
        Answers answers;

        ALScriptAnalysisLane::Main main()
        {
            return ALScriptAnalysisLane::Main{ [this](ALScriptAnalysisLane::Result result, std::shared_ptr<ALScriptAnalysisLane::callback_t>) {
                                                   answers.add(result.id + "/" + std::to_string(static_cast<int>(result.kind)));
                                               },
                                               [](std::function<void()> done) { done(); } };
        }

        static ALScriptAnalysisLane::Job job(const std::string& id, ALScriptAnalysis::Kind kind, bool front)
        {
            ALScriptAnalysisLane::Job out;
            out.request.id      = id;
            out.request.kind    = kind;
            out.request.front   = front;
            out.request.lua     = true;
            out.request.version = 1;
            out.callback        = std::make_shared<ALScriptAnalysisLane::callback_t>([](const ALScriptAnalysisLane::Result&) {});
            return out;
        }

        // As ALScriptAnalysis::ask posts it.
        static bool post(ALScriptAnalysisLane& lane, ALScriptAnalysisLane::Job one)
        {
            const std::string key   = ALScriptAnalysisLane::keyOf(one.request);
            const U8          rank  = ALScriptAnalysisLane::rankOf(one.request);
            const bool        gives = ALScriptAnalysisLane::yieldsOf(one.request);
            return lane.post(key, rank, gives, std::move(one));
        }
    };

    typedef test_group<alscriptanalysislane_data> alscriptanalysislane_group;
    typedef alscriptanalysislane_group::object    alscriptanalysislane_object;
    alscriptanalysislane_group                    alscriptanalysislane_instance("alscriptanalysislane");

    template<> template<>
    void alscriptanalysislane_object::test<1>()
    {
        set_test_name("a long check on one lane holds nothing of the other's: each engine its own thread and queue");
        auto                 gate = std::make_shared<Held::Gate>();
        ALScriptAnalysisLane luau("TestLuau", [gate] { return std::make_unique<Held>(gate, false); }, main());
        ALScriptAnalysisLane lsl("TestLSL", [gate] { return std::make_unique<Held>(gate, false); }, main());
        ensure("taken", post(luau, job("slow", ALScriptAnalysis::Kind::Check, true)));
        ensure("held", gate->waitEntered());
        ensure("taken too", post(lsl, job("other", ALScriptAnalysis::Kind::Hover, true)));
        ensure("the other lane's question answered while the check is held", answers.waitFor(1));
        ensure_equals("it alone", answers.all(), std::string("other/2"));
        gate->release();
        ensure("then the check", answers.waitFor(2));
        ensure_equals("in that order", answers.all(), std::string("other/2 slow/0"));
        luau.close();
        lsl.close();
        ensure("closed: refused", !post(luau, job("late", ALScriptAnalysis::Kind::Hover, true)));
    }

    template<> template<>
    void alscriptanalysislane_object::test<2>()
    {
        set_test_name("another tab's check gives way to a question of the tab in front, waits again behind it, and answers after");
        auto                 gate = std::make_shared<Held::Gate>();
        ALScriptAnalysisLane luau("TestLuau", [gate] { return std::make_unique<Held>(gate, true); }, main());
        ensure("taken", post(luau, job("slow", ALScriptAnalysis::Kind::Check, false)));
        ensure("held", gate->waitEntered());
        ensure("asked", post(luau, job("front", ALScriptAnalysis::Kind::Complete, true)));
        ensure("both answered", answers.waitFor(2));
        ensure_equals("the question first, then the check, run again", answers.all(), std::string("front/1 slow/0"));
        luau.close();
    }

    template<> template<>
    void alscriptanalysislane_object::test<3>()
    {
        set_test_name("the front tab's own check runs on through its questions, and is answered as it was asked");
        auto                 gate = std::make_shared<Held::Gate>();
        ALScriptAnalysisLane luau("TestLuau", [gate] { return std::make_unique<Held>(gate, true); }, main());
        ensure("taken", post(luau, job("slow", ALScriptAnalysis::Kind::Check, true)));
        ensure("held", gate->waitEntered());
        ensure("asked", post(luau, job("front", ALScriptAnalysis::Kind::Complete, true)));
        gate->release();
        ensure("both answered", answers.waitFor(2));
        ensure_equals("the check first, not stopped", answers.all(), std::string("slow/0 front/1"));
        luau.close();
    }

    template<> template<>
    void alscriptanalysislane_object::test<4>()
    {
        set_test_name("another tab's check gives way once: run again, it runs through the next question of the tab in front, which waits for it");
        auto gate    = std::make_shared<Held::Gate>();
        gate->always = true;
        ALScriptAnalysisLane luau("TestLuau", [gate] { return std::make_unique<Held>(gate, true); }, main());
        ensure("taken", post(luau, job("slow", ALScriptAnalysis::Kind::Check, false)));
        ensure("held", gate->waitEntered());
        ensure("asked", post(luau, job("front", ALScriptAnalysis::Kind::Complete, true)));
        ensure("the question answered", answers.waitFor(1));
        ensure("the check run again, and held", gate->waitEntered(2));
        ensure("asked again", post(luau, job("again", ALScriptAnalysis::Kind::Hover, true)));
        // A moment, in which a question that stopped it would be answered.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        ensure_equals("the second question waits for the check", answers.all(), std::string("front/1"));
        ensure_equals("not started over", gate->enteredNow(), 2);
        gate->release();
        ensure("all answered", answers.waitFor(3));
        ensure_equals("the check, then the question", answers.all(), std::string("front/1 slow/0 again/2"));
        luau.close();
    }

    template<> template<>
    void alscriptanalysislane_object::test<5>()
    {
        set_test_name("a tab let go of while its check runs: the check stopped, answered to nobody, and not run again");
        auto                 gate = std::make_shared<Held::Gate>();
        ALScriptAnalysisLane luau("TestLuau", [gate] { return std::make_unique<Held>(gate, true); }, main());
        ensure("taken", post(luau, job("slow", ALScriptAnalysis::Kind::Check, false)));
        ensure("held", gate->waitEntered());
        luau.forget("slow");
        ensure("asked", post(luau, job("front", ALScriptAnalysis::Kind::Hover, true)));
        ensure("the question answered", answers.waitFor(1));
        gate->release();
        ensure("asked again", post(luau, job("last", ALScriptAnalysis::Kind::Hover, true)));
        ensure("answered", answers.waitFor(2));
        ensure_equals("nothing for the tab let go of", answers.all(), std::string("front/2 last/2"));
        ensure_equals("run once", gate->enteredNow(), 1);
        luau.close();
    }

    template<> template<>
    void alscriptanalysislane_object::test<6>()
    {
        set_test_name("a lookup's question of another script gives way to the tab in front as another tab's check does; the front tab's own, LSL's and a weigh do not");
        using Kind = ALScriptAnalysis::Kind;
        ensure("another tab's check", ALScriptAnalysisLane::yieldsOf(job("x", Kind::Check, false).request));
        ensure("a lookup's", ALScriptAnalysisLane::yieldsOf(job("lookup:a:b", Kind::References, false).request));
        ensure("not the front tab's", !ALScriptAnalysisLane::yieldsOf(job("x", Kind::Check, true).request));
        ensure("not a weigh", !ALScriptAnalysisLane::yieldsOf(job("x", Kind::Weigh, false).request));
        ALScriptAnalysisLane::Job lsl = job("x", Kind::Check, false);
        lsl.request.lua               = false;
        ensure("not LSL's", !ALScriptAnalysisLane::yieldsOf(lsl.request));

        auto                 gate = std::make_shared<Held::Gate>();
        ALScriptAnalysisLane luau("TestLuau", [gate] { return std::make_unique<Held>(gate, true); }, main());
        ensure("taken", post(luau, job("slow", Kind::References, false)));
        ensure("held", gate->waitEntered());
        ensure("asked", post(luau, job("front", Kind::Complete, true)));
        ensure("both answered", answers.waitFor(2));
        ensure_equals("the question first, then the lookup's, run again", answers.all(), std::string("front/1 slow/5"));
        luau.close();
    }
}
