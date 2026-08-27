/**
 * @file llxuiparser.cpp
 * @brief Utility functions for handling XUI structures in XML
 *
 * $LicenseInfo:firstyear=2003&license=viewerlgpl$
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

#include "llxuiparser.h"

#include "alxmldocument.h"

#include "llxmlnode.h"
#include "llfasttimer.h"


#include <algorithm>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <fstream>
#if LL_DARWIN
#include <xlocale.h>
#endif
#include <vector>

#include "lluicolor.h"
#include "v3math.h"

const S32 MAX_STRING_ATTRIBUTE_SIZE = 40;


static  LLInitParam::Parser::parser_read_func_map_t sSimpleXUIReadFuncs;
static  LLInitParam::Parser::parser_write_func_map_t sSimpleXUIWriteFuncs;

const char* NO_VALUE_MARKER = "no_value";


static  LLInitParam::Parser::parser_read_func_map_t sXUIReadFuncs;
static  LLInitParam::Parser::parser_write_func_map_t sXUIWriteFuncs;

//
// LLXUIParser
//
LLXUIParser::LLXUIParser()
:   Parser(sXUIReadFuncs, sXUIWriteFuncs),
    mCurReadDepth(0)
{
    if (sXUIReadFuncs.empty())
    {
        registerParserFuncs<LLInitParam::Flag>(readFlag, writeFlag);
        registerParserFuncs<bool>(readBoolValue, writeBoolValue);
        registerParserFuncs<std::string>(readStringValue, writeStringValue);
        registerParserFuncs<U8>(readU8Value, writeU8Value);
        registerParserFuncs<S8>(readS8Value, writeS8Value);
        registerParserFuncs<U16>(readU16Value, writeU16Value);
        registerParserFuncs<S16>(readS16Value, writeS16Value);
        registerParserFuncs<U32>(readU32Value, writeU32Value);
        registerParserFuncs<S32>(readS32Value, writeS32Value);
        registerParserFuncs<F32>(readF32Value, writeF32Value);
        registerParserFuncs<F64>(readF64Value, writeF64Value);
        registerParserFuncs<LLVector3>(readVector3Value, writeVector3Value);
        registerParserFuncs<LLColor4>(readColor4Value, writeColor4Value);
        registerParserFuncs<LLUIColor>(readUIColorValue, writeUIColorValue);
        registerParserFuncs<LLUUID>(readUUIDValue, writeUUIDValue);
        registerParserFuncs<LLSD>(readSDValue, writeSDValue);
    }
}

static S32 pushDottedName(LLInitParam::Parser::name_stack_t& stack, const char* name);

const LLXMLNodePtr DUMMY_NODE = new LLXMLNode();

void LLXUIParser::readXUI(LLXMLNodePtr node, LLInitParam::BaseBlock& block, const std::string& filename, bool silent)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

    if (node.isNull())
    {
        // Can't use parserWarning() here: it dereferences mCurReadNode, which
        // is null/stale before any node of this parse has been read.
        LL_WARNS() << "Invalid (null) node passed to readXUI" << LL_ENDL;
        return;
    }

    mNameStack.clear();
    mRootNodeName = node->getName()->mString;
    mCurFileName = filename;
    mCurReadDepth = 0;
    setParseSilently(silent);

    readXUIImpl(node, block);
}

bool LLXUIParser::readXUIImpl(LLXMLNodePtr nodep, LLInitParam::BaseBlock& block)
{
    bool values_parsed = false;
    bool silent = mCurReadDepth > 0;

    // getSanitizedValue() does real work, and for most element nodes there is
    // no work to do: whatever sits between child elements is whitespace, which
    // sanitizes to nothing. Ask first, then compute once and reuse it for both
    // the empty-node check and the "value" parameter below.
    std::string text_contents;
    if (nodep->hasTextContents())
    {
        nodep->getSanitizedValue(text_contents);
    }

    if (nodep->getFirstChild().isNull()
        && nodep->mAttributes.empty()
        && text_contents.empty())
    {
        // empty node, just parse as flag
        mCurReadNode = DUMMY_NODE;
        return block.submitValue(mNameStack, *this, silent);
    }

    // submit attributes for current node
    values_parsed |= readAttributes(nodep, block);

    // treat text contents of xml node as "value" parameter
    if (!text_contents.empty())
    {
        mCurReadNode = nodep;
        mNameStack.emplace_back("value", true);
        // child nodes are not necessarily valid parameters (could be a child widget)
        // so don't complain once we've recursed
        if (!block.submitValue(mNameStack, *this, true))
        {
            mNameStack.pop_back();
            block.submitValue(mNameStack, *this, silent);
        }
        else
        {
            mNameStack.pop_back();
        }
    }

    // then traverse children
    // child node must start with last name of parent node (our "scope")
    // for example: "<button><button.param nested_param1="foo"><param.nested_param2 nested_param3="bar"/></button.param></button>"
    // which equates to the following nesting:
    // button
    //     param
    //         nested_param1
    //         nested_param2
    //             nested_param3
    mCurReadDepth++;
    for(LLXMLNodePtr childp = nodep->getFirstChild(); childp.notNull();)
    {
        // The name belongs to the string table and outlives this parse, so the
        // stack can hold runs of it rather than copies.
        const std::string_view child_name(childp->getName()->mString);
        S32 num_tokens_pushed = 0;

        // for non "dotted" child nodes check to see if child node maps to another widget type
        // and if not, treat as a child element of the current node
        // e.g. <button><rect left="10"/></button> will interpret <rect> as "button.rect"
        // since there is no widget named "rect"
        const std::size_t first_dot = child_name.find('.');
        if (first_dot == std::string_view::npos)
        {
            mNameStack.emplace_back(child_name, true);
            num_tokens_pushed++;
        }
        else
        {
            // check for proper nesting against the leading token
            const std::string_view head = child_name.substr(0, first_dot);
            if (mNameStack.empty())
            {
                if (head != mRootNodeName)
                {
                    childp = childp->getNextSibling();
                    continue;
                }
            }
            else if (mNameStack.back().first != head)
            {
                childp = childp->getNextSibling();
                continue;
            }

            // push everything after the leading token
            num_tokens_pushed += pushDottedName(mNameStack, childp->getName()->mString + first_dot + 1);
        }

        // recurse and visit children XML nodes
        if(readXUIImpl(childp, block))
        {
            // child node successfully parsed, remove from DOM

            values_parsed = true;
            LLXMLNodePtr node_to_remove = childp;
            childp = childp->getNextSibling();

            nodep->deleteChild(node_to_remove);
        }
        else
        {
            childp = childp->getNextSibling();
        }

        while(num_tokens_pushed-- > 0)
        {
            mNameStack.pop_back();
        }
    }
    mCurReadDepth--;
    return values_parsed;
}

// A parameter name is a dot separated path, but almost never has a dot in it:
// across the shipped skin, 349 of 109204 attribute names do. Splitting one that
// holds no separator returns the name it was given, so do nothing but push it.
//
// The name belongs to the caller and outlives the parse, so the pieces of a
// dotted one are runs inside it rather than copies. Note that a run is not NUL
// terminated -- anything wanting a C string from the stack must make one.
// Returns how many names were pushed.
static S32 pushDottedName(LLInitParam::Parser::name_stack_t& stack, const char* name)
{
    const std::string_view whole(name);
    if (whole.empty())
    {
        return 0;
    }

    if (whole.find('.') == std::string_view::npos)
    {
        stack.emplace_back(whole, true);
        return 1;
    }

    S32 pushed = 0;
    for (std::size_t begin = 0; begin <= whole.size(); )
    {
        const std::size_t dot = whole.find('.', begin);
        const std::size_t end = (dot == std::string_view::npos) ? whole.size() : dot;
        if (end > begin)
        {
            stack.emplace_back(whole.substr(begin, end - begin), true);
            ++pushed;
        }
        if (dot == std::string_view::npos)
        {
            break;
        }
        begin = dot + 1;
    }
    return pushed;
}

S32 LLXUIParser::pushNameTokens(const char* name)
{
    return pushDottedName(mNameStack, name);
}

bool LLXUIParser::readAttributes(LLXMLNodePtr nodep, LLInitParam::BaseBlock& block)
{
    bool any_parsed = false;
    bool silent = mCurReadDepth > 0;

    for (const auto& [name_entry, attribute_node] : nodep->mAttributes)
    {
        const char* const attribute_name = name_entry->mString;
        mCurReadNode = attribute_node;

        S32 num_tokens_pushed = pushNameTokens(attribute_name);

        // child nodes are not necessarily valid attributes, so don't complain once we've recursed
        any_parsed |= block.submitValue(mNameStack, *this, silent);

        while(num_tokens_pushed-- > 0)
        {
            mNameStack.pop_back();
        }
    }

    return any_parsed;
}

void LLXUIParser::writeXUIImpl(LLXMLNodePtr node, const LLInitParam::BaseBlock &block, const LLInitParam::predicate_rule_t rules, const LLInitParam::BaseBlock* diff_block)
{
    mWriteRootNode = node;
    name_stack_t name_stack = Parser::name_stack_t();
    block.serializeBlock(*this, name_stack, rules, diff_block);
    mOutNodes.clear();
}

// go from a stack of names to a specific XML node
LLXMLNodePtr LLXUIParser::getNode(name_stack_t& stack)
{
    LLXMLNodePtr out_node = mWriteRootNode;

    name_stack_t::iterator next_it = stack.begin();
    for (name_stack_t::iterator it = stack.begin();
        it != stack.end();
        it = next_it)
    {
        ++next_it;
        bool force_new_node = false;

        if (it->first.empty())
        {
            it->second = false;
            continue;
        }

        if (next_it != stack.end() && next_it->first.empty() && next_it->second)
        {
            force_new_node = true;
        }


        // A run inside a dotted name has no terminator of its own, so the
        // write path materializes one. It runs for toolbars, keybindings
        // and colors, not for building widgets.
        const std::string name(it->first);
        out_nodes_t::iterator found_it = mOutNodes.find(name);

        // node with this name not yet written
        if (found_it == mOutNodes.end() || it->second || force_new_node)
        {
            // make an attribute if we are the last element on the name stack
            bool is_attribute = next_it == stack.end();
            LLXMLNodePtr new_node = new LLXMLNode(name.c_str(), is_attribute);
            out_node->addChild(new_node);
            mOutNodes[name] = new_node;
            out_node = new_node;
            it->second = false;
        }
        else
        {
            out_node = found_it->second;
        }
    }

    return (out_node == mWriteRootNode ? LLXMLNodePtr(NULL) : out_node);
}

bool LLXUIParser::readFlag(Parser& parser, void* val_ptr)
{
    LLXUIParser& self = static_cast<LLXUIParser&>(parser);
    return self.mCurReadNode == DUMMY_NODE;
}

bool LLXUIParser::writeFlag(Parser& parser, const void* val_ptr, name_stack_t& stack)
{
    // just create node
    LLXUIParser& self = static_cast<LLXUIParser&>(parser);
    LLXMLNodePtr node = self.getNode(stack);
    return node.notNull();
}

bool LLXUIParser::readBoolValue(Parser& parser, void* val_ptr)
{
    bool value;
    LLXUIParser& self = static_cast<LLXUIParser&>(parser);
    bool success = self.mCurReadNode->getBoolValue(1, &value);
    *((bool*)val_ptr) = value;
    return success;
}

bool LLXUIParser::writeBoolValue(Parser& parser, const void* val_ptr, name_stack_t& stack)
{
    LLXUIParser& self = static_cast<LLXUIParser&>(parser);
    LLXMLNodePtr node = self.getNode(stack);
    if (node.notNull())
    {
        node->setBoolValue(*((bool*)val_ptr));
        return true;
    }
    return false;
}

bool LLXUIParser::readStringValue(Parser& parser, void* val_ptr)
{
    LLXUIParser& self = static_cast<LLXUIParser&>(parser);
    // Assign straight into the destination: the value being read is nearly
    // always an attribute, where this is one copy rather than a copy into a
    // temporary and a move out of it.
    self.mCurReadNode->getSanitizedValue(*(std::string*)val_ptr);
    return true;
}

bool LLXUIParser::writeStringValue(Parser& parser, const void* val_ptr, name_stack_t& stack)
{
    LLXUIParser& self = static_cast<LLXUIParser&>(parser);
    LLXMLNodePtr node = self.getNode(stack);
    if (node.notNull())
    {
        const std::string* string_val = reinterpret_cast<const std::string*>(val_ptr);
        if (string_val->find('\n') != std::string::npos
            || string_val->size() > MAX_STRING_ATTRIBUTE_SIZE)
        {
            // don't write strings with newlines into attributes
            std::string attribute_name = node->getName()->mString;
            LLXMLNodePtr parent_node = node->mParent;
            parent_node->deleteChild(node);
            // write results in text contents of node
            if (attribute_name == "value")
            {
                // "value" is implicit, just write to parent
                node = parent_node;
            }
            else
            {
                // create a child that is not an attribute, but with same name
                node = parent_node->createChild(attribute_name.c_str(), false);
            }
        }
        node->setStringValue(*string_val);
        return true;
    }
    return false;
}

bool LLXUIParser::readU8Value(Parser& parser, void* val_ptr)
{
    LLXUIParser& self = static_cast<LLXUIParser&>(parser);
    return self.mCurReadNode->getByteValue(1, (U8*)val_ptr);
}

bool LLXUIParser::writeU8Value(Parser& parser, const void* val_ptr, name_stack_t& stack)
{
    LLXUIParser& self = static_cast<LLXUIParser&>(parser);
    LLXMLNodePtr node = self.getNode(stack);
    if (node.notNull())
    {
        node->setUnsignedValue(*((U8*)val_ptr));
        return true;
    }
    return false;
}

bool LLXUIParser::readS8Value(Parser& parser, void* val_ptr)
{
    LLXUIParser& self = static_cast<LLXUIParser&>(parser);
    S32 value;
    if(self.mCurReadNode->getIntValue(1, &value))
    {
        *((S8*)val_ptr) = value;
        return true;
    }
    return false;
}

bool LLXUIParser::writeS8Value(Parser& parser, const void* val_ptr, name_stack_t& stack)
{
    LLXUIParser& self = static_cast<LLXUIParser&>(parser);
    LLXMLNodePtr node = self.getNode(stack);
    if (node.notNull())
    {
        node->setIntValue(*((S8*)val_ptr));
        return true;
    }
    return false;
}

bool LLXUIParser::readU16Value(Parser& parser, void* val_ptr)
{
    LLXUIParser& self = static_cast<LLXUIParser&>(parser);
    U32 value;
    if(self.mCurReadNode->getUnsignedValue(1, &value))
    {
        *((U16*)val_ptr) = value;
        return true;
    }
    return false;
}

bool LLXUIParser::writeU16Value(Parser& parser, const void* val_ptr, name_stack_t& stack)
{
    LLXUIParser& self = static_cast<LLXUIParser&>(parser);
    LLXMLNodePtr node = self.getNode(stack);
    if (node.notNull())
    {
        node->setUnsignedValue(*((U16*)val_ptr));
        return true;
    }
    return false;
}

bool LLXUIParser::readS16Value(Parser& parser, void* val_ptr)
{
    LLXUIParser& self = static_cast<LLXUIParser&>(parser);
    S32 value;
    if(self.mCurReadNode->getIntValue(1, &value))
    {
        *((S16*)val_ptr) = value;
        return true;
    }
    return false;
}

bool LLXUIParser::writeS16Value(Parser& parser, const void* val_ptr, name_stack_t& stack)
{
    LLXUIParser& self = static_cast<LLXUIParser&>(parser);
    LLXMLNodePtr node = self.getNode(stack);
    if (node.notNull())
    {
        node->setIntValue(*((S16*)val_ptr));
        return true;
    }
    return false;
}

bool LLXUIParser::readU32Value(Parser& parser, void* val_ptr)
{
    LLXUIParser& self = static_cast<LLXUIParser&>(parser);
    return self.mCurReadNode->getUnsignedValue(1, (U32*)val_ptr);
}

bool LLXUIParser::writeU32Value(Parser& parser, const void* val_ptr, name_stack_t& stack)
{
    LLXUIParser& self = static_cast<LLXUIParser&>(parser);
    LLXMLNodePtr node = self.getNode(stack);
    if (node.notNull())
    {
        node->setUnsignedValue(*((U32*)val_ptr));
        return true;
    }
    return false;
}

bool LLXUIParser::readS32Value(Parser& parser, void* val_ptr)
{
    LLXUIParser& self = static_cast<LLXUIParser&>(parser);
    return self.mCurReadNode->getIntValue(1, (S32*)val_ptr);
}

bool LLXUIParser::writeS32Value(Parser& parser, const void* val_ptr, name_stack_t& stack)
{
    LLXUIParser& self = static_cast<LLXUIParser&>(parser);
    LLXMLNodePtr node = self.getNode(stack);
    if (node.notNull())
    {
        node->setIntValue(*((S32*)val_ptr));
        return true;
    }
    return false;
}

bool LLXUIParser::readF32Value(Parser& parser, void* val_ptr)
{
    LLXUIParser& self = static_cast<LLXUIParser&>(parser);
    return self.mCurReadNode->getFloatValue(1, (F32*)val_ptr);
}

bool LLXUIParser::writeF32Value(Parser& parser, const void* val_ptr, name_stack_t& stack)
{
    LLXUIParser& self = static_cast<LLXUIParser&>(parser);
    LLXMLNodePtr node = self.getNode(stack);
    if (node.notNull())
    {
        node->setFloatValue(*((F32*)val_ptr));
        return true;
    }
    return false;
}

bool LLXUIParser::readF64Value(Parser& parser, void* val_ptr)
{
    LLXUIParser& self = static_cast<LLXUIParser&>(parser);
    return self.mCurReadNode->getDoubleValue(1, (F64*)val_ptr);
}

bool LLXUIParser::writeF64Value(Parser& parser, const void* val_ptr, name_stack_t& stack)
{
    LLXUIParser& self = static_cast<LLXUIParser&>(parser);
    LLXMLNodePtr node = self.getNode(stack);
    if (node.notNull())
    {
        node->setDoubleValue(*((F64*)val_ptr));
        return true;
    }
    return false;
}

bool LLXUIParser::readVector3Value(Parser& parser, void* val_ptr)
{
    LLXUIParser& self = static_cast<LLXUIParser&>(parser);
    LLVector3* vecp = (LLVector3*)val_ptr;
    if(self.mCurReadNode->getFloatValue(3, vecp->mV) >= 3)
    {
        return true;
    }

    return false;
}

bool LLXUIParser::writeVector3Value(Parser& parser, const void* val_ptr, name_stack_t& stack)
{
    LLXUIParser& self = static_cast<LLXUIParser&>(parser);
    LLXMLNodePtr node = self.getNode(stack);
    if (node.notNull())
    {
        LLVector3 vector = *((LLVector3*)val_ptr);
        node->setFloatValue(3, vector.mV);
        return true;
    }
    return false;
}

bool LLXUIParser::readColor4Value(Parser& parser, void* val_ptr)
{
    LLXUIParser& self = static_cast<LLXUIParser&>(parser);
    LLColor4* colorp = (LLColor4*)val_ptr;
    if(self.mCurReadNode->getFloatValue(4, colorp->mV) >= 3)
    {
        return true;
    }

    return false;
}

bool LLXUIParser::writeColor4Value(Parser& parser, const void* val_ptr, name_stack_t& stack)
{
    LLXUIParser& self = static_cast<LLXUIParser&>(parser);
    LLXMLNodePtr node = self.getNode(stack);
    if (node.notNull())
    {
        LLColor4 color = *((LLColor4*)val_ptr);
        node->setFloatValue(4, color.mV);
        return true;
    }
    return false;
}

bool LLXUIParser::readUIColorValue(Parser& parser, void* val_ptr)
{
    LLXUIParser& self = static_cast<LLXUIParser&>(parser);
    LLUIColor* param = (LLUIColor*)val_ptr;
    LLColor4 color;
    bool success =  self.mCurReadNode->getFloatValue(4, color.mV) >= 3;
    if (success)
    {
        param->set(color);
        return true;
    }
    return false;
}

bool LLXUIParser::writeUIColorValue(Parser& parser, const void* val_ptr, name_stack_t& stack)
{
    LLXUIParser& self = static_cast<LLXUIParser&>(parser);
    LLXMLNodePtr node = self.getNode(stack);
    if (node.notNull())
    {
        LLUIColor color = *((LLUIColor*)val_ptr);
        //RN: don't write out the color that is represented by a function
        // rely on param block exporting to get the reference to the color settings
        if (color.isReference()) return false;
        node->setFloatValue(4, color.get().mV);
        return true;
    }
    return false;
}

bool LLXUIParser::readUUIDValue(Parser& parser, void* val_ptr)
{
    LLXUIParser& self = static_cast<LLXUIParser&>(parser);
    LLUUID temp_id;
    // LLUUID::set is destructive, so use temporary value
    if (temp_id.set(self.mCurReadNode->getSanitizedValue()))
    {
        *(LLUUID*)(val_ptr) = temp_id;
        return true;
    }
    return false;
}

bool LLXUIParser::writeUUIDValue(Parser& parser, const void* val_ptr, name_stack_t& stack)
{
    LLXUIParser& self = static_cast<LLXUIParser&>(parser);
    LLXMLNodePtr node = self.getNode(stack);
    if (node.notNull())
    {
        node->setStringValue(((LLUUID*)val_ptr)->asString());
        return true;
    }
    return false;
}

bool LLXUIParser::readSDValue(Parser& parser, void* val_ptr)
{
    LLXUIParser& self = static_cast<LLXUIParser&>(parser);
    *((LLSD*)val_ptr) = LLSD(self.mCurReadNode->getSanitizedValue());
    return true;
}

bool LLXUIParser::writeSDValue(Parser& parser, const void* val_ptr, name_stack_t& stack)
{
    LLXUIParser& self = static_cast<LLXUIParser&>(parser);

    LLXMLNodePtr node = self.getNode(stack);
    if (node.notNull())
    {
        std::string string_val = ((LLSD*)val_ptr)->asString();
        if (string_val.find('\n') != std::string::npos || string_val.size() > MAX_STRING_ATTRIBUTE_SIZE)
        {
            // don't write strings with newlines into attributes
            std::string attribute_name = node->getName()->mString;
            LLXMLNodePtr parent_node = node->mParent;
            parent_node->deleteChild(node);
            // write results in text contents of node
            if (attribute_name == "value")
            {
                // "value" is implicit, just write to parent
                node = parent_node;
            }
            else
            {
                node = parent_node->createChild(attribute_name.c_str(), false);
            }
        }

        node->setStringValue(string_val);
        return true;
    }
    return false;
}

/*virtual*/ std::string LLXUIParser::getCurrentElementName()
{
    std::string full_name;
    for (name_stack_t::iterator it = mNameStack.begin();
        it != mNameStack.end();
        ++it)
    {
        full_name.append(it->first); // build up dotted names: "button.param.nestedparam."
        full_name += '.';
    }

    return full_name;
}

