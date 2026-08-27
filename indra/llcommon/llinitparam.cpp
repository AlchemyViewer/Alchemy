/**
 * @file llinitparam.cpp
 * @brief parameter block abstraction for creating complex objects and
 * parsing construction parameters from xml and LLSD
 *
 * $LicenseInfo:firstyear=2008&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "llinitparam.h"
#include "llformat.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <sstream>
#include <unordered_set>


namespace LLInitParam
{

    predicate_rule_t default_parse_rules()
    {
        return ll_make_predicate(PROVIDED) && !ll_make_predicate(EMPTY);
    }

    std::size_t allocateParserTypeIndex()
    {
        // Parsers register their types while constructing, which can happen on
        // more than one thread, so the counter is atomic. It runs a couple of
        // dozen times over the life of the process.
        static std::atomic<std::size_t> sNextIndex{0};
        return sNextIndex.fetch_add(1, std::memory_order_relaxed);
    }

    //
    // Param
    //
    Param::Param(BaseBlock* enclosing_block)
    :   mEnclosingBlockOffset(0)
    ,   mIsProvided(false)
    {
        // Store the byte offset from the enclosing BaseBlock subobject to
        // this Param subobject. With 31 bits of storage the limit is just
        // over 2 GB -- well past any realistic block size -- so the only
        // remaining precondition is that the offset is non-negative: under
        // multiple inheritance arrangements where BaseBlock is not the
        // leading base, a Param member could land at a lower address than
        // the BaseBlock subobject and we'd wrap. None of the in-tree blocks
        // do that, but assert both invariants so a regression fails loudly.
        const ptrdiff_t offset =
            reinterpret_cast<const U8*>(this) - reinterpret_cast<const U8*>(enclosing_block);
        llassert(offset >= 0);
        llassert(offset <= 0x7fffffff);
        mEnclosingBlockOffset = static_cast<U32>(offset);
    }

    //
    // ParamDescriptor
    //
    ParamDescriptor::ParamDescriptor(param_handle_t p,
                                    merge_func_t merge_func,
                                    deserialize_func_t deserialize_func,
                                    serialize_func_t serialize_func,
                                    validation_func_t validation_func,
                                    S32 min_count,
                                    S32 max_count)
    :   mParamHandle(p),
        mMergeFunc(merge_func),
        mDeserializeFunc(deserialize_func),
        mSerializeFunc(serialize_func),
        mValidationFunc(validation_func),
        mMinCount(min_count),
        mMaxCount(max_count)
    {}

    ParamDescriptor::ParamDescriptor()
    :   mParamHandle(0),
        mMergeFunc(NULL),
        mDeserializeFunc(NULL),
        mSerializeFunc(NULL),
        mValidationFunc(NULL),
        mMinCount(0),
        mMaxCount(0)
    {}

    //
    // Parser
    //
    void Parser::parserWarning(const std::string& message)
    {
        if (mParseSilently) return;
        LL_WARNS() << message << LL_ENDL;
    }

    void Parser::parserError(const std::string& message)
    {
        if (mParseSilently) return;
        LL_ERRS() << message << LL_ENDL;
    }


    //
    // BlockDescriptor
    //
    namespace
    {
        // Descriptors are registered once per block type and read for the life
        // of the process, so they are cut from chunks that are never freed.
        // One per allocation is what a std::deque gave: MSVC hands a deque
        // holding anything above eight bytes exactly one element per block, so
        // every descriptor carried an allocation header of its own.
        //
        // The lock is not for contention -- a descriptor is allocated once,
        // during a block type's first construction -- but because which thread
        // that happens on is not ours to say.
        constexpr std::size_t DESCRIPTORS_PER_CHUNK = 64;

        ParamDescriptor* allocateDescriptor()
        {
            static std::mutex sChunkMutex;
            static std::vector<std::unique_ptr<ParamDescriptor[]> > sChunks;
            static std::size_t sUsedInChunk = DESCRIPTORS_PER_CHUNK;

            std::lock_guard<std::mutex> lock(sChunkMutex);
            if (sUsedInChunk == DESCRIPTORS_PER_CHUNK)
            {
                sChunks.push_back(std::make_unique<ParamDescriptor[]>(DESCRIPTORS_PER_CHUNK));
                sUsedInChunk = 0;
            }
            return &sChunks.back()[sUsedInChunk++];
        }
    }

    void BlockDescriptor::addParam(param_handle_t handle,
                                   ParamDescriptor::merge_func_t merge_func,
                                   ParamDescriptor::deserialize_func_t deserialize_func,
                                   ParamDescriptor::serialize_func_t serialize_func,
                                   ParamDescriptor::validation_func_t validation_func,
                                   S32 min_count,
                                   S32 max_count,
                                   const char* char_name)
    {
        if ((size_t)handle > mMaxParamOffset)
        {
            LL_ERRS() << "Attempted to register param with block defined for parent class, make sure to derive from LLInitParam::Block<YOUR_CLASS, PARAM_BLOCK_BASE_CLASS>" << LL_ENDL;
        }

        ParamDescriptorPtr param = allocateDescriptor();
        *param = ParamDescriptor(handle, merge_func, deserialize_func, serialize_func,
                                 validation_func, min_count, max_count);

        mAllParams.push_back(param);

        if (!char_name[0])
        {
            mUnnamedParams.push_back(param);
        }
        else
        {
            // don't use insert, since we want to overwrite existing entries
            mNamedParams[char_name] = param;
        }

        if (validation_func)
        {
            mValidationList.emplace_back(handle, validation_func);
        }
    }

    static std::vector<BlockDescriptor*>& block_descriptor_registry()
    {
        // Function-local so it is built before the first descriptor that
        // registers into it, whatever order the statics initialize in.
        static std::vector<BlockDescriptor*> sRegistry;
        return sRegistry;
    }

    BlockDescriptor::BlockDescriptor()
    :   mBaseDescriptor(NULL),
        mTypeName(""),
        mMaxParamOffset(0),
        mInitializationState(UNINITIALIZED),
        mCurrentBlockPtr(NULL)
    {
        block_descriptor_registry().push_back(this);
    }

    ParamDescriptorPtr BlockDescriptor::findNamedParam(std::string_view name) const
    {
        for (const BlockDescriptor* descriptor = this; descriptor; descriptor = descriptor->mBaseDescriptor)
        {
            param_map_t::const_iterator found = descriptor->mNamedParams.find(name);
            if (found != descriptor->mNamedParams.end())
            {
                return found->second;
            }
        }
        return NULL;
    }

    std::vector<std::pair<std::string_view, ParamDescriptorPtr> > BlockDescriptor::namedParams() const
    {
        std::vector<std::pair<std::string_view, ParamDescriptorPtr> > named;
        std::unordered_set<std::string_view> seen;

        for (const BlockDescriptor* descriptor = this; descriptor; descriptor = descriptor->mBaseDescriptor)
        {
            for (const param_map_t::value_type& pair : descriptor->mNamedParams)
            {
                // Nearest first, so the first spelling of a name wins and a
                // base's shadowed one is passed over.
                if (seen.insert(pair.first).second)
                {
                    named.push_back(pair);
                }
            }
        }
        return named;
    }

    // static
    const std::vector<BlockDescriptor*>& BlockDescriptor::getAllDescriptors()
    {
        return block_descriptor_registry();
    }

    // static
    std::string BlockDescriptor::getStatsReport()
    {
        const std::vector<BlockDescriptor*>& all = getAllDescriptors();

        size_t own_entries = 0;
        size_t effective_entries = 0;
        size_t unnamed_entries = 0;
        size_t all_param_entries = 0;
        size_t validated_entries = 0;
        size_t deepest_chain = 0;
        std::unordered_set<std::string_view> distinct_names;
        std::unordered_set<const ParamDescriptor*> distinct_descriptors;

        for (const BlockDescriptor* descriptor : all)
        {
            own_entries += descriptor->mNamedParams.size();
            unnamed_entries += descriptor->mUnnamedParams.size();
            all_param_entries += descriptor->mAllParams.size();
            validated_entries += descriptor->mValidationList.size();
            effective_entries += descriptor->namedParams().size();

            size_t depth = 0;
            for (const BlockDescriptor* walk = descriptor; walk; walk = walk->mBaseDescriptor)
            {
                ++depth;
            }
            deepest_chain = llmax(deepest_chain, depth);

            for (const param_map_t::value_type& pair : descriptor->mNamedParams)
            {
                distinct_names.emplace(pair.first);
                distinct_descriptors.emplace(pair.second);
            }
            for (ParamDescriptorPtr ptr : descriptor->mUnnamedParams)
            {
                distinct_descriptors.emplace(ptr);
            }
        }

        // A named entry in a flat map is a view and a pointer, with no node of
        // its own. Only the entries a block declares itself are stored; a name
        // it inherits is found by walking to the base that declared it, which
        // is what effective_entries counts and own_entries does not.
        const size_t named_node_bytes = own_entries * (sizeof(std::string_view) + sizeof(ParamDescriptorPtr));
        const size_t list_node_bytes = all_param_entries * sizeof(ParamDescriptorPtr);
        const size_t descriptor_bytes = distinct_descriptors.size() * sizeof(ParamDescriptor);

        std::ostringstream out;
        out << "LLInitParam block descriptors\n"
            << "  block types registered : " << all.size() << "\n"
            << "  named entries stored   : " << own_entries << "\n"
            << "  named entries reachable: " << effective_entries
            << " (what each block answers to, bases included)\n"
            << "  unnamed param entries  : " << unnamed_entries << "\n"
            << "  param table entries    : " << all_param_entries << "\n"
            << "  distinct descriptors   : " << distinct_descriptors.size() << "\n"
            << "  validated params       : " << validated_entries << "\n"
            << "  distinct param names   : " << distinct_names.size()
            << " (of " << own_entries << " stored)\n"
            << "  deepest block chain    : " << deepest_chain << "\n"
            << "  named map entries      : ~" << named_node_bytes << " bytes\n"
            << "  mAllParams vector      : ~" << list_node_bytes << " bytes\n"
            << "  descriptor objects     : ~" << descriptor_bytes << " bytes\n"
            << "  approximate total      : ~"
            << (named_node_bytes + list_node_bytes + descriptor_bytes) << " bytes\n";

        // A name declared at two levels of one chain resolves to the nearer,
        // which is how Ignored swallows an attribute a base would otherwise
        // act on -- a menu item takes its geometry from its menu, so
        // LLMenuItemGL::Params declares rect and the deltas Ignored over
        // LLView::Params. An Ignored param registers with neither a merge nor
        // a serialize function, so the two cases are told apart here: the
        // second list is two real parameters answering to one name, where one
        // of them is unreachable from XUI and only the block that declared it
        // can read what it holds.
        std::vector<std::string> swallowed;
        std::vector<std::string> collisions;
        for (const BlockDescriptor* descriptor : all)
        {
            for (const param_map_t::value_type& pair : descriptor->mNamedParams)
            {
                for (const BlockDescriptor* base = descriptor->mBaseDescriptor; base; base = base->mBaseDescriptor)
                {
                    param_map_t::const_iterator found = base->mNamedParams.find(pair.first);
                    if (found == base->mNamedParams.end() || found->second == pair.second)
                    {
                        continue;
                    }

                    std::ostringstream line;
                    line << "    " << pair.first << " : " << descriptor->mTypeName
                         << " over " << base->mTypeName << "\n";
                    const bool ignored = !pair.second->mSerializeFunc && !pair.second->mMergeFunc;
                    (ignored ? swallowed : collisions).push_back(line.str());
                }
            }
        }
        std::sort(swallowed.begin(), swallowed.end());
        std::sort(collisions.begin(), collisions.end());

        out << "  names an Ignored swallows : " << swallowed.size() << "\n";
        for (const std::string& line : swallowed)
        {
            out << line;
        }
        out << "  names two params share    : " << collisions.size() << "\n";
        for (const std::string& line : collisions)
        {
            out << line;
        }

        return out.str();
    }

    // called by each derived class in least to most derived order
    void BaseBlock::init(BlockDescriptor& descriptor, BlockDescriptor& base_descriptor, size_t block_size, const char* type_name)
    {
        descriptor.mCurrentBlockPtr = this;
        descriptor.mMaxParamOffset = block_size;

        switch(descriptor.mInitializationState)
        {
        case BlockDescriptor::UNINITIALIZED:
            descriptor.mTypeName = type_name;
            // A block with no base of its own is handed its own descriptor,
            // which would make findNamedParam walk in a circle.
            descriptor.mBaseDescriptor = (&base_descriptor == &descriptor) ? NULL : &base_descriptor;
            std::copy(base_descriptor.mUnnamedParams.begin(), base_descriptor.mUnnamedParams.end(),
                      std::back_inserter(descriptor.mUnnamedParams));
            descriptor.mInitializationState = BlockDescriptor::INITIALIZING;
            break;
        case BlockDescriptor::INITIALIZING:
            descriptor.mInitializationState = BlockDescriptor::INITIALIZED;
            break;
        case BlockDescriptor::INITIALIZED:
            // nothing to do
            break;
        }
    }

    param_handle_t BaseBlock::getHandleFromParam(const Param* param) const
    {
        const U8* param_address = reinterpret_cast<const U8*>(param);
        const U8* baseblock_address = reinterpret_cast<const U8*>(this);
        return (param_address - baseblock_address);
    }

    bool BaseBlock::submitValue(Parser::name_stack_t& name_stack, Parser& p, bool silent)
    {
        Parser::name_stack_range_t range = std::make_pair(name_stack.begin(), name_stack.end());
        if (!deserializeBlock(p, range, true))
        {
            if (!silent)
            {
                p.parserWarning(llformat("Failed to parse parameter \"%s\"", p.getCurrentElementName().c_str()));
            }
            return false;
        }
        return true;
    }


    bool BaseBlock::validateBlock(bool emit_errors) const
    {
        // only validate block when it hasn't already passed validation with current data
        if (!mValidated)
        {
            const BlockDescriptor& block_data = mostDerivedBlockDescriptor();
            const bool failed = block_data.anyValidator(
                [&](const BlockDescriptor::param_validation_list_t::value_type& pair)
                {
                    const Param* param = getParamFromHandle(pair.first);
                    if (pair.second(param))
                    {
                        return false;
                    }
                    if (emit_errors)
                    {
                        LL_WARNS() << "Invalid param \"" << getParamName(block_data, param) << "\"" << LL_ENDL;
                    }
                    return true;
                });
            if (failed)
            {
                return false;
            }
            mValidated = true;
        }
        return mValidated;
    }

    namespace
    {
        // A block has one or two of these, so a scan beats building a set of
        // them for every named param the caller is about to write.
        bool isUnnamedParam(const BlockDescriptor& block_data, param_handle_t handle)
        {
            for (ParamDescriptorPtr ptr : block_data.mUnnamedParams)
            {
                if (ptr->mParamHandle == handle)
                {
                    return true;
                }
            }
            return false;
        }
    }

    bool BaseBlock::serializeBlock(Parser& parser, Parser::name_stack_t& name_stack, const predicate_rule_t predicate_rule, const LLInitParam::BaseBlock* diff_block) const
    {
        bool serialized = false;
        if (!predicate_rule.check(ll_make_predicate(PROVIDED, isProvided())))
        {
            return false;
        }
        // named param is one like LLView::Params::follows
        // unnamed param is like LLView::Params::rect - implicit
        const BlockDescriptor& block_data = mostDerivedBlockDescriptor();

        for (ParamDescriptorPtr ptr : block_data.mUnnamedParams)
        {
            param_handle_t param_handle = ptr->mParamHandle;
            const Param* param = getParamFromHandle(param_handle);
            ParamDescriptor::serialize_func_t serialize_func = ptr->mSerializeFunc;
            if (serialize_func && predicate_rule.check(ll_make_predicate(PROVIDED, param->anyProvided())))
            {
                const Param* diff_param = diff_block ? diff_block->getParamFromHandle(param_handle) : NULL;
                serialized |= serialize_func(*param, parser, name_stack, predicate_rule, diff_param);
            }
        }

        // Named params are held by the block that declared them, so walk out
        // to the bases rather than expecting one flat table.
        for (const BlockDescriptor* descriptor = &block_data; descriptor; descriptor = descriptor->mBaseDescriptor)
        {
            for (const BlockDescriptor::param_map_t::value_type& pair : descriptor->mNamedParams)
            {
                param_handle_t param_handle = pair.second->mParamHandle;
                const Param* param = getParamFromHandle(param_handle);
                ParamDescriptor::serialize_func_t serialize_func = pair.second->mSerializeFunc;
                if (!serialize_func || !predicate_rule.check(ll_make_predicate(PROVIDED, param->anyProvided())))
                {
                    continue;
                }

                // Write the entry a lookup would reach. A name a derived block
                // redeclares appears at two levels, and the one it shadows is
                // unreachable -- writing it too would emit the name twice.
                if (block_data.findNamedParam(pair.first) != pair.second)
                {
                    continue;
                }

                // Ensure this param has not already been serialized
                // Prevents <rect> from being serialized as its own tag.
                //FIXME: for now, don't attempt to serialize values under synonyms, as current parsers
                // don't know how to detect them
                if (isUnnamedParam(block_data, param_handle))
                {
                    continue;
                }

                name_stack.emplace_back(pair.first, true);
                const Param* diff_param = diff_block ? diff_block->getParamFromHandle(param_handle) : NULL;
                serialized |= serialize_func(*param, parser, name_stack, predicate_rule, diff_param);
                name_stack.pop_back();
            }
        }

        if (!serialized && predicate_rule.check(ll_make_predicate(EMPTY)))
        {
            serialized |= parser.writeValue(Flag(), name_stack);
        }
        // was anything serialized in this block?
        return serialized;
    }

    bool BaseBlock::deserializeBlock(Parser& p, Parser::name_stack_range_t& name_stack_range, bool ignored)
    {
        BlockDescriptor& block_data = mostDerivedBlockDescriptor();
        bool names_left = name_stack_range.first != name_stack_range.second;

        bool new_name = names_left
                        ? name_stack_range.first->second
                        : true;

        if (names_left)
        {
            const std::string_view top_name = name_stack_range.first->first;

            ParamDescriptorPtr found = block_data.findNamedParam(top_name);
            if (found)
            {
                // find pointer to member parameter from offset table
                Param* paramp = getParamFromHandle(found->mParamHandle);
                ParamDescriptor::deserialize_func_t deserialize_func = found->mDeserializeFunc;

                Parser::name_stack_range_t new_name_stack(name_stack_range.first, name_stack_range.second);
                ++new_name_stack.first;
                if (deserialize_func(*paramp, p, new_name_stack, new_name))
                {
                    // value is no longer new, we know about it now
                    name_stack_range.first->second = false;
                    return true;
                }
                else
                {
                    return false;
                }
            }
        }

        // try to parse unnamed parameters, in declaration order
        for (ParamDescriptorPtr ptr : block_data.mUnnamedParams)
        {
            Param* paramp = getParamFromHandle(ptr->mParamHandle);
            ParamDescriptor::deserialize_func_t deserialize_func = ptr->mDeserializeFunc;

            if (deserialize_func && deserialize_func(*paramp, p, name_stack_range, new_name))
            {
                return true;
            }
        }

        // if no match, and no names left on stack, this is just an existence assertion of this block
        // verify by calling readValue with NoParamValue type, an inherently unparseable type
        if (!names_left)
        {
            Flag no_value;
            return p.readValue(no_value);
        }

        return false;
    }

    void BaseBlock::addSynonymImpl(Param& param, std::string_view synonym)
    {
        BlockDescriptor& block_data = mostDerivedBlockDescriptor();
        if (block_data.mInitializationState == BlockDescriptor::INITIALIZING)
        {
            param_handle_t handle = getHandleFromParam(&param);

            // check for invalid derivation from a paramblock (i.e. without using
            // Block<T, Base_Class>
            if ((size_t)handle > block_data.mMaxParamOffset)
            {
                LL_ERRS() << "Attempted to register param with block defined for parent class, make sure to derive from LLInitParam::Block<YOUR_CLASS, PARAM_BLOCK_BASE_CLASS>" << LL_ENDL;
            }

            ParamDescriptorPtr param_descriptor = findParamDescriptor(param);
            if (param_descriptor)
            {
                if (synonym.empty())
                {
                    block_data.mUnnamedParams.push_back(param_descriptor);
                }
                else
                {
                    block_data.mNamedParams[synonym] = param_descriptor;
                }
            }
        }
    }

    std::string_view BaseBlock::getParamName(const BlockDescriptor& block_data, const Param* paramp) const
    {
        param_handle_t handle = getHandleFromParam(paramp);
        for (const BlockDescriptor* descriptor = &block_data; descriptor; descriptor = descriptor->mBaseDescriptor)
        {
            for (const BlockDescriptor::param_map_t::value_type& pair : descriptor->mNamedParams)
            {
                if (pair.second->mParamHandle == handle)
                {
                    return pair.first;
                }
            }
        }

        return {};
    }

    ParamDescriptorPtr BaseBlock::findParamDescriptor(const Param& param)
    {
        param_handle_t handle = getHandleFromParam(&param);
        BlockDescriptor& descriptor = mostDerivedBlockDescriptor();
        ParamDescriptorPtr found = nullptr;
        descriptor.anyParam([&](ParamDescriptorPtr ptr)
        {
            if (ptr->mParamHandle != handle)
            {
                return false;
            }
            found = ptr;
            return true;
        });
        return found;
    }

    // take all provided params from other and apply to self
    // NOTE: this requires that "other" is of the same derived type as this
    bool BaseBlock::mergeBlock(BlockDescriptor& block_data, const BaseBlock& other, bool overwrite)
    {
        bool some_param_changed = false;
        block_data.anyParam([&](ParamDescriptorPtr ptr)
        {
            ParamDescriptor::merge_func_t merge_func = ptr->mMergeFunc;
            if (merge_func)
            {
                const Param* other_paramp = other.getParamFromHandle(ptr->mParamHandle);
                Param* paramp = getParamFromHandle(ptr->mParamHandle);
                llassert(paramp->getEnclosingBlockOffset() == ptr->mParamHandle);
                some_param_changed |= merge_func(*paramp, *other_paramp, overwrite);
            }
            return false;
        });
        return some_param_changed;
    }
}
