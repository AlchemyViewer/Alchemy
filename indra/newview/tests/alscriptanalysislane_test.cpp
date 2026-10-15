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

#include <catch2/catch_test_macros.hpp>

#include "Luau/Cancellation.h"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace
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

    TEST_CASE_METHOD(alscriptanalysislane_data, "alscriptanalysislane: a long check on one lane holds up nothing on the other", "[alscriptanalysislane]")
    {
        auto                 gate = std::make_shared<Held::Gate>();
        ALScriptAnalysisLane luau("TestLuau", [gate] { return std::make_unique<Held>(gate, false); }, main());
        ALScriptAnalysisLane lsl("TestLSL", [gate] { return std::make_unique<Held>(gate, false); }, main());
        CHECK(post(luau, job("slow", ALScriptAnalysis::Kind::Check, true)));
        CHECK(gate->waitEntered());
        CHECK(post(lsl, job("other", ALScriptAnalysis::Kind::Hover, true)));
        UNSCOPED_INFO("the other lane's question answered while the check is held");
        CHECK(answers.waitFor(1));
        CHECK(answers.all() == std::string("other/2"));
        gate->release();
        UNSCOPED_INFO("then the check");
        CHECK(answers.waitFor(2));
        CHECK(answers.all() == std::string("other/2 slow/0"));
        luau.close();
        lsl.close();
        UNSCOPED_INFO("closed: refused");
        CHECK_FALSE(post(luau, job("late", ALScriptAnalysis::Kind::Hover, true)));
    }

    TEST_CASE_METHOD(alscriptanalysislane_data, "alscriptanalysislane: another tab's check gives way to the front tab's question and reruns", "[alscriptanalysislane]")
    {
        auto                 gate = std::make_shared<Held::Gate>();
        ALScriptAnalysisLane luau("TestLuau", [gate] { return std::make_unique<Held>(gate, true); }, main());
        CHECK(post(luau, job("slow", ALScriptAnalysis::Kind::Check, false)));
        CHECK(gate->waitEntered());
        CHECK(post(luau, job("front", ALScriptAnalysis::Kind::Complete, true)));
        CHECK(answers.waitFor(2));
        UNSCOPED_INFO("the question first, then the check, run again");
        CHECK(answers.all() == std::string("front/1 slow/0"));
        luau.close();
    }

    TEST_CASE_METHOD(alscriptanalysislane_data, "alscriptanalysislane: the front tab's own check runs on through its questions", "[alscriptanalysislane]")
    {
        auto                 gate = std::make_shared<Held::Gate>();
        ALScriptAnalysisLane luau("TestLuau", [gate] { return std::make_unique<Held>(gate, true); }, main());
        CHECK(post(luau, job("slow", ALScriptAnalysis::Kind::Check, true)));
        CHECK(gate->waitEntered());
        CHECK(post(luau, job("front", ALScriptAnalysis::Kind::Complete, true)));
        gate->release();
        CHECK(answers.waitFor(2));
        UNSCOPED_INFO("the check first, not stopped");
        CHECK(answers.all() == std::string("slow/0 front/1"));
        luau.close();
    }

    TEST_CASE_METHOD(alscriptanalysislane_data, "alscriptanalysislane: another tab's check gives way once and its rerun holds the next question", "[alscriptanalysislane]")
    {
        auto gate    = std::make_shared<Held::Gate>();
        gate->always = true;
        ALScriptAnalysisLane luau("TestLuau", [gate] { return std::make_unique<Held>(gate, true); }, main());
        CHECK(post(luau, job("slow", ALScriptAnalysis::Kind::Check, false)));
        CHECK(gate->waitEntered());
        CHECK(post(luau, job("front", ALScriptAnalysis::Kind::Complete, true)));
        UNSCOPED_INFO("the question answered");
        CHECK(answers.waitFor(1));
        UNSCOPED_INFO("the check run again, and held");
        CHECK(gate->waitEntered(2));
        CHECK(post(luau, job("again", ALScriptAnalysis::Kind::Hover, true)));
        // A moment, in which a question that stopped it would be answered.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        UNSCOPED_INFO("the second question waits for the check");
        CHECK(answers.all() == std::string("front/1"));
        UNSCOPED_INFO("not started over");
        CHECK(gate->enteredNow() == 2);
        gate->release();
        CHECK(answers.waitFor(3));
        CHECK(answers.all() == std::string("front/1 slow/0 again/2"));
        luau.close();
    }

    TEST_CASE_METHOD(alscriptanalysislane_data, "alscriptanalysislane: a tab let go of mid-check has the check stopped unanswered and not rerun", "[alscriptanalysislane]")
    {
        auto                 gate = std::make_shared<Held::Gate>();
        ALScriptAnalysisLane luau("TestLuau", [gate] { return std::make_unique<Held>(gate, true); }, main());
        CHECK(post(luau, job("slow", ALScriptAnalysis::Kind::Check, false)));
        CHECK(gate->waitEntered());
        luau.forget("slow");
        CHECK(post(luau, job("front", ALScriptAnalysis::Kind::Hover, true)));
        UNSCOPED_INFO("the question answered");
        CHECK(answers.waitFor(1));
        gate->release();
        CHECK(post(luau, job("last", ALScriptAnalysis::Kind::Hover, true)));
        CHECK(answers.waitFor(2));
        UNSCOPED_INFO("nothing for the tab let go of");
        CHECK(answers.all() == std::string("front/2 last/2"));
        UNSCOPED_INFO("run once");
        CHECK(gate->enteredNow() == 1);
        luau.close();
    }

    TEST_CASE_METHOD(alscriptanalysislane_data, "alscriptanalysislane: lookups and other tabs' checks yield to the front tab but not LSL or weighs", "[alscriptanalysislane]")
    {
        using Kind = ALScriptAnalysis::Kind;
        UNSCOPED_INFO("another tab's check");
        CHECK(ALScriptAnalysisLane::yieldsOf(job("x", Kind::Check, false).request));
        UNSCOPED_INFO("a lookup's");
        CHECK(ALScriptAnalysisLane::yieldsOf(job("lookup:a:b", Kind::References, false).request));
        UNSCOPED_INFO("not the front tab's");
        CHECK_FALSE(ALScriptAnalysisLane::yieldsOf(job("x", Kind::Check, true).request));
        UNSCOPED_INFO("not a weigh");
        CHECK_FALSE(ALScriptAnalysisLane::yieldsOf(job("x", Kind::Weigh, false).request));
        ALScriptAnalysisLane::Job lsl = job("x", Kind::Check, false);
        lsl.request.lua               = false;
        UNSCOPED_INFO("not LSL's");
        CHECK_FALSE(ALScriptAnalysisLane::yieldsOf(lsl.request));

        auto                 gate = std::make_shared<Held::Gate>();
        ALScriptAnalysisLane luau("TestLuau", [gate] { return std::make_unique<Held>(gate, true); }, main());
        CHECK(post(luau, job("slow", Kind::References, false)));
        CHECK(gate->waitEntered());
        CHECK(post(luau, job("front", Kind::Complete, true)));
        CHECK(answers.waitFor(2));
        UNSCOPED_INFO("the question first, then the lookup's, run again");
        CHECK(answers.all() == std::string("front/1 slow/5"));
        luau.close();
    }
}