void LLXUIParser::parserWarning(const std::string& message)
{
    std::string warning_msg = llformat("%s:\t%s(%d)", message.c_str(), mCurFileName.c_str(), mCurReadNode->getLineNumber());
    Parser::parserWarning(warning_msg);
}

void LLXUIParser::parserError(const std::string& message)
{
    std::string error_msg = llformat("%s:\t%s(%d)", message.c_str(), mCurFileName.c_str(), mCurReadNode->getLineNumber());
    Parser::parserError(error_msg);
}


//
// LLSimpleXUIParser
//

LLSimpleXUIParser::LLSimpleXUIParser(LLSimpleXUIParser::element_start_callback_t element_cb)
:   Parser(sSimpleXUIReadFuncs, sSimpleXUIWriteFuncs),
    mCurReadDepth(0),
    mElementCB(element_cb)
{
    if (sSimpleXUIReadFuncs.empty())
    {
        registerParserFuncs<LLInitParam::Flag>(readFlag);
        registerParserFuncs<bool>(readBoolValue);
        registerParserFuncs<std::string>(readStringValue);
        registerParserFuncs<U8>(readU8Value);
        registerParserFuncs<S8>(readS8Value);
        registerParserFuncs<U16>(readU16Value);
        registerParserFuncs<S16>(readS16Value);
        registerParserFuncs<U32>(readU32Value);
        registerParserFuncs<S32>(readS32Value);
        registerParserFuncs<F32>(readF32Value);
        registerParserFuncs<F64>(readF64Value);
        registerParserFuncs<LLColor4>(readColor4Value);
        registerParserFuncs<LLUIColor>(readUIColorValue);
        registerParserFuncs<LLUUID>(readUUIDValue);
        registerParserFuncs<LLSD>(readSDValue);
    }
}

