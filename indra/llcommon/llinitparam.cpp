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

#include <sstream>
#include <unordered_set>


namespace LLInitParam
{

    predicate_rule_t default_parse_rules()
    {
        return ll_make_predicate(PROVIDED) && !ll_make_predicate(EMPTY);
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
    void BlockDescriptor::aggregateBlockData(BlockDescriptor& src_block_data)
    {
        mNamedParams.insert(src_block_data.mNamedParams.begin(), src_block_data.mNamedParams.end());
        std::copy(src_block_data.mUnnamedParams.begin(), src_block_data.mUnnamedParams.end(), std::back_inserter(mUnnamedParams));
        std::copy(src_block_data.mValidationList.begin(), src_block_data.mValidationList.end(), std::back_inserter(mValidationList));
        std::copy(src_block_data.mAllParams.begin(), src_block_data.mAllParams.end(), std::back_inserter(mAllParams));
    }

    void BlockDescriptor::addParam(const ParamDescriptorPtr in_param, const char* char_name)
    {
        // create a copy of the param descriptor in mAllParams
        // so other data structures can store a pointer to it
        mAllParams.push_back(in_param);
        ParamDescriptorPtr param(mAllParams.back());

        std::string name(char_name);
        if ((size_t)param->mParamHandle > mMaxParamOffset)
        {
            LL_ERRS() << "Attempted to register param with block defined for parent class, make sure to derive from LLInitParam::Block<YOUR_CLASS, PARAM_BLOCK_BASE_CLASS>" << LL_ENDL;
        }

        if (name.empty())
        {
            mUnnamedParams.push_back(param);
        }
        else
        {
            // don't use insert, since we want to overwrite existing entries
            mNamedParams[name] = param;
        }

        if (param->mValidationFunc)
        {
            mValidationList.emplace_back(param->mParamHandle, param->mValidationFunc);
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
    :   mMaxParamOffset(0),
        mInitializationState(UNINITIALIZED),
        mCurrentBlockPtr(NULL)
    {
        block_descriptor_registry().push_back(this);
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

        size_t named_entries = 0;
        size_t unnamed_entries = 0;
        size_t owned_descriptors = 0;
        size_t validated_entries = 0;
        size_t name_bytes = 0;
        std::unordered_set<std::string> distinct_names;

        for (const BlockDescriptor* descriptor : all)
        {
            named_entries += descriptor->mNamedParams.size();
            unnamed_entries += descriptor->mUnnamedParams.size();
            owned_descriptors += descriptor->mAllParams.size();
            validated_entries += descriptor->mValidationList.size();

            for (const param_map_t::value_type& pair : descriptor->mNamedParams)
            {
                name_bytes += pair.first.capacity();
                distinct_names.insert(pair.first);
            }
        }

        // A named entry is a hash node holding a std::string and a
        // shared_ptr; an mAllParams entry is a list node holding a
        // shared_ptr; each descriptor carries its own control block because
        // it is allocated with new rather than make_shared.
        const size_t named_node_bytes = named_entries * (sizeof(void*) + sizeof(size_t) + sizeof(std::string) + sizeof(ParamDescriptorPtr));
        const size_t list_node_bytes = owned_descriptors * (2 * sizeof(void*) + sizeof(ParamDescriptorPtr));
        const size_t descriptor_bytes = owned_descriptors * (sizeof(ParamDescriptor) + 3 * sizeof(void*));

        size_t distinct_name_bytes = 0;
        for (const std::string& name : distinct_names)
        {
            distinct_name_bytes += name.capacity();
        }

        std::ostringstream out;
        out << "LLInitParam block descriptors\n"
            << "  block types registered : " << all.size() << "\n"
            << "  named param entries    : " << named_entries << "\n"
            << "  unnamed param entries  : " << unnamed_entries << "\n"
            << "  descriptors owned      : " << owned_descriptors << "\n"
            << "  validated params       : " << validated_entries << "\n"
            << "  distinct param names   : " << distinct_names.size()
            << " (of " << named_entries << " entries)\n"
            << "  name string bytes      : " << name_bytes
            << " (" << distinct_name_bytes << " if interned)\n"
            << "  named map nodes        : ~" << named_node_bytes << " bytes\n"
            << "  mAllParams list nodes  : ~" << list_node_bytes << " bytes\n"
            << "  descriptor objects     : ~" << descriptor_bytes << " bytes\n"
            << "  approximate total      : ~"
            << (named_node_bytes + list_node_bytes + descriptor_bytes + name_bytes) << " bytes\n";
        return out.str();
    }

    // called by each derived class in least to most derived order
    void BaseBlock::init(BlockDescriptor& descriptor, BlockDescriptor& base_descriptor, size_t block_size)
    {
        descriptor.mCurrentBlockPtr = this;
        descriptor.mMaxParamOffset = block_size;

        switch(descriptor.mInitializationState)
        {
        case BlockDescriptor::UNINITIALIZED:
            // copy params from base class here
            descriptor.aggregateBlockData(base_descriptor);

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
        for (const BlockDescriptor::param_validation_list_t::value_type& pair : block_data.mValidationList)
        {
            const Param* param = getParamFromHandle(pair.first);
            if (!pair.second(param))
            {
                if (emit_errors)
                {
                    LL_WARNS() << "Invalid param \"" << getParamName(block_data, param) << "\"" << LL_ENDL;
                }
                return false;
            }
        }
            mValidated = true;
        }
        return mValidated;
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

        for (const ParamDescriptorPtr& ptr : block_data.mUnnamedParams)
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

        // Precompute the set of unnamed (implicit) param handles so the loop
        // below can skip already-serialized params in O(1) instead of rescanning
        // mUnnamedParams for every named param.
        std::unordered_set<param_handle_t> unnamed_handles;
        unnamed_handles.reserve(block_data.mUnnamedParams.size());
        for (const ParamDescriptorPtr& ptr : block_data.mUnnamedParams)
        {
            unnamed_handles.insert(ptr->mParamHandle);
        }

        for (const BlockDescriptor::param_map_t::value_type& pair : block_data.mNamedParams)
        {
            param_handle_t param_handle = pair.second->mParamHandle;
            const Param* param = getParamFromHandle(param_handle);
            ParamDescriptor::serialize_func_t serialize_func = pair.second->mSerializeFunc;
            if (serialize_func && predicate_rule.check(ll_make_predicate(PROVIDED, param->anyProvided())))
            {
                // Ensure this param has not already been serialized
                // Prevents <rect> from being serialized as its own tag.
                //FIXME: for now, don't attempt to serialize values under synonyms, as current parsers
                // don't know how to detect them
                if (unnamed_handles.find(param_handle) != unnamed_handles.end())
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
            const std::string& top_name = name_stack_range.first->first;

            BlockDescriptor::param_map_t::iterator found_it = block_data.mNamedParams.find(top_name);
            if (found_it != block_data.mNamedParams.end())
            {
                // find pointer to member parameter from offset table
                Param* paramp = getParamFromHandle(found_it->second->mParamHandle);
                ParamDescriptor::deserialize_func_t deserialize_func = found_it->second->mDeserializeFunc;

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
        for (ParamDescriptorPtr& ptr : block_data.mUnnamedParams)
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

    void BaseBlock::addSynonym(Param& param, const std::string& synonym)
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

    const std::string& BaseBlock::getParamName(const BlockDescriptor& block_data, const Param* paramp) const
    {
        param_handle_t handle = getHandleFromParam(paramp);
        for (BlockDescriptor::param_map_t::const_iterator it = block_data.mNamedParams.begin(); it != block_data.mNamedParams.end(); ++it)
        {
            if (it->second->mParamHandle == handle)
            {
                return it->first;
            }
        }

        return LLStringUtil::null;
    }

    ParamDescriptorPtr BaseBlock::findParamDescriptor(const Param& param)
    {
        param_handle_t handle = getHandleFromParam(&param);
        BlockDescriptor& descriptor = mostDerivedBlockDescriptor();
        for (ParamDescriptorPtr& ptr : descriptor.mAllParams)
        {
            if (ptr->mParamHandle == handle) return ptr;
        }
        return ParamDescriptorPtr();
    }

    // take all provided params from other and apply to self
    // NOTE: this requires that "other" is of the same derived type as this
    bool BaseBlock::mergeBlock(BlockDescriptor& block_data, const BaseBlock& other, bool overwrite)
    {
        bool some_param_changed = false;
        for (const ParamDescriptorPtr& ptr : block_data.mAllParams)
        {
            const Param* other_paramp = other.getParamFromHandle(ptr->mParamHandle);
            ParamDescriptor::merge_func_t merge_func = ptr->mMergeFunc;
            if (merge_func)
            {
                Param* paramp = getParamFromHandle(ptr->mParamHandle);
                llassert(paramp->getEnclosingBlockOffset() == ptr->mParamHandle);
                some_param_changed |= merge_func(*paramp, *other_paramp, overwrite);
            }
        }
        return some_param_changed;
    }
}
