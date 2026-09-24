/**
 * @file alscriptweight.cpp
 * @brief What a script weighs for a target: the code its compiler makes, by part and by line.
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

#include "alscriptweight.h"

#include "alscriptengine.h"
#include "allslservice.h"

#include "Luau/Bytecode.h"
#include "Luau/BytecodeHeader.h"
#include "Luau/Compiler.h"
#include "Luau/LSLCompiler.h"
#include "Luau/ParseResult.h"

#include <tailslide/tailslide.hh>
#include <tailslide/passes/lso/bytecode_compiler.hh>
#include <tailslide/passes/lso/script_compiler.hh>
#include <tailslide/passes/mono/script_compiler.hh>

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <sstream>

// static
size_t ALScriptWeight::limitOf(Target target)
{
    switch (target)
    {
        case Target::LSO:
            return 16384;
        case Target::Mono:
            return 65536;
        case Target::SLua:
        case Target::LSLLuau:
        default:
            return 131072;
    }
}

// static
const char* ALScriptWeight::nameOf(Target target)
{
    switch (target)
    {
        case Target::LSO:
            return "LSO";
        case Target::Mono:
            return "Mono";
        case Target::LSLLuau:
            return "LSL on Luau";
        case Target::SLua:
        default:
            return "SLua";
    }
}

// --- SLua: the bytecode read back ----------------------------------------------------------

namespace
{
    // Luau's bytecode, read as the VM's loader reads it (lvmload.cpp) -- the
    // fork's yield points and call feedback included -- for where each
    // prototype begins and ends and which line each instruction came of.
    // Nothing is loaded and nothing runs.
    class Reader
    {
    public:
        Reader(std::string_view data) : mData(data) {}

        bool ok() const { return mOk; }
        size_t at() const { return mAt; }
        void moveTo(size_t at)
        {
            if (at > mData.size())
            {
                mOk = false;
                return;
            }
            mAt = at;
        }

        uint8_t byte()
        {
            if (mAt >= mData.size())
            {
                mOk = false;
                return 0;
            }
            return static_cast<uint8_t>(mData[mAt++]);
        }
        uint64_t varint()
        {
            uint64_t result = 0;
            for (int shift = 0; shift < 64; shift += 7)
            {
                const uint8_t b = byte();
                result |= uint64_t(b & 127) << shift;
                if (!(b & 128) || !mOk)
                {
                    return result;
                }
            }
            mOk = false;
            return result;
        }
        int32_t int32()
        {
            uint32_t v = 0;
            for (int i = 0; i < 4; ++i)
            {
                v |= uint32_t(byte()) << (8 * i);
            }
            return static_cast<int32_t>(v);
        }
        void skip(uint64_t count)
        {
            if (count > mData.size() - std::min(mAt, mData.size()))
            {
                mOk = false;
                return;
            }
            mAt += static_cast<size_t>(count);
        }

    private:
        std::string_view mData;
        size_t           mAt = 0;
        bool             mOk = true;
    };

    struct Proto
    {
        size_t           begin = 0;
        size_t           end   = 0;
        uint64_t         lineDefined = 0;
        uint64_t         name = 0;       // into the string table, from one
        std::vector<S32> lines;          // one-based, per code word; empty without line info
    };

    struct Bytecode
    {
        std::vector<std::string> strings;
        size_t                   stringsBegin = 0;
        size_t                   stringsEnd   = 0;
        std::vector<Proto>       protos;
        uint64_t                 main = 0;
    };

    bool readBytecode(std::string_view data, Bytecode& out)
    {
        Reader        in(data);
        const uint8_t version = in.byte();
        if (!in.ok() || version == 0 || ((version < LBC_VERSION_MIN || version > LBC_VERSION_MAX) && version != LBC_VERSION_CLASSES))
        {
            return false;
        }
        const uint8_t types = version >= 4 ? in.byte() : 0;
        out.stringsBegin    = in.at();
        const uint64_t count = in.varint();
        for (uint64_t i = 0; i < count && in.ok(); ++i)
        {
            const uint64_t length = in.varint();
            const size_t   from   = in.at();
            in.skip(length);
            if (in.ok())
            {
                out.strings.emplace_back(data.substr(from, static_cast<size_t>(length)));
            }
        }
        out.stringsEnd = in.at();
        if (types == 3)
        {
            for (uint8_t index = in.byte(); index != 0 && in.ok(); index = in.byte())
            {
                in.varint();
            }
        }
        const uint64_t protos = in.varint();
        for (uint64_t i = 0; i < protos && in.ok(); ++i)
        {
            Proto p;
            p.begin              = in.at();
            const uint64_t size  = version >= 12 ? in.varint() : 0;
            const size_t   start = in.at();
            in.skip(4); // maxstacksize, numparams, nups, is_vararg
            uint8_t flags = 0;
            if (version >= 4)
            {
                flags = in.byte();
                if (types >= 1 && types <= 3)
                {
                    in.skip(in.varint());
                }
            }
            const uint64_t sizecode = in.varint();
            in.skip(sizecode * 4);
            const uint64_t sizek = in.varint();
            for (uint64_t k = 0; k < sizek && in.ok(); ++k)
            {
                switch (in.byte())
                {
                    case LBC_CONSTANT_NIL:
                        break;
                    case LBC_CONSTANT_BOOLEAN:
                        in.skip(1);
                        break;
                    case LBC_CONSTANT_NUMBER:
                        in.skip(8);
                        break;
                    case LBC_CONSTANT_VECTOR:
                        in.skip(16);
                        break;
                    case LBC_CONSTANT_VECTORD:
                        in.skip(32);
                        break;
                    case LBC_CONSTANT_STRING:
                    case LBC_CONSTANT_CLOSURE:
                        in.varint();
                        break;
                    case LBC_CONSTANT_IMPORT:
                        in.skip(4);
                        break;
                    case LBC_CONSTANT_TABLE:
                    {
                        const uint64_t keys = in.varint();
                        for (uint64_t j = 0; j < keys && in.ok(); ++j)
                        {
                            in.varint();
                        }
                        break;
                    }
                    case LBC_CONSTANT_TABLE_WITH_CONSTANTS:
                    {
                        const uint64_t keys = in.varint();
                        for (uint64_t j = 0; j < keys && in.ok(); ++j)
                        {
                            in.varint();
                            in.skip(4);
                        }
                        break;
                    }
                    case LBC_CONSTANT_CLASS_SHAPE:
                    {
                        in.varint();
                        const uint64_t members = in.varint() + in.varint();
                        for (uint64_t j = 0; j < members && in.ok(); ++j)
                        {
                            in.varint();
                        }
                        break;
                    }
                    case LBC_CONSTANT_INTEGER:
                        in.skip(1);
                        in.varint();
                        break;
                    default:
                        return false;
                }
            }
            const uint64_t children = in.varint();
            for (uint64_t c = 0; c < children && in.ok(); ++c)
            {
                in.varint();
            }
            p.lineDefined = in.varint();
            p.name        = in.varint();
            if (in.byte() != 0)
            {
                const uint8_t        gaplog2   = in.byte();
                const uint64_t       intervals = sizecode ? ((sizecode - 1) >> gaplog2) + 1 : 0;
                std::vector<uint8_t> offsets;
                uint8_t              last = 0;
                for (uint64_t j = 0; j < sizecode && in.ok(); ++j)
                {
                    last = static_cast<uint8_t>(last + in.byte());
                    offsets.push_back(last);
                }
                std::vector<int32_t> absolute;
                int32_t              line = 0;
                for (uint64_t j = 0; j < intervals && in.ok(); ++j)
                {
                    line += in.int32();
                    absolute.push_back(line);
                }
                for (uint64_t j = 0; j < offsets.size() && in.ok(); ++j)
                {
                    const size_t interval = static_cast<size_t>(j >> gaplog2);
                    p.lines.push_back(interval < absolute.size() ? absolute[interval] + offsets[j] : 0);
                }
            }
            if (in.byte() != 0)
            {
                const uint64_t locals = in.varint();
                for (uint64_t j = 0; j < locals && in.ok(); ++j)
                {
                    in.varint();
                    in.varint();
                    in.varint();
                    in.skip(1);
                }
                const uint64_t upvalues = in.varint();
                for (uint64_t j = 0; j < upvalues && in.ok(); ++j)
                {
                    in.varint();
                }
            }
            // ServerLua: where the script may yield.
            const uint64_t yields = in.varint();
            for (uint64_t j = 0; j < yields && in.ok(); ++j)
            {
                in.varint();
            }
            if (version >= 11)
            {
                const uint64_t slots = in.varint();
                for (uint64_t j = 0; j < slots && in.ok(); ++j)
                {
                    in.skip(1);
                    in.varint();
                }
            }
            if (version >= 12)
            {
                if (flags & LPF_INLINABLE)
                {
                    in.varint();
                }
                in.moveTo(start + static_cast<size_t>(size));
            }
            p.end = in.at();
            out.protos.push_back(std::move(p));
        }
        out.main = in.varint();
        return in.ok();
    }

    ALScriptWeight::Part part(ALScriptWeight::Part::Kind kind, std::string name, size_t bytes)
    {
        ALScriptWeight::Part p;
        p.kind  = kind;
        p.name  = std::move(name);
        p.bytes = bytes;
        return p;
    }
}

namespace
{
    // What an asset of Luau bytecode weighs, and its parts and lines: what
    // the server charges, read off the header, and the bytecode read back.
    void weighAsset(ALScriptWeight& weight, const std::string& asset);
    // The LSL compiler's prototypes by the script's own names -- `_f<name>`
    // a function, `_e<state>/<event>` a handler of the state so numbered --
    // and where each is in the script.
    void nameLSLParts(ALScriptWeight& weight, std::string_view source);
}

namespace ALScriptWeigh
{
    ALScriptWeight slua(std::string_view source)
    {
        ALScriptWeight weight;
        weight.target = ALScriptWeight::Target::SLua;
        weight.limit  = ALScriptWeight::limitOf(weight.target);
        Luau::CompileOptions options;
        options.optimizationLevel = SLUA_OPTIMIZATION_LEVEL;
        options.debugLevel        = SLUA_DEBUG_LEVEL;
        std::string asset;
        try
        {
            asset = Luau::compileAssetOrThrow(std::string(source), 0, options);
        }
        catch (const std::exception& e)
        {
            weight.error = e.what();
            return weight;
        }
        weighAsset(weight, asset);
        return weight;
    }

    ALScriptWeight lslLuau(std::string_view source)
    {
        ALScriptWeight weight;
        weight.target = ALScriptWeight::Target::LSLLuau;
        weight.limit  = ALScriptWeight::limitOf(weight.target);
        if (!ALLSLService::builtinsLoaded())
        {
            weight.error = "the LSL builtins are not loaded";
            return weight;
        }
        AL_SCRIPT_ENGINE_HELD;
        std::string asset;
        try
        {
            asset = compileLSLAssetOrThrow(std::string(source), 0);
        }
        catch (const Luau::ParseErrors& e)
        {
            weight.error = e.getErrors().empty() ? std::string("it does not compile") : e.getErrors().front().getMessage();
            return weight;
        }
        catch (const std::exception& e)
        {
            weight.error = e.what();
            return weight;
        }
        weighAsset(weight, asset);
        nameLSLParts(weight, source);
        return weight;
    }
}

namespace
{
    void weighAsset(ALScriptWeight& weight, const std::string& asset)
    {
        Luau::BytecodeHeader header;
        size_t               start = 0;
        if (!Luau::readBytecodeHeader(asset.data(), asset.size(), header, start) || start > asset.size())
        {
            weight.error = "the compiled asset has no header it can be read by";
            return;
        }
        const std::string_view code(asset.data() + start, asset.size() - start);
        // What the server charges: the bytecode's length, unless the header
        // says to charge another.
        weight.total = header.chargedBytecodeSize ? header.chargedBytecodeSize : code.size();
        Bytecode read;
        if (!readBytecode(code, read))
        {
            weight.error = "the bytecode could not be read back";
            return;
        }
        weight.compiled = true;
        weight.parts.push_back(part(ALScriptWeight::Part::Kind::Constant, "strings", read.stringsEnd - read.stringsBegin));
        std::map<S32, size_t> by_line;
        for (size_t i = 0; i < read.protos.size(); ++i)
        {
            const Proto& p   = read.protos[i];
            const bool   top = i == read.main;
            std::string  name;
            if (top)
            {
                name = "(the script)";
            }
            else if (p.name > 0 && p.name <= read.strings.size())
            {
                name = read.strings[static_cast<size_t>(p.name - 1)];
            }
            else
            {
                name = "function";
            }
            ALScriptWeight::Part one = part(top ? ALScriptWeight::Part::Kind::Frame : ALScriptWeight::Part::Kind::Function, name, p.end - p.begin);
            S32 first = p.lineDefined > 0 ? static_cast<S32>(p.lineDefined) : 0;
            S32 last  = first;
            for (S32 line : p.lines)
            {
                if (line > 0)
                {
                    by_line[line - 1] += 4;
                    last = std::max(last, line);
                }
            }
            if (!top && first > 0)
            {
                one.line    = first - 1;
                one.column  = 0;
                one.endLine = last - 1;
            }
            weight.parts.push_back(std::move(one));
        }
        for (const auto& [line, bytes] : by_line)
        {
            weight.lines.push_back({ line, bytes });
        }
    }
}

// --- LSO: Tailslide's image ------------------------------------------------------------------

namespace
{
    S32 zeroBased(int one_based)
    {
        return std::max(0, one_based - 1);
    }

    // Which line of the text a compiler is writing for as it walks the
    // tree: the line each node begins on, or the one around it where it has
    // no place of its own; for a function or a handler, its closing line,
    // since what it writes of its own comes after its body -- the return
    // its last path does not make. Nothing for the script as a whole. A
    // node Tailslide makes after the parse -- `++i` made `i = i + 1` --
    // may carry the whole script's place rather than its statement's, so a
    // place is believed only inside the one around it. Kept as where the
    // output stood at each change of line, so that what was written from
    // one mark to the next is the first mark's line. The compiler's
    // visitor calls it around every node it visits, which Tailslide lets a
    // subclass do by `visitSpecific`.
    class LineMarks
    {
    public:
        void enter(Tailslide::LSLASTNode* node, size_t at)
        {
            Open                         open = mOpen.empty() ? Open() : mOpen.back();
            const Tailslide::YYLTYPE*    loc  = node->getLoc();
            const Tailslide::LSLNodeType type = node->getNodeType();
            if (type == Tailslide::NODE_SCRIPT)
            {
                open.line = -1;
            }
            if (loc && loc->first_line > 0 && loc->first_line >= open.first && loc->last_line <= open.last)
            {
                const bool closes = type == Tailslide::NODE_GLOBAL_FUNCTION || type == Tailslide::NODE_EVENT_HANDLER;
                if (type != Tailslide::NODE_SCRIPT)
                {
                    open.line = zeroBased(closes ? loc->last_line : loc->first_line);
                }
                open.first = loc->first_line;
                open.last  = loc->last_line;
            }
            mOpen.push_back(open);
            mark(at);
        }
        void leave(size_t at)
        {
            if (!mOpen.empty())
            {
                mOpen.pop_back();
            }
            mark(at);
        }

        // Each mark's line and where it stood, in the order written.
        const std::vector<std::pair<size_t, S32>>& marks() const { return mFrom; }
        // The bytes each line came to, of output `total` long.
        void spread(size_t total, std::map<S32, size_t>& lines) const
        {
            for (size_t i = 0; i < mFrom.size(); ++i)
            {
                const size_t end = i + 1 < mFrom.size() ? mFrom[i + 1].first : total;
                if (mFrom[i].second >= 0 && end > mFrom[i].first)
                {
                    lines[mFrom[i].second] += end - mFrom[i].first;
                }
            }
        }

    private:
        void mark(size_t at)
        {
            const S32 line = mOpen.empty() ? -1 : mOpen.back().line;
            // Nothing written since the last mark: it was never that line's.
            if (!mFrom.empty() && mFrom.back().first == at)
            {
                mFrom.pop_back();
            }
            if (mFrom.empty() || mFrom.back().second != line)
            {
                mFrom.emplace_back(at, line);
            }
        }

        struct Open
        {
            S32 line  = -1;
            // The one-based lines of the place around, which a place within
            // it must be inside.
            int first = 0;
            int last  = std::numeric_limits<int>::max();
        };
        std::vector<Open>                   mOpen;
        std::vector<std::pair<size_t, S32>> mFrom;
    };

    // Tailslide's LSO code for one function or handler written a second
    // time, with the line each byte came of. The bytecode compiler writes
    // only what it reads from the tree and the symbols' data, and changes
    // neither, so the second writing is the first's.
    class LinedLSO : public Tailslide::LSOBytecodeCompiler
    {
    public:
        explicit LinedLSO(Tailslide::LSOSymbolDataMap& symbols) : LSOBytecodeCompiler(symbols) {}

        LineMarks marks;

        bool visitSpecific(Tailslide::LSLASTNode* node) override
        {
            marks.enter(node, mCodeBS.size());
            const bool descend = LSOBytecodeCompiler::visitSpecific(node);
            marks.leave(mCodeBS.size());
            return descend;
        }
    };

    // Tailslide's LSO compiler, told of each function, state, handler and
    // global as it writes them, and measuring what each came to: from where
    // its writing begins to where it ends. Not by a stream's size, which a
    // move past the end leaves a byte beyond where it moved to, for what is
    // written next to write over.
    class MeasuringLSO : public Tailslide::LSOScriptCompiler
    {
    public:
        explicit MeasuringLSO(Tailslide::ScriptAllocator* allocator) : LSOScriptCompiler(allocator) {}

        std::vector<ALScriptWeight::Part> parts;
        std::map<S32, size_t>             lines;
        size_t registers = 0, globals = 0, functions = 0, states = 0, heap = 0;

    protected:
        bool visit(Tailslide::LSLScript* script) override
        {
            const bool result = LSOScriptCompiler::visit(script);
            registers         = _mRegistersBS.size();
            globals           = _mGlobalVarManager.mGlobalsBS.size();
            functions         = _mFunctionsBS.size();
            states            = _mStatesBS.size();
            heap              = _mHeapManager.mHeapBS.size();
            return result;
        }
        bool visit(Tailslide::LSLGlobalVariable* global) override
        {
            const size_t before = _mGlobalVarManager.mGlobalsBS.pos() + _mHeapManager.mHeapBS.pos();
            LSOScriptCompiler::visit(global);
            const size_t bytes = _mGlobalVarManager.mGlobalsBS.pos() + _mHeapManager.mHeapBS.pos() - before;
            add(ALScriptWeight::Part::Kind::Global, global, nameOf(global->getChild(0)), std::string(), bytes);
            // Its value is in the image, and is its line's.
            if (const Tailslide::YYLTYPE* at = global->getLoc(); at && at->first_line > 0 && bytes > 0)
            {
                lines[zeroBased(at->first_line)] += bytes;
            }
            return false;
        }
        bool visit(Tailslide::LSLGlobalFunction* function) override
        {
            const size_t before = _mFunctionsBS.pos();
            LSOScriptCompiler::visit(function);
            add(ALScriptWeight::Part::Kind::Function, function, nameOf(function->getChild(0)), std::string(), _mFunctionsBS.pos() - before);
            if (_mFunctionsBS.pos() > before)
            {
                lineCode(function);
            }
            return false;
        }
        bool visit(Tailslide::LSLState* state) override
        {
            const std::string was = mState;
            mState                = nameOf(state->getChild(0));
            const size_t before   = _mStateBS.pos();
            LSOScriptCompiler::visit(state);
            add(ALScriptWeight::Part::Kind::State, state, mState, std::string(), _mStateBS.pos() - before);
            mState = was;
            return false;
        }
        bool visit(Tailslide::LSLEventHandler* handler) override
        {
            const size_t before = _mStateBS.pos();
            LSOScriptCompiler::visit(handler);
            add(ALScriptWeight::Part::Kind::Handler, handler, nameOf(handler->getChild(0)), mState, _mStateBS.pos() - before);
            lineCode(handler);
            return false;
        }

    private:
        // The code of a function or handler, by the line each byte came of.
        void lineCode(Tailslide::LSLASTNode* node)
        {
            LinedLSO lined(_mSymData);
            node->visit(&lined);
            lined.marks.spread(lined.mCodeBS.size(), lines);
        }

        static std::string nameOf(Tailslide::LSLASTNode* node)
        {
            return node && node->getNodeType() == Tailslide::NODE_IDENTIFIER ? static_cast<Tailslide::LSLIdentifier*>(node)->getName() : std::string();
        }
        void add(ALScriptWeight::Part::Kind kind, Tailslide::LSLASTNode* node, std::string name, std::string within, size_t bytes)
        {
            ALScriptWeight::Part p;
            p.kind   = kind;
            p.name   = std::move(name);
            p.within = std::move(within);
            p.bytes  = bytes;
            if (const Tailslide::YYLTYPE* at = node->getLoc(); at && at->first_line > 0)
            {
                p.line      = zeroBased(at->first_line);
                p.column    = zeroBased(at->first_column);
                p.endLine   = zeroBased(at->last_line);
                p.endColumn = zeroBased(at->last_column);
            }
            parts.push_back(std::move(p));
        }

        std::string mState;
    };
}

namespace ALScriptWeigh
{
    ALScriptWeight lso(std::string_view source)
    {
        ALScriptWeight weight;
        weight.target = ALScriptWeight::Target::LSO;
        weight.limit  = ALScriptWeight::limitOf(weight.target);
        if (!ALLSLService::builtinsLoaded())
        {
            weight.error = "the LSL builtins are not loaded";
            return weight;
        }
        AL_SCRIPT_ENGINE_HELD;
        // A tree of its own: the compiler desugars the one it is given.
        Tailslide::ScopedScriptParser parser(nullptr);
        const std::string            text(source);
        Tailslide::LSLScript*        script = parser.parseLSLBytes(text.data(), static_cast<int>(text.size()));
        if (!script)
        {
            weight.error = "it does not parse";
            return weight;
        }
        // The passes Tailslide's own tool runs before it compiles, but its
        // optimizer: what is weighed is what is sent.
        script->collectSymbols();
        script->determineTypes();
        script->recalculateReferenceData();
        script->propagateValues();
        script->finalPass();
        script->validateGlobals(false);
        script->checkSymbols();
        if (parser.logger.getErrors())
        {
            weight.error = "it has errors";
            return weight;
        }
        MeasuringLSO compiler(&parser.allocator);
        script->visit(&compiler);
        weight.total = compiler.registers + compiler.globals + compiler.functions + compiler.states + compiler.heap;
        weight.parts = std::move(compiler.parts);
        weight.parts.insert(weight.parts.begin(), part(ALScriptWeight::Part::Kind::Frame, "registers", compiler.registers));
        for (const auto& [line, bytes] : compiler.lines)
        {
            weight.lines.push_back({ line, bytes });
        }
        if (parser.logger.getErrors())
        {
            // Where the stack meets the heap before the image is made.
            weight.error = "it does not fit in 16 KB";
            return weight;
        }
        weight.compiled = true;
        return weight;
    }
}

// --- Mono: Tailslide's IL, sized ----------------------------------------------------------------

namespace
{
    // The bytes an IL instruction encodes to: its opcode, one byte or two,
    // and its operand. Tailslide writes the long forms of branches.
    size_t ilBytes(const std::string& op, const std::string& rest)
    {
        static const std::set<std::string> TWO_BYTE = { "ceq",  "cgt",   "cgt.un", "clt",   "clt.un", "ldftn",  "ldvirtftn", "ldarg",
                                                         "ldarga", "starg", "ldloc",  "ldloca", "stloc", "initobj", "sizeof",    "rethrow" };
        static const std::set<std::string> TOKEN = { "call",   "callvirt", "newobj",   "ldfld",   "ldflda", "stfld",  "ldsfld",  "ldsflda",
                                                     "stsfld", "ldstr",    "box",      "unbox",   "unbox.any", "castclass", "isinst", "ldtoken",
                                                     "newarr", "ldobj",    "stobj",    "initobj", "ldelema", "ldelem", "stelem",  "ldftn",
                                                     "ldvirtftn", "sizeof", "jmp",     "calli" };
        static const std::set<std::string> INT32 = { "ldc.i4", "br",  "brfalse", "brtrue", "beq",    "bge",    "bgt",    "ble",
                                                     "blt",    "bne.un", "bge.un", "bgt.un", "ble.un", "blt.un", "leave", "ldc.r4" };
        static const std::set<std::string> INT16 = { "ldarg", "ldarga", "starg", "ldloc", "ldloca", "stloc" };
        size_t bytes = TWO_BYTE.count(op) ? 2 : 1;
        if (TOKEN.count(op))
        {
            bytes += 4;
        }
        else if (INT32.count(op))
        {
            bytes += 4;
        }
        else if (INT16.count(op))
        {
            bytes += 2;
        }
        else if (op == "ldc.i8" || op == "ldc.r8")
        {
            bytes += 8;
        }
        else if (op == "switch")
        {
            bytes += 4 + 4 * static_cast<size_t>(std::count(rest.begin(), rest.end(), ',') + (rest.find('(') != std::string::npos ? 1 : 0));
        }
        else if (op.size() > 2 && op.compare(op.size() - 2, 2, ".s") == 0)
        {
            bytes += 1;
        }
        return bytes;
    }

    // A string literal's bytes in the user string heap: two a character,
    // a trailing byte and its length.
    size_t userStringBytes(const std::string& rest)
    {
        const size_t open  = rest.find('"');
        const size_t close = rest.rfind('"');
        if (open == std::string::npos || close <= open)
        {
            return 0;
        }
        size_t chars = 0;
        for (size_t i = open + 1; i < close; ++i)
        {
            if (rest[i] == '\\' && i + 1 < close)
            {
                ++i;
            }
            // UTF-8 continuation bytes are part of the character before.
            if ((static_cast<unsigned char>(rest[i]) & 0xC0) != 0x80)
            {
                ++chars;
            }
        }
        const size_t length = chars * 2 + 1;
        return length + (length < 0x80 ? 1 : length < 0x4000 ? 2 : 4);
    }

    // Where each function and handler is in the tree, by the name Tailslide
    // gives its method: 'g<name>' and e<state><event>.
    struct Places final : public Tailslide::ASTVisitor
    {
        std::map<std::string, std::pair<Tailslide::LSLASTNode*, std::pair<std::string, std::string>>> methods;
        std::string                                                                           state;

        static std::string nameOf(Tailslide::LSLASTNode* node)
        {
            return node && node->getNodeType() == Tailslide::NODE_IDENTIFIER ? static_cast<Tailslide::LSLIdentifier*>(node)->getName() : std::string();
        }
        bool visit(Tailslide::LSLGlobalFunction* function) override
        {
            const std::string name = nameOf(function->getChild(0));
            methods["'g" + name + "'"] = { function, { name, std::string() } };
            return false;
        }
        bool visit(Tailslide::LSLState* node) override
        {
            state = nameOf(node->getChild(0));
            return true;
        }
        bool visit(Tailslide::LSLEventHandler* handler) override
        {
            const std::string event = nameOf(handler->getChild(0));
            methods["e" + state + event] = { handler, { event, state } };
            return false;
        }
    };

    // Tailslide's Mono compiler, marking where its CIL stands as it enters
    // and leaves each node, so that each instruction is the line it was
    // written for. It only appends to the text.
    class LinedMono : public Tailslide::MonoScriptCompiler
    {
    public:
        explicit LinedMono(Tailslide::ScriptAllocator* allocator) : MonoScriptCompiler(allocator) {}

        LineMarks marks;

        bool visitSpecific(Tailslide::LSLASTNode* node) override
        {
            marks.enter(node, at());
            const bool descend = MonoScriptCompiler::visitSpecific(node);
            marks.leave(at());
            return descend;
        }

    private:
        size_t at()
        {
            const std::streamoff pos = mCIL.tellp();
            return pos < 0 ? 0 : static_cast<size_t>(pos);
        }
    };
}

namespace ALScriptWeigh
{
    ALScriptWeight mono(std::string_view source)
    {
        ALScriptWeight weight;
        weight.target   = ALScriptWeight::Target::Mono;
        weight.limit    = ALScriptWeight::limitOf(weight.target);
        weight.estimate = true;
        if (!ALLSLService::builtinsLoaded())
        {
            weight.error = "the LSL builtins are not loaded";
            return weight;
        }
        AL_SCRIPT_ENGINE_HELD;
        Tailslide::ScopedScriptParser parser(nullptr);
        const std::string            text(source);
        Tailslide::LSLScript*        script = parser.parseLSLBytes(text.data(), static_cast<int>(text.size()));
        if (!script)
        {
            weight.error = "it does not parse";
            return weight;
        }
        script->collectSymbols();
        script->determineTypes();
        script->recalculateReferenceData();
        script->propagateValues();
        script->finalPass();
        script->validateGlobals(true);
        script->checkSymbols();
        if (parser.logger.getErrors())
        {
            weight.error = "it has errors";
            return weight;
        }
        Places places;
        script->visit(&places);
        LinedMono compiler(&parser.allocator);
        script->visit(&compiler);

        // A method at a time: its header, its code, its locals, and what
        // declaring it takes; with the fields, the strings and what it calls
        // over the whole. Each instruction, with its string, is the line it
        // was written for as well.
        std::istringstream    cil(compiler.mCIL.str());
        std::set<std::string> referenced;
        size_t                shared = MONO_BASE_BYTES;
        ALScriptWeight::Part* method = nullptr;
        ALScriptWeight::Part  globals;
        globals.kind = ALScriptWeight::Part::Kind::Frame;
        globals.name = "globals";
        const std::vector<std::pair<size_t, S32>>& marks = compiler.marks.marks();
        size_t                                     mark  = 0;
        size_t                                     next  = 0;
        std::map<S32, size_t>                      by_line;
        for (std::string line; std::getline(cil, line);)
        {
            const size_t begins = next;
            next += line.size() + 1;
            while (mark < marks.size() && marks[mark].first <= begins)
            {
                ++mark;
            }
            const S32    source = mark > 0 ? marks[mark - 1].second : -1;
            const size_t lead   = line.find_first_not_of(" \t");
            if (lead == std::string::npos)
            {
                continue;
            }
            line.erase(0, lead);
            if (line.compare(0, 7, ".field ") == 0)
            {
                // A row, its name and its signature.
                const size_t quote = line.find('\'');
                globals.bytes += 6 + 4 + (quote == std::string::npos ? 8 : line.size() - quote);
                continue;
            }
            if (line.compare(0, 8, ".method ") == 0)
            {
                // Its row and signature, and its header, fat: Tailslide says
                // a stack of more than eight.
                const size_t paren = line.find('(');
                const size_t space = paren == std::string::npos ? std::string::npos : line.rfind(' ', paren);
                std::string  name  = space == std::string::npos ? std::string() : line.substr(space + 1, paren - space - 1);
                const size_t close = line.find(')', paren);
                const size_t args  = paren == std::string::npos || close == paren + 1 ? 0 : std::count(line.begin() + paren, line.begin() + close, ',') + 1;
                const size_t head  = 14 + name.size() + 1 + 4 + args * (6 + 8) + 12;
                if (name == ".ctor")
                {
                    method = &globals;
                    globals.bytes += head;
                    continue;
                }
                const auto known = places.methods.find(name);
                ALScriptWeight::Part part;
                part.kind  = known != places.methods.end() && !known->second.second.second.empty() ? ALScriptWeight::Part::Kind::Handler
                                                                                                  : ALScriptWeight::Part::Kind::Function;
                part.name  = known != places.methods.end() ? known->second.second.first : name;
                part.within = known != places.methods.end() ? known->second.second.second : std::string();
                part.bytes = head;
                if (known != places.methods.end())
                {
                    if (const Tailslide::YYLTYPE* at = known->second.first->getLoc(); at && at->first_line > 0)
                    {
                        part.line      = zeroBased(at->first_line);
                        part.column    = zeroBased(at->first_column);
                        part.endLine   = zeroBased(at->last_line);
                        part.endColumn = zeroBased(at->last_column);
                    }
                }
                weight.parts.push_back(std::move(part));
                method = &weight.parts.back();
                continue;
            }
            if (!method || line[0] == '.' || line[0] == '{' || line[0] == '}' || line.back() == ':')
            {
                if (method && line.compare(0, 13, ".locals init ") == 0)
                {
                    // A signature for them, and a row to hold it.
                    method->bytes += 2 + 3 + 2 * static_cast<size_t>(std::count(line.begin(), line.end(), ',') + 1);
                }
                continue;
            }
            const size_t      cut  = line.find(' ');
            const std::string op   = line.substr(0, cut);
            const std::string rest = cut == std::string::npos ? std::string() : line.substr(cut + 1);
            const size_t      bytes = ilBytes(op, rest) + (op == "ldstr" ? userStringBytes(rest) : 0);
            method->bytes += bytes;
            if (source >= 0)
            {
                by_line[source] += bytes;
            }
            if ((op == "call" || op == "callvirt" || op == "newobj" || op == "ldfld" || op == "stfld" || op == "ldflda") &&
                referenced.insert(rest).second)
            {
                // What it names outside itself, once however often: a row
                // with a name and a signature.
                const size_t colons = rest.find("::");
                const size_t paren  = rest.find('(', colons == std::string::npos ? 0 : colons);
                shared += 6 + (colons == std::string::npos ? 8 : (paren == std::string::npos ? rest.size() : paren) - colons) + 8;
            }
        }
        weight.parts.insert(weight.parts.begin(), globals);
        weight.parts.insert(weight.parts.begin(), part(ALScriptWeight::Part::Kind::Frame, "assembly", shared));
        for (const ALScriptWeight::Part& one : weight.parts)
        {
            weight.total += one.bytes;
        }
        for (const auto& [line, bytes] : by_line)
        {
            weight.lines.push_back({ line, bytes });
        }
        weight.compiled = true;
        return weight;
    }
}

namespace
{
    void nameLSLParts(ALScriptWeight& weight, std::string_view source)
    {
        Tailslide::ScopedScriptParser parser(nullptr);
        const std::string            text(source);
        Tailslide::LSLScript*        script = parser.parseLSLBytes(text.data(), static_cast<int>(text.size()));
        if (!script)
        {
            return;
        }
        // The states in the order written, which is the order numbered; and
        // each function and handler where it is.
        std::vector<std::string>                              states;
        std::map<std::string, Tailslide::LSLASTNode*>         functions;
        std::map<std::pair<size_t, std::string>, Tailslide::LSLASTNode*> handlers;
        const auto nameOf = [](Tailslide::LSLASTNode* node) {
            return node && node->getNodeType() == Tailslide::NODE_IDENTIFIER ? std::string(static_cast<Tailslide::LSLIdentifier*>(node)->getName())
                                                                             : std::string();
        };
        for (Tailslide::LSLASTNode* global = script->getGlobals() ? script->getGlobals()->getChild(0) : nullptr; global; global = global->getNext())
        {
            if (global->getNodeType() == Tailslide::NODE_GLOBAL_FUNCTION)
            {
                functions[nameOf(global->getChild(0))] = global;
            }
        }
        for (Tailslide::LSLASTNode* state = script->getStates() ? script->getStates()->getChild(0) : nullptr; state; state = state->getNext())
        {
            if (state->getNodeType() != Tailslide::NODE_STATE)
            {
                continue;
            }
            states.push_back(nameOf(state->getChild(0)));
            Tailslide::LSLASTNode* list = state->getChild(1);
            for (Tailslide::LSLASTNode* handler = list ? list->getChild(0) : nullptr; handler; handler = handler->getNext())
            {
                handlers[{ states.size() - 1, nameOf(handler->getChild(0)) }] = handler;
            }
        }
        const auto place = [](ALScriptWeight::Part& part, Tailslide::LSLASTNode* node) {
            if (const Tailslide::YYLTYPE* at = node ? node->getLoc() : nullptr; at && at->first_line > 0)
            {
                part.line      = zeroBased(at->first_line);
                part.column    = zeroBased(at->first_column);
                part.endLine   = zeroBased(at->last_line);
                part.endColumn = zeroBased(at->last_column);
            }
        };
        for (ALScriptWeight::Part& part : weight.parts)
        {
            if (part.name.compare(0, 2, "_f") == 0)
            {
                part.name = part.name.substr(2);
                const auto found = functions.find(part.name);
                place(part, found == functions.end() ? nullptr : found->second);
            }
            else if (part.name.compare(0, 2, "_e") == 0)
            {
                const size_t slash = part.name.find('/');
                if (slash == std::string::npos)
                {
                    continue;
                }
                const size_t state = static_cast<size_t>(std::atoi(part.name.c_str() + 2));
                part.kind          = ALScriptWeight::Part::Kind::Handler;
                part.within        = state < states.size() ? states[state] : std::string();
                part.name          = part.name.substr(slash + 1);
                const auto found   = handlers.find({ state, part.name });
                place(part, found == handlers.end() ? nullptr : found->second);
            }
        }
    }
}