LLSimpleXUIParser::~LLSimpleXUIParser()
{
}


bool LLSimpleXUIParser::readXUI(const std::string& filename, LLInitParam::BaseBlock& block, bool silent)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

    ALXmlDocument document;
    if (!document.loadFile(filename))
    {
        LL_WARNS("ReadXUI") << "Error while reading file " << filename
                << ": " << document.errorDescription()
                << " on line " << document.errorLine() << LL_ENDL;
        return false;
    }

    // A completed parse leaves all of these empty, but an abandoned one does
    // not, and readXUI is a public entry point that says nothing about having
    // to be the first. Clear them rather than build on whatever is there.
    mNameStack.clear();
    mOutputStack.clear();
    mScope.clear();
    mTokenSizeStack.clear();
    mEmptyLeafNode.clear();
    mTextContents.clear();

    mOutputStack.emplace_back(&block, 0);
    mCurFileName = filename;
    mCurReadDepth = 0;
    setParseSilently(silent);

    mEmptyLeafNode.push_back(false);

    // An explicit stack rather than recursion, since nothing bounds how deeply
    // a document nests. The second member says the element is being left, so
    // that its close is reached after everything inside it, and text is visited
    // in document order alongside the elements it sits between.
    std::vector<std::pair<pugi::xml_node, bool> > pending;
    if (pugi::xml_node root_element = document.document().document_element())
    {
        pending.emplace_back(root_element, false);
    }

    while (!pending.empty())
    {
        const pugi::xml_node node = pending.back().first;
        const bool leaving = pending.back().second;
        pending.pop_back();

        if (leaving)
        {
            endElement();
            continue;
        }

        if (node.type() == pugi::node_pcdata || node.type() == pugi::node_cdata)
        {
            mTextContents.append(node.value());
            continue;
        }

        startElement(node);

        pending.emplace_back(node, true);

        const size_t first_child = pending.size();
        for (pugi::xml_node child : node.children())
        {
            switch (child.type())
            {
            case pugi::node_element:
            case pugi::node_pcdata:
            case pugi::node_cdata:
                pending.emplace_back(child, false);
                break;

            default:
                break;
            }
        }
        std::reverse(pending.begin() + first_child, pending.end());
    }

    mEmptyLeafNode.pop_back();
    return true;
}

