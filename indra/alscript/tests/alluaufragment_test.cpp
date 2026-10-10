/**
 * @file alluaufragment_test.cpp
 * @brief SLua completion and signature help over a fragment, against the same over the whole script, across a corpus.
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

#include "../luau/alluauservice.h"
#include "../lsl/allslservice.h"
#include "../lsl/allsltoslua.h"
#include "../preprocessor/alpreprocessor.h"

#include "../test/lltut.h"
#include "llsdserialize.h"

#include "Luau/Allocator.h"
#include "Luau/Ast.h"
#include "Luau/Lexer.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>

// What the studio is answered as a script is typed, over the statement
// being typed checked alone against the script's last check (ALLuauFragment),
// set against what the whole script checked again answers, at the places a
// scripter asks: after `.` and `:`, at the start of a name, at a call's `(`
// and `,`. Over every SLua script there is to hand: the studio's templates,
// Tailslide's test scripts written again as SLua, and a script expanded by
// the preprocessor, with an include changed since its last check.
//
// Each place's text is the script as typed so far, and the last check's the
// script with that line not yet typed: the analysis thread's check, then its
// warm job (ALLuauService::warm). Where the fragment declines, the whole
// script answers both, and the two agree by construction; what is compared
// is where it answers.
namespace tut
{
    struct alluaufragment_data
    {
        ALLuauService fragment;
        ALLuauService whole;
        std::string   definitions;
        std::string   error;
        bool          loaded = false;
        // Luau's new type solver, where the run asks for it: CTest runs
        // these twice, the second time with AL_TEST_LUAU_SOLVER=new.
        const bool    newSolver = getenv("AL_TEST_LUAU_SOLVER") && std::string(getenv("AL_TEST_LUAU_SOLVER")) == "new";

        alluaufragment_data()
        {
            definitions = readFile(std::string(AL_LSL_DEFINITIONS_DIR) + "/secondlife.d.luau");
            fragment.setNewSolver(newSolver, error);
            whole.setNewSolver(newSolver, error);
            loaded = fragment.loadDefinitions(definitions, error) && whole.loadDefinitions(definitions, error);
            const std::string docs = readFile(std::string(AL_LSL_DEFINITIONS_DIR) + "/secondlife.docs.json");
            fragment.loadDocs(docs, error);
            whole.loadDocs(docs, error);
            fragment.setFragments(true);
        }

        static std::string readFile(const std::string& path)
        {
            llifstream        in(path, std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            return text.str();
        }

        // A question at a place of a text, and the text as the last check
        // had it.
        struct Probe
        {
            bool        signature = false;
            std::string text;
            std::string base;
            S32         line   = 0;
            S32         column = 0;
            std::string what;
        };

        // The places of a script a scripter asks at, as the studio asks:
        // after each `.`, and each `:` that calls; at the start of a name,
        // the name typed; at each `(` and `,` of a call. At most `each` of
        // each kind, spread through the script.
        //
        // On a line that is one statement with no block of its own, the
        // line is being typed new: the last check had no such line, and
        // the text has it as far as the place -- after `.`, nothing more;
        // a name, through the name; a call, whole. On any other line it is
        // edited in place, the rest of it there, as a block's opening line
        // is: after `.`, the name that followed it deleted since the check;
        // a name, its last letter typed since; a call, the line changed
        // since (indented).
        static std::vector<Probe> probesOf(const std::string& script, size_t each)
        {
            std::vector<size_t> starts{ 0 };
            for (size_t i = 0; i < script.size(); ++i)
            {
                if (script[i] == '\n')
                {
                    starts.push_back(i + 1);
                }
            }
            const auto offset  = [&](Luau::Position at) { return starts[at.line] + at.column; };
            const auto lineEnd = [&](unsigned line) {
                const size_t end = script.find('\n', starts[line]);
                return end == std::string::npos ? script.size() : end;
            };
            // The script before a line was typed: without it, the lines
            // after it a line further up, as an editor has them.
            const auto without = [&](unsigned line) {
                const size_t end = lineEnd(line);
                return script.substr(0, starts[line]) + script.substr(end < script.size() ? end + 1 : end);
            };
            const auto cut   = [&](Luau::Position at) { return script.substr(0, offset(at)) + script.substr(lineEnd(at.line)); };

            struct Token
            {
                Luau::Lexeme::Type type;
                Luau::Location     location;
            };
            std::vector<Token> tokens;
            Luau::Allocator    allocator;
            Luau::AstNameTable names(allocator);
            Luau::Lexer        lexer(script.data(), script.size(), names);
            lexer.setSkipComments(true);
            for (const Luau::Lexeme* token = &lexer.next(); token->type != Luau::Lexeme::Eof; token = &lexer.next())
            {
                tokens.push_back(Token{ token->type, token->location });
            }
            // Which lines are one statement with no block: no token of a
            // block, no token over lines, and their brackets closed.
            std::vector<bool> simple(starts.size(), true);
            std::vector<int>  depth(starts.size(), 0);
            for (const Token& token : tokens)
            {
                const unsigned line = token.location.begin.line;
                switch (static_cast<int>(token.type))
                {
                    case Luau::Lexeme::ReservedFunction:
                    case Luau::Lexeme::ReservedThen:
                    case Luau::Lexeme::ReservedDo:
                    case Luau::Lexeme::ReservedEnd:
                    case Luau::Lexeme::ReservedRepeat:
                    case Luau::Lexeme::ReservedUntil:
                    case Luau::Lexeme::ReservedElse:
                    case Luau::Lexeme::ReservedElseif:
                        simple[line] = false;
                        break;
                    case '(':
                    case '[':
                    case '{':
                        ++depth[line];
                        break;
                    case ')':
                    case ']':
                    case '}':
                        --depth[line];
                        break;
                    default:
                        break;
                }
                if (token.location.end.line != line)
                {
                    simple[line] = false;
                }
            }
            for (size_t line = 0; line < simple.size(); ++line)
            {
                simple[line] = simple[line] && depth[line] == 0;
            }

            std::vector<Probe> indexes, words, calls;
            std::vector<bool>  open;
            // Whether the names here are being declared -- a `local`'s or a
            // `for`'s list, a function's parameters -- where nothing is
            // offered that means anything.
            bool     declaring  = false;
            bool     parameters = false;
            unsigned declared   = 0;
            for (size_t i = 0; i < tokens.size(); ++i)
            {
                {
                    const Luau::Lexeme::Type type = tokens[i].type;
                    const unsigned           line = tokens[i].location.begin.line;
                    if (line != declared || type == '=' || type == Luau::Lexeme::ReservedIn)
                    {
                        declaring = false;
                    }
                    if (type == Luau::Lexeme::ReservedLocal || type == Luau::Lexeme::ReservedFor)
                    {
                        declaring = true;
                        declared  = line;
                    }
                    if (type == Luau::Lexeme::ReservedFunction)
                    {
                        parameters = true;
                    }
                    else if (type == ')')
                    {
                        parameters = false;
                    }
                }
                const Token&             token = tokens[i];
                const Luau::Lexeme::Type type  = token.type;
                const Luau::Lexeme::Type prev  = i > 0 ? tokens[i - 1].type : Luau::Lexeme::Eof;
                const Luau::Lexeme::Type next  = i + 1 < tokens.size() ? tokens[i + 1].type : Luau::Lexeme::Eof;
                const Luau::Lexeme::Type after = i + 2 < tokens.size() ? tokens[i + 2].type : Luau::Lexeme::Eof;
                const bool               value = prev == Luau::Lexeme::Name || prev == ')' || prev == ']';
                const unsigned           line  = token.location.begin.line;
                if ((type == '.' && value && next == Luau::Lexeme::Name) ||
                    (type == ':' && value && next == Luau::Lexeme::Name && (after == '(' || after == '{' || after == Luau::Lexeme::QuotedString)))
                {
                    Probe probe;
                    probe.line   = static_cast<S32>(token.location.end.line);
                    probe.column = static_cast<S32>(token.location.end.column);
                    if (simple[line])
                    {
                        probe.text = cut(token.location.end);
                        probe.base = without(line);
                        probe.what = std::string("a new line, after ") + static_cast<char>(type);
                    }
                    else
                    {
                        probe.text = script.substr(0, offset(token.location.end)) + script.substr(offset(tokens[i + 1].location.end));
                        probe.base = script;
                        probe.what = std::string("a name after ") + static_cast<char>(type) + " deleted";
                    }
                    indexes.push_back(std::move(probe));
                }
                if (type == Luau::Lexeme::Name && prev != '.' && prev != ':' && !declaring && !parameters)
                {
                    Probe probe;
                    probe.line   = static_cast<S32>(token.location.begin.line);
                    probe.column = static_cast<S32>(token.location.begin.column);
                    if (simple[line])
                    {
                        probe.text = cut(token.location.end);
                        probe.base = without(line);
                        probe.what = "a new line, a name";
                    }
                    else
                    {
                        probe.text = script;
                        probe.base = script;
                        probe.base.erase(offset(token.location.end) - 1, 1);
                        probe.what = "a name's last letter typed";
                    }
                    words.push_back(std::move(probe));
                }
                if (type == '(' || type == '[' || type == '{')
                {
                    open.push_back(type == '(' && value);
                }
                else if ((type == ')' || type == ']' || type == '}') && !open.empty())
                {
                    open.pop_back();
                }
                if ((type == '(' && value) || (type == ',' && !open.empty() && open.back()))
                {
                    Probe probe;
                    probe.signature = true;
                    probe.text      = script;
                    probe.line      = static_cast<S32>(token.location.end.line);
                    probe.column    = static_cast<S32>(token.location.end.column);
                    if (simple[line])
                    {
                        probe.base = without(line);
                        probe.what = std::string("a new line, signature after ") + static_cast<char>(type);
                    }
                    else
                    {
                        probe.base = script;
                        probe.base.insert(starts[line], " ");
                        probe.what = std::string("a line changed, signature after ") + static_cast<char>(type);
                    }
                    calls.push_back(std::move(probe));
                }
            }
            std::vector<Probe> out;
            for (std::vector<Probe>* kind : { &indexes, &words, &calls })
            {
                const size_t step = std::max<size_t>(1, kind->size() / each);
                for (size_t i = 0; i < kind->size() && out.size() < each * 3; i += step)
                {
                    out.push_back(std::move((*kind)[i]));
                }
            }
            return out;
        }

        // What two answers say differently, judged. A fault fails the test:
        // names the whole script offers missing, or ones it does not offer
        // added, past a few; an entry of another kind, place, or
        // deprecation; brackets put elsewhere for an entry both say fits or
        // not alike; signature help found by one alone, or of another
        // label, argument or form. What is counted is inherent to a
        // fragment, checked against the last check rather than the text:
        // - a name's type beside it, which is the last check's where the
        //   line typed adds a use the whole script's check infers from;
        // - which entries fit, from the same types; under the old solver,
        //   by the new solver's rules, which check every fragment -- and so
        //   there where a call's brackets go and what the place is said to
        //   be, which follow from them;
        // - a few names offered by one alone: a local in its own
        //   initializer, the last check's binding of the line typed, or a
        //   table's field from its uses there;
        // - under the old solver, a function's signature with its
        //   parameters said otherwise: a generic as it is declared, where
        //   the old solver's whole check instantiates it at the call, or a
        //   parameter's type inferred from the last check's uses.
        struct Told
        {
            std::string faults;
            bool        types    = false;
            bool        fits     = false;
            bool        few      = false;
            bool        generics = false;
        };

        Told judged(const std::vector<ALScriptCompletion>& over, const std::vector<ALScriptCompletion>& all) const
        {
            Told                                             told;
            std::map<std::string, const ALScriptCompletion*> a, b;
            for (const ALScriptCompletion& c : over)
            {
                a[c.text + "\x1f" + c.snippet] = &c;
            }
            for (const ALScriptCompletion& c : all)
            {
                b[c.text + "\x1f" + c.snippet] = &c;
            }
            std::string added, missing;
            size_t      plus = 0, minus = 0;
            for (const auto& [key, one] : a)
            {
                if (!b.contains(key))
                {
                    added += " +" + one->text;
                    ++plus;
                }
            }
            for (const auto& [key, one] : b)
            {
                if (!a.contains(key))
                {
                    missing += " -" + one->text;
                    ++minus;
                }
            }
            if (plus > std::max<size_t>(2, b.size() / 10) || minus > std::max<size_t>(2, b.size() / 10))
            {
                told.faults += (added + missing).substr(0, 400);
            }
            told.few = plus + minus > 0;
            for (const auto& [key, one] : a)
            {
                const auto other = b.find(key);
                if (other == b.end())
                {
                    continue;
                }
                const ALScriptCompletion& two = *other->second;
                if (one->fits != two.fits)
                {
                    told.fits = true;
                }
                // Under the old solver, where a call's brackets go and the
                // place said follow the new solver's rules too.
                else if (!newSolver && (one->brackets != two.brackets || one->context != two.context))
                {
                    told.fits = true;
                }
                else if (one->brackets != two.brackets)
                {
                    told.faults += llformat(" ~%s(brackets %d|%d)", one->text.c_str(), int(one->brackets), int(two.brackets));
                }
                if (one->kind != two.kind || one->deprecated != two.deprecated || (newSolver && one->context != two.context))
                {
                    told.faults += llformat(" ~%s(kind %d|%d, context %d|%d)", one->text.c_str(), int(one->kind), int(two.kind), int(one->context),
                                            int(two.context));
                }
                if (one->detail != two.detail || one->documentation != two.documentation)
                {
                    told.types = true;
                }
            }
            return told;
        }

        Told judged(const ALScriptSignature& a, const ALScriptSignature& b) const
        {
            Told told;
            if (a.found != b.found)
            {
                told.faults = llformat(" found %d | %d (%s)", a.found, b.found, (a.found ? a.label : b.label).c_str());
                return told;
            }
            if (a.label != b.label || a.parameters != b.parameters)
            {
                // The same function, its parameters said otherwise: under the
                // old solver, a generic as declared, or a parameter's type
                // inferred from the last check's uses.
                const auto name = [](const std::string& label) { return label.substr(0, label.find_first_of("<(")); };
                if (!newSolver && name(a.label) == name(b.label) && a.parameters.size() == b.parameters.size())
                {
                    told.generics = true;
                }
                else
                {
                    told.faults += " label(" + a.label + " | " + b.label + ")";
                }
            }
            if (a.active != b.active)
            {
                told.faults += llformat(" active(%d | %d)", a.active, b.active);
            }
            if (a.overload != b.overload || a.overloads.size() != b.overloads.size())
            {
                told.faults += llformat(" overload(%d of %zu | %d of %zu)", a.overload, a.overloads.size(), b.overload, b.overloads.size());
            }
            if (a.documentation != b.documentation)
            {
                told.faults += " documentation";
            }
            return told;
        }

        // How the corpus went: places asked, answered over a fragment, what
        // is counted of what differs where it answered, and the faults.
        struct Tally
        {
            size_t      asked    = 0;
            size_t      answered = 0;
            size_t      types    = 0;
            size_t      fits     = 0;
            size_t      few      = 0;
            size_t      generics = 0;
            std::string faults;
        };

        // Each place asked of both, the last check made first as the
        // analysis thread leaves it -- or already made, where `settled`.
        void ask(const std::string& name, const std::vector<Probe>& probes, Tally& tally, bool settled = false)
        {
            fragment.setDocument("corpus");
            whole.setDocument("corpus");
            for (const Probe& probe : probes)
            {
                if (!settled)
                {
                    fragment.check(probe.base);
                    fragment.warm(probe.base);
                }
                const size_t before = fragment.fragmentsChecked();
                const Told   told   = probe.signature
                                          ? judged(fragment.signature(probe.text, probe.line, probe.column), whole.signature(probe.text, probe.line, probe.column))
                                          : judged(fragment.complete(probe.text, probe.line, probe.column), whole.complete(probe.text, probe.line, probe.column));
                ++tally.asked;
                if (fragment.fragmentsChecked() == before)
                {
                    continue;
                }
                ++tally.answered;
                tally.types += told.types;
                tally.fits += told.fits;
                tally.few += told.few;
                tally.generics += told.generics;
                if (!told.faults.empty())
                {
                    tally.faults += llformat("%s %d:%d %s:%s\n", name.c_str(), probe.line + 1, probe.column + 1, probe.what.c_str(), told.faults.c_str());
                }
            }
        }

        void report(const char* corpus, const Tally& tally, bool answers = true)
        {
            std::printf("alluaufragment, %s solver, %s: %zu places, %zu answered over a fragment; told apart by a type beside a name %zu, "
                        "by which fit %zu, by a few names %zu, by a signature's parameters %zu\n",
                        newSolver ? "new" : "old", corpus, tally.asked, tally.answered, tally.types, tally.fits, tally.few, tally.generics);
            ensure(std::string(corpus) + ": the fragment answers as the whole script does:\n" + tally.faults, tally.faults.empty());
            if (answers)
            {
                ensure(std::string(corpus) + ": most places answered over a fragment", tally.answered * 2 > tally.asked);
            }
        }
    };

    typedef test_group<alluaufragment_data> alluaufragment_group;
    typedef alluaufragment_group::object    alluaufragment_object;
    alluaufragment_group                    alluaufragment_instance("alluaufragment");

    template<> template<>
    void alluaufragment_object::test<1>()
    {
        set_test_name("over the studio's templates, the fragment answers as the whole script does");
        ensure("definitions loaded: " + error, loaded);
        llifstream in(std::string(AL_SCRIPT_TEMPLATES_DIR) + "/slua.xml", std::ios::in | std::ios::binary);
        LLSD       templates;
        ensure("read", in.is_open() && LLSDSerialize::fromXML(templates, in) != LLSDParser::PARSE_FAILURE && templates.isArray());
        Tally tally;
        for (LLSD::array_const_iterator it = templates.beginArray(); it != templates.endArray(); ++it)
        {
            ask((*it)["name"].asString(), probesOf((*it)["body"].asString(), 12), tally);
        }
        report("the templates", tally);
    }

    template<> template<>
    void alluaufragment_object::test<2>()
    {
        set_test_name("over Tailslide's test scripts written again as SLua, the fragment answers as the whole script does");
        ensure("definitions loaded: " + error, loaded);
        const std::string        dir = std::string(AL_ALSCRIPT_TEST_DIR) + "/tailslide/scripts";
        std::vector<std::string> files;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir))
        {
            if (entry.path().extension() == ".lsl")
            {
                files.push_back(std::filesystem::relative(entry.path(), dir).generic_string());
            }
        }
        std::sort(files.begin(), files.end());
        ensure("the scripts: " + dir, files.size() > 100);
        // The converter reads Tailslide's table of LSL's builtins, which the
        // LSL service fills for the whole process.
        ALLSLService lsl;
        ensure("builtins loaded: " + error, lsl.loadBuiltins(std::string(AL_LSL_DEFINITIONS_DIR) + "/builtins.txt", error));
        Tally tally;
        for (const std::string& name : files)
        {
            const ALLSLToSLua::Result converted = ALLSLToSLua::convert(readFile(dir + "/" + name));
            if (converted.converted)
            {
                ask(name, probesOf(converted.text, 6), tally);
            }
        }
        report("Tailslide's scripts", tally);
    }

    template<> template<>
    void alluaufragment_object::test<3>()
    {
        set_test_name("over a script the preprocessor expanded, its requires apart as the studio checks them, the fragment answers as the whole script does, a module changed since the last check or not");
        ensure("definitions loaded: " + error, loaded);
        namespace fs           = std::filesystem;
        const fs::path    dir  = fs::path(AL_ALSCRIPT_TEST_DIR) / "preprocessor";
        ALPreprocessor::Options options;
        options.lua     = true;
        options.apart   = true;
        options.resolve = [dir](const ALPreprocessor::Ask& ask, ALPreprocessor::Include& out) {
            const fs::path path = dir / "include" / (ask.name + ".luau");
            if (!fs::exists(path))
            {
                return ALPreprocessor::Found::No;
            }
            // Named by its class: MSVC finds no static member from a lambda
            // that does not hold `this`.
            out.text = alluaufragment_data::readFile(path.string());
            out.name = path.filename().string();
            out.path = "disk:/" + out.name;
            return ALPreprocessor::Found::Yes;
        };
        const ALPreprocessor::Result made = ALPreprocessor::run(readFile((dir / "test_require.luau").string()), options);
        ensure("expanded", made.problems.empty() && !made.apart.script.text.empty());
        ALLuauService::Modules modules;
        for (const ALPreprocessor::Result::Piece& piece : made.apart.modules)
        {
            modules.modules.push_back({ piece.key, piece.text });
        }
        for (const ALPreprocessor::Result::Resolved& resolved : made.resolved)
        {
            modules.reaches.push_back({ resolved.from, resolved.name, resolved.path });
        }
        ensure("its modules", modules.modules.size() == 2);
        fragment.setModules(modules);
        whole.setModules(modules);
        Tally tally;
        ask("test_require", probesOf(made.apart.script.text, 12), tally);
        report("an expanded script", tally);
        // A module changed since the last check: util, which had a function
        // since taken out.
        ALLuauService::Modules before = modules;
        for (ALLuauService::Module& module : before.modules)
        {
            if (module.text.find("function M.greet") != std::string::npos)
            {
                module.text.insert(module.text.find("function M.greet"), "function M.wave(): string\n    return \"o/\"\nend\n");
            }
        }
        fragment.setDocument("corpus");
        Tally changed;
        for (const Probe& probe : probesOf(made.apart.script.text, 12))
        {
            fragment.setModules(before);
            fragment.check(probe.base);
            fragment.warm(probe.base);
            fragment.setModules(modules);
            ask("test_require, util changed", { probe }, changed, /*settled*/ true);
        }
        report("an expanded script, a module changed since", changed, false);
        ensure_equals("a module changed since: every place asked of the whole script", changed.answered, size_t(0));
        fragment.setModules({});
        whole.setModules({});
    }

    template<> template<>
    void alluaufragment_object::test<4>()
    {
        set_test_name("a line opened above the statement being typed since the last check: Luau would start the fragment where the text has no such place, and the whole script answers instead");
        ensure("definitions loaded: " + error, loaded);
        // Return pressed at the start of `print(a)`, then `.` typed in it
        // before another check: the last check's statement began at the
        // fifth column of a line that is empty now.
        const std::string base = "local function f(p)\n    local a = p\n    print(a)\nend\n";
        const std::string text = "local function f(p)\n    local a = p\n\n    print(a.)\nend\n";
        fragment.setDocument("opened");
        whole.setDocument("opened");
        fragment.check(base);
        fragment.warm(base);
        const size_t before = fragment.fragmentsChecked();
        const Told   told   = judged(fragment.complete(text, 3, 12), whole.complete(text, 3, 12));
        ensure_equals("asked of the whole script", fragment.fragmentsChecked(), before);
        ensure("and answered as it is:" + told.faults, told.faults.empty());
    }

    template<> template<>
    void alluaufragment_object::test<5>()
    {
        set_test_name("an overloaded callee's handler is offered written out where its argument is being written, and not in a field of one, a table in one or a type asserted of one: over a fragment as over the whole script");
        ensure("definitions loaded: " + error, loaded);
        const auto stubbed = [](const std::vector<ALScriptCompletion>& found) {
            return std::any_of(found.begin(), found.end(), [](const ALScriptCompletion& c) { return !c.snippet.empty(); });
        };
        // Each line typed new since the last check, asked at the argument
        // begun, after a dot in it, in a table in it, at the end of a type
        // asserted of it.
        struct Place
        {
            const char* line;
            S32         column;
            bool        offered;
        };
        const Place places[] = {
            { "LLEvents:on(\"touch_start\", h)", 27, true },
            { "LLEvents:on(\"touch_start\", handlers.)", 36, false },
            { "LLEvents:on(\"touch_start\", {})", 28, false },
            { "LLEvents:on(\"touch_start\", h :: num)", 35, false },
        };
        const std::string base = "local handlers = { touch = 1 }\n";
        fragment.setDocument("stubs");
        whole.setDocument("stubs");
        for (const Place& place : places)
        {
            const std::string text = base + place.line + "\n";
            fragment.check(base);
            fragment.warm(base);
            ensure_equals(std::string(place.line) + ": over a fragment", stubbed(fragment.complete(text, 1, place.column)), place.offered);
            ensure_equals(std::string(place.line) + ": over the whole script", stubbed(whole.complete(text, 1, place.column)), place.offered);
        }
    }

    template<> template<>
    void alluaufragment_object::test<6>()
    {
        set_test_name("a local declared again in its own scope is the declaration in effect, offered and called, over a fragment as over the whole script, wherever the parser put each");
        ensure("definitions loaded: " + error, loaded);
        // `test` a function, then a number in the same scope, and a line
        // typed below since the last check: the name alone, and a third
        // `test` initialized with it. Luau keeps both declarations among the
        // scope's bindings, a table ordered by where the parser put each,
        // and took whichever it gave first. And the number declared since
        // the last check as well, which the fragment declares beside the
        // function it brings from that check -- with the other locals the
        // line uses, so that there are enough for that table's order to be
        // by where they were put too. Each round puts more locals above, so
        // that everything lands elsewhere.
        struct Place
        {
            const char* checked;
            const char* typed;
            S32         column;
        };
        const Place places[] = {
            { "local test = 1\n", "test", 0 },
            { "local test = 1\n", "local test = test", 13 },
            { "", "local test = 1\ntest", 0 },
            { "", "local test = 1\nprint(a, b, c, d, e, f, g, h, test)", 30 },
        };
        fragment.setDocument("redeclared");
        whole.setDocument("redeclared");
        size_t asked = 0, answered = 0;
        for (S32 round = 0; round < 24; ++round)
        {
            std::string above;
            for (S32 i = 0; i < round; ++i)
            {
                above += llformat("local v%d = %d\n", i, i);
            }
            above += "local a, b, c, d, e, f, g, h = 1, 2, 3, 4, 5, 6, 7, 8\nlocal function test()\nend\n";
            const auto ask = [&](const std::string& base, const std::string& typed, auto question) {
                const std::string text = base + typed + "\n";
                const S32         line = static_cast<S32>(std::count(text.begin(), text.end(), '\n')) - 1;
                fragment.check(base);
                fragment.warm(base);
                const size_t before = fragment.fragmentsChecked();
                question(fragment, text, line, "a fragment");
                question(whole, text, line, "the whole script");
                ++asked;
                answered += fragment.fragmentsChecked() != before;
            };
            for (const Place& place : places)
            {
                ask(above + place.checked, place.typed, [&](ALLuauService& service, const std::string& text, S32 line, const char* over) {
                    const std::vector<ALScriptCompletion> found = service.complete(text, line, place.column);
                    const auto test = std::find_if(found.begin(), found.end(), [](const ALScriptCompletion& c) { return c.text == "test"; });
                    ensure(llformat("round %d, %s, over %s: the number", round, place.typed, over),
                           test != found.end() && test->kind == ALScriptSymbolKind::Variable);
                });
            }
            // A function of two strings declared over one of a number: a
            // call typed below takes the two.
            ask(above + "local function call(n: number)\nend\nlocal call = function(s: string, t: string)\nend\n", "call(\"x\", \"y\")",
                [&](ALLuauService& service, const std::string& text, S32 line, const char* over) {
                    const ALScriptSignature found = service.signature(text, line, 5);
                    ensure(llformat("round %d, a call, over %s: the two strings, not %s", round, over, found.label.c_str()),
                           found.found && found.parameters.size() == 2);
                });
        }
        ensure("answered over a fragment", answered * 2 > asked);
    }
}