void LLSimpleXUIParser::startElement(const pugi::xml_node& element)
{
    processText();

    const char* const name = element.name();

    if (mElementCB)
    {
        LLInitParam::BaseBlock* blockp = mElementCB(*this, name);
        if (blockp)
        {
            mOutputStack.emplace_back(blockp, 0);
        }
    }

    mOutputStack.back().second++;
    S32 num_tokens_pushed = 0;
    bool ignore_element = false;
    std::string_view child_name(name);

    if (mOutputStack.back().second == 1)
    {   // root node for this block
        mScope.emplace_back(child_name);
    }
    else
    {   // compound attribute
        const std::size_t first_dot = child_name.find('.');
        if (first_dot == std::string_view::npos)
        {
            mNameStack.emplace_back(child_name, true);
            num_tokens_pushed++;
            mScope.emplace_back(child_name);
        }
        else if (!mScope.empty() && child_name.substr(0, first_dot) != mScope.back())
        {
            // Addressed to some other block, so take nothing from it. An
            // empty scope keeps anything nested inside it from matching
            // either.
            ignore_element = true;
            mScope.emplace_back();
        }
        else
        {
            // push everything after the leading token
            num_tokens_pushed += pushDottedName(mNameStack, name + first_dot + 1);
            if (num_tokens_pushed == 0)
            {
                ignore_element = true;
                mScope.emplace_back();
            }
            else
            {
                mScope.emplace_back(mNameStack.back().first);
            }
        }
    }

    // parent node is not empty
    mEmptyLeafNode.back() = false;
    // we are empty if we have no attributes
    mEmptyLeafNode.push_back(!element.first_attribute());

    // Every element reaches endElement, which pops one entry from each of
    // these, so an element being ignored still has to leave one behind.
    // Returning early here instead unbalanced all three, and popped whatever
    // an enclosing element had put there -- or nothing at all, at the top.
    mTokenSizeStack.push_back(num_tokens_pushed);
    if (!ignore_element)
    {
        readAttributes(element);
    }
}

void LLSimpleXUIParser::endElement()
{
    bool has_text = processText();

    // no text, attributes, or children
    if (!has_text && mEmptyLeafNode.back())
    {
        // submit this as a valueless name (even though there might be text contents we haven't seen yet)
        mCurAttributeValueBegin = NO_VALUE_MARKER;
        mOutputStack.back().first->submitValue(mNameStack, *this, mParseSilently);
    }

    if (--mOutputStack.back().second == 0)
    {
        if (mOutputStack.empty())
        {
            LL_ERRS("ReadXUI") << "Parameter block output stack popped while empty." << LL_ENDL;
        }
        mOutputStack.pop_back();
    }

    S32 num_tokens_to_pop = mTokenSizeStack.back();
    mTokenSizeStack.pop_back();
    while(num_tokens_to_pop-- > 0)
    {
        mNameStack.pop_back();
    }
    mScope.pop_back();
    mEmptyLeafNode.pop_back();
}

// As LLXUIParser::pushNameTokens, over this parser's own name stack.
S32 LLSimpleXUIParser::pushNameTokens(const char* name)
{
    return pushDottedName(mNameStack, name);
}

bool LLSimpleXUIParser::readAttributes(const pugi::xml_node& element)
{
    bool any_parsed = false;
    for (pugi::xml_attribute attribute : element.attributes())
    {
        mCurAttributeValueBegin = attribute.value();

        S32 num_tokens_pushed = pushNameTokens(attribute.name());

        // child nodes are not necessarily valid attributes, so don't complain once we've recursed
        any_parsed |= mOutputStack.back().first->submitValue(mNameStack, *this, mParseSilently);

        while(num_tokens_pushed-- > 0)
        {
            mNameStack.pop_back();
        }
    }
    return any_parsed;
}

bool LLSimpleXUIParser::processText()
{
    if (!mTextContents.empty())
    {
        LLStringUtil::trim(mTextContents);
        if (!mTextContents.empty())
        {
            mNameStack.emplace_back("value", true);
            mCurAttributeValueBegin = std::move(mTextContents);
            mOutputStack.back().first->submitValue(mNameStack, *this, mParseSilently);
            mNameStack.pop_back();
        }
        mTextContents.clear();
        return true;
    }
    return false;
}

/*virtual*/ std::string LLSimpleXUIParser::getCurrentElementName()
{
    std::string full_name;
    for (name_stack_t::iterator it = mNameStack.begin();
        it != mNameStack.end();
        ++it)
    {
        full_name.append(it->first); // build up dotted names: "button.param.nestedparam."
        full_name += '.';
    }

    return full_name;
}

void LLSimpleXUIParser::parserWarning(const std::string& message)
{
    std::string warning_msg = llformat("%s:\t%s",  message.c_str(), mCurFileName.c_str());
    Parser::parserWarning(warning_msg);
}

void LLSimpleXUIParser::parserError(const std::string& message)
{
    std::string error_msg = llformat("%s:\t%s",  message.c_str(), mCurFileName.c_str());
    Parser::parserError(error_msg);
}

bool LLSimpleXUIParser::readFlag(Parser& parser, void* val_ptr)
{
    LLSimpleXUIParser& self = static_cast<LLSimpleXUIParser&>(parser);
    return self.mCurAttributeValueBegin == NO_VALUE_MARKER;
}

bool LLSimpleXUIParser::readBoolValue(Parser& parser, void* val_ptr)
{
    LLSimpleXUIParser& self = static_cast<LLSimpleXUIParser&>(parser);
    if (!strcmp(self.mCurAttributeValueBegin.c_str(), "true"))
    {
        *((bool*)val_ptr) = true;
        return true;
    }
    else if (!strcmp(self.mCurAttributeValueBegin.c_str(), "false"))
    {
        *((bool*)val_ptr) = false;
        return true;
    }

    return false;
}

// The readers below replace boost::spirit::classic's parse(...).full, which
// succeeded only when the whole attribute was consumed. from_chars gives the
// same answer by comparing its end pointer against the end of the text, and
// range-checks the narrow types, which assign_a into a U8 did not.
//
// One spirit behaviour has to be kept by hand: int_p and real_p accepted a
// leading '+', and from_chars does not.
namespace
{
    const char* skipParseSpace(const char* p, const char* end)
    {
        while (p != end && std::isspace(static_cast<unsigned char>(*p)))
        {
            ++p;
        }
        return p;
    }

    // libc++ marks the floating-point from_chars overloads unavailable before
    // macOS 26, so a lower deployment target falls back to strtof/strtod. The
    // checks below reject what strtod takes and from_chars does not: leading
    // whitespace, a leading '+', and the hex form.
#if defined(_LIBCPP_AVAILABILITY_HAS_FROM_CHARS_FLOATING_POINT) && !_LIBCPP_AVAILABILITY_HAS_FROM_CHARS_FLOATING_POINT

    template <typename T>
    std::from_chars_result fromChars(const char* begin, const char* end, T& out)
        requires std::is_floating_point_v<T>
    {
        if (begin == end || std::isspace(static_cast<unsigned char>(*begin)) || *begin == '+')
        {
            return {begin, std::errc::invalid_argument};
        }

        const char* digits = (*begin == '-') ? begin + 1 : begin;
        if (end - digits > 1 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X'))
        {
            return {begin, std::errc::invalid_argument};
        }

        // Safe: the text comes from std::string::data(), NUL-terminated at
        // `end`, and strtod stops at the first character it cannot use.
        char* stop = nullptr;
        errno = 0;

        // _l forms: from_chars ignores LC_NUMERIC, plain strtod does not.
        // strtof, not narrowing: float overflow sets ERANGE in neither call.
        T value{};
        if constexpr (std::is_same_v<T, float>)
        {
            value = strtof_l(begin, &stop, LC_C_LOCALE);
        }
        else
        {
            value = static_cast<T>(strtod_l(begin, &stop, LC_C_LOCALE));
        }

        if (stop == begin || stop > end)
        {
            return {begin, std::errc::invalid_argument};
        }
        if (errno == ERANGE)
        {
            return {stop, std::errc::result_out_of_range};
        }

        out = value;
        return {stop, std::errc{}};
    }

    template <typename T>
    std::from_chars_result fromChars(const char* begin, const char* end, T& out)
        requires (!std::is_floating_point_v<T>)
    {
        return std::from_chars(begin, end, out);
    }

#else

    template <typename T>
    std::from_chars_result fromChars(const char* begin, const char* end, T& out)
    {
        return std::from_chars(begin, end, out);
    }

#endif

    template <typename T>
    bool parseWholeValue(const std::string& text, T& out)
    {
        const char* begin = text.data();
        const char* const end = begin + text.size();
        if (begin != end && *begin == '+')
        {
            ++begin;
        }
        const std::from_chars_result result = fromChars(begin, end, out);
        return result.ec == std::errc{} && result.ptr == end;
    }

    // Four reals separated by whitespace, as real_p >> real_p >> real_p >>
    // real_p parsed with space_p as the skipper.
    bool parseColor4Value(const std::string& text, LLColor4& value)
    {
        const char* p = text.data();
        const char* const end = p + text.size();
        for (S32 i = 0; i < 4; ++i)
        {
            p = skipParseSpace(p, end);
            if (p != end && *p == '+')
            {
                ++p;
            }
            const std::from_chars_result result = fromChars(p, end, value.mV[i]);
            if (result.ec != std::errc{})
            {
                return false;
            }
            p = result.ptr;
        }
        return skipParseSpace(p, end) == end;
    }
}

bool LLSimpleXUIParser::readStringValue(Parser& parser, void* val_ptr)
{
    LLSimpleXUIParser& self = static_cast<LLSimpleXUIParser&>(parser);
    *((std::string*)val_ptr) = std::move(self.mCurAttributeValueBegin);
    return true;
}

bool LLSimpleXUIParser::readU8Value(Parser& parser, void* val_ptr)
{
    LLSimpleXUIParser& self = static_cast<LLSimpleXUIParser&>(parser);
    return parseWholeValue(self.mCurAttributeValueBegin, *(U8*)val_ptr);
}

bool LLSimpleXUIParser::readS8Value(Parser& parser, void* val_ptr)
{
    LLSimpleXUIParser& self = static_cast<LLSimpleXUIParser&>(parser);
    return parseWholeValue(self.mCurAttributeValueBegin, *(S8*)val_ptr);
}

bool LLSimpleXUIParser::readU16Value(Parser& parser, void* val_ptr)
{
    LLSimpleXUIParser& self = static_cast<LLSimpleXUIParser&>(parser);
    return parseWholeValue(self.mCurAttributeValueBegin, *(U16*)val_ptr);
}

bool LLSimpleXUIParser::readS16Value(Parser& parser, void* val_ptr)
{
    LLSimpleXUIParser& self = static_cast<LLSimpleXUIParser&>(parser);
    return parseWholeValue(self.mCurAttributeValueBegin, *(S16*)val_ptr);
}

bool LLSimpleXUIParser::readU32Value(Parser& parser, void* val_ptr)
{
    LLSimpleXUIParser& self = static_cast<LLSimpleXUIParser&>(parser);
    return parseWholeValue(self.mCurAttributeValueBegin, *(U32*)val_ptr);
}

bool LLSimpleXUIParser::readS32Value(Parser& parser, void* val_ptr)
{
    LLSimpleXUIParser& self = static_cast<LLSimpleXUIParser&>(parser);
    return parseWholeValue(self.mCurAttributeValueBegin, *(S32*)val_ptr);
}

bool LLSimpleXUIParser::readF32Value(Parser& parser, void* val_ptr)
{
    LLSimpleXUIParser& self = static_cast<LLSimpleXUIParser&>(parser);
    return parseWholeValue(self.mCurAttributeValueBegin, *(F32*)val_ptr);
}

bool LLSimpleXUIParser::readF64Value(Parser& parser, void* val_ptr)
{
    LLSimpleXUIParser& self = static_cast<LLSimpleXUIParser&>(parser);
    return parseWholeValue(self.mCurAttributeValueBegin, *(F64*)val_ptr);
}

bool LLSimpleXUIParser::readColor4Value(Parser& parser, void* val_ptr)
{
    LLSimpleXUIParser& self = static_cast<LLSimpleXUIParser&>(parser);
    LLColor4 value;

    if (parseColor4Value(self.mCurAttributeValueBegin, value))
    {
        *(LLColor4*)(val_ptr) = value;
        return true;
    }
    return false;
}

bool LLSimpleXUIParser::readUIColorValue(Parser& parser, void* val_ptr)
{
    LLSimpleXUIParser& self = static_cast<LLSimpleXUIParser&>(parser);
    LLColor4 value;
    LLUIColor* colorp = (LLUIColor*)val_ptr;

    if (parseColor4Value(self.mCurAttributeValueBegin, value))
    {
        colorp->set(value);
        return true;
    }
    return false;
}

bool LLSimpleXUIParser::readUUIDValue(Parser& parser, void* val_ptr)
{
    LLSimpleXUIParser& self = static_cast<LLSimpleXUIParser&>(parser);
    LLUUID temp_id;
    // LLUUID::set is destructive, so use temporary value
    if (temp_id.set(self.mCurAttributeValueBegin))
    {
        *(LLUUID*)(val_ptr) = temp_id;
        return true;
    }
    return false;
}

bool LLSimpleXUIParser::readSDValue(Parser& parser, void* val_ptr)
{
    LLSimpleXUIParser& self = static_cast<LLSimpleXUIParser&>(parser);
    *((LLSD*)val_ptr) = LLSD(self.mCurAttributeValueBegin);
    return true;
}
