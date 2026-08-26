/**
 * @file llxmltree.cpp
 * @brief LLXmlTree implementation
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
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

#include "llxmltree.h"

#include "alxmldocument.h"
#include "v3color.h"
#include "v4color.h"
#include "v4coloru.h"
#include "v3math.h"
#include "v3dmath.h"
#include "v4math.h"
#include "llquaternion.h"
#include "lluuid.h"

#include <algorithm>
#include <cstring>
#include <vector>

//////////////////////////////////////////////////////////////
// LLXmlTree

// static
LLStdStringTable LLXmlTree::sAttributeKeys(1024);

LLXmlTree::LLXmlTree()
    : mRoot( nullptr ),
      mNodeNames(512)
{
}

LLXmlTree::~LLXmlTree()
{
    cleanup();
}

LLXmlTree::LLXmlTree(LLXmlTree&& other) noexcept
    : mRoot(other.mRoot),
      mNodeNames(512)
{
    other.mRoot = nullptr;
}

LLXmlTree& LLXmlTree::operator=(LLXmlTree&& other) noexcept
{
    if (this != &other)
    {
        cleanup();
        mRoot = other.mRoot;
        other.mRoot = nullptr;
    }
    return *this;
}

void LLXmlTree::cleanup()
{
    delete mRoot;
    mRoot = nullptr;
    mNodeNames.cleanup();
}


bool LLXmlTree::parseFile(const std::string &path, bool keep_contents)
{
    delete mRoot;
    mRoot = nullptr;

    LLXmlTreeParser parser(this);
    bool success = parser.parseFile( path, &mRoot, keep_contents );
    if( !success )
    {
        LL_WARNS() << "LLXmlTree parse failed.  Line " << parser.getCurrentLineNumber()
                << ": " << parser.getErrorString() << LL_ENDL;
    }
    return success;
}

void LLXmlTree::dump()
{
    if( mRoot )
    {
        dumpNode( mRoot, "    " );
    }
}

void LLXmlTree::dumpNode( LLXmlTreeNode* node, const std::string& prefix )
{
    node->dump( prefix );

    std::string new_prefix = prefix + "    ";
    for( LLXmlTreeNode* child = node->getFirstChild(); child; child = node->getNextChild() )
    {
        dumpNode( child, new_prefix );
    }
}

//////////////////////////////////////////////////////////////
// LLXmlTreeNode

LLXmlTreeNode::LLXmlTreeNode( std::string name, LLXmlTreeNode* parent, LLXmlTree* tree )
    : mName(std::move(name)),
      mParent(parent),
      mTree(tree)
{
}

LLXmlTreeNode::~LLXmlTreeNode()
{
    for (auto& attr : mAttributes)
    {
        delete attr.second;
    }
    mAttributes.clear();

    for (auto& child : mChildren)
    {
        delete child;
    }
    mChildren.clear();
}

void LLXmlTreeNode::dump( const std::string& prefix )
{
    LL_INFOS() << prefix << mName ;
    if( !mContents.empty() )
    {
        LL_CONT << " contents = \"" << mContents << "\"";
    }
    for (const auto& [key, value] : mAttributes)
    {
        LL_CONT << prefix << " " << key << "=" << (value->empty() ? "NULL" : *value);
    }
    LL_CONT << LL_ENDL;
}

bool LLXmlTreeNode::hasAttribute(const std::string& name)
{
    LLStdStringHandle canonical_name = LLXmlTree::sAttributeKeys.addString(name);
    attribute_map_t::iterator iter = mAttributes.find(canonical_name);
    return iter != mAttributes.end();
}

void LLXmlTreeNode::addAttribute(const std::string& name, std::string value)
{
    LLStdStringHandle canonical_name = LLXmlTree::sAttributeKeys.addString(name);
    const std::string*& slot = mAttributes[canonical_name];
    // The destructor owns whatever is here, so a repeated attribute name has to
    // free the value it replaces rather than drop the pointer on the floor.
    delete slot;
    slot = new std::string(std::move(value));
}

LLXmlTreeNode*  LLXmlTreeNode::getFirstChild()
{
    mChildrenIter = mChildren.begin();
    return getNextChild();
}
LLXmlTreeNode*  LLXmlTreeNode::getNextChild()
{
    if (mChildrenIter == mChildren.end())
        return 0;
    else
        return *mChildrenIter++;
}

LLXmlTreeNode* LLXmlTreeNode::getChildByName(const std::string& name)
{
    LLStdStringHandle tableptr = mTree->mNodeNames.checkString(name);
    mChildMapIter = mChildMap.lower_bound(tableptr);
    mChildMapEndIter = mChildMap.upper_bound(tableptr);
    return getNextNamedChild();
}

LLXmlTreeNode* LLXmlTreeNode::getNextNamedChild()
{
    if (mChildMapIter == mChildMapEndIter)
        return nullptr;
    else
        return (mChildMapIter++)->second;
}

void LLXmlTreeNode::appendContents(const char* str, std::string::size_type len)
{
    mContents.append( str, len );
}

void LLXmlTreeNode::addChild(LLXmlTreeNode* child)
{
    llassert( child );
    mChildren.push_back( child );

    // Add a name mapping to this node
    LLStdStringHandle tableptr = mTree->mNodeNames.insert(child->mName);
    mChildMap.insert( child_map_t::value_type(tableptr, child));

    child->mParent = this;
}

//////////////////////////////////////////////////////////////

// These functions assume that name is already in mAttritrubteKeys

bool LLXmlTreeNode::getFastAttributeBOOL(LLStdStringHandle canonical_name, bool& value)
{
    const std::string *s = getAttribute( canonical_name );
    return s && LLStringUtil::convertToBOOL( *s, value );
}

bool LLXmlTreeNode::getFastAttributeU8(LLStdStringHandle canonical_name, U8& value)
{
    const std::string *s = getAttribute( canonical_name );
    return s && LLStringUtil::convertToU8( *s, value );
}

bool LLXmlTreeNode::getFastAttributeS8(LLStdStringHandle canonical_name, S8& value)
{
    const std::string *s = getAttribute( canonical_name );
    return s && LLStringUtil::convertToS8( *s, value );
}

bool LLXmlTreeNode::getFastAttributeS16(LLStdStringHandle canonical_name, S16& value)
{
    const std::string *s = getAttribute( canonical_name );
    return s && LLStringUtil::convertToS16( *s, value );
}

bool LLXmlTreeNode::getFastAttributeU16(LLStdStringHandle canonical_name, U16& value)
{
    const std::string *s = getAttribute( canonical_name );
    return s && LLStringUtil::convertToU16( *s, value );
}

bool LLXmlTreeNode::getFastAttributeU32(LLStdStringHandle canonical_name, U32& value)
{
    const std::string *s = getAttribute( canonical_name );
    return s && LLStringUtil::convertToU32( *s, value );
}

bool LLXmlTreeNode::getFastAttributeS32(LLStdStringHandle canonical_name, S32& value)
{
    const std::string *s = getAttribute( canonical_name );
    return s && LLStringUtil::convertToS32( *s, value );
}

bool LLXmlTreeNode::getFastAttributeF32(LLStdStringHandle canonical_name, F32& value)
{
    const std::string *s = getAttribute( canonical_name );
    return s && LLStringUtil::convertToF32( *s, value );
}

bool LLXmlTreeNode::getFastAttributeF64(LLStdStringHandle canonical_name, F64& value)
{
    const std::string *s = getAttribute( canonical_name );
    return s && LLStringUtil::convertToF64( *s, value );
}

bool LLXmlTreeNode::getFastAttributeColor(LLStdStringHandle canonical_name, LLColor4& value)
{
    const std::string *s = getAttribute( canonical_name );
    return s ? LLColor4::parseColor(*s, &value) : false;
}

bool LLXmlTreeNode::getFastAttributeColor4(LLStdStringHandle canonical_name, LLColor4& value)
{
    const std::string *s = getAttribute( canonical_name );
    return s ? LLColor4::parseColor4(*s, &value) : false;
}

bool LLXmlTreeNode::getFastAttributeColor4U(LLStdStringHandle canonical_name, LLColor4U& value)
{
    const std::string *s = getAttribute( canonical_name );
    return s ? LLColor4U::parseColor4U(*s, &value ) : false;
}

bool LLXmlTreeNode::getFastAttributeVector3(LLStdStringHandle canonical_name, LLVector3& value)
{
    const std::string *s = getAttribute( canonical_name );
    return s ? LLVector3::parseVector3(*s, &value ) : false;
}

bool LLXmlTreeNode::getFastAttributeVector3d(LLStdStringHandle canonical_name, LLVector3d& value)
{
    const std::string *s = getAttribute( canonical_name );
    return s ? LLVector3d::parseVector3d(*s,  &value ) : false;
}

bool LLXmlTreeNode::getFastAttributeQuat(LLStdStringHandle canonical_name, LLQuaternion& value)
{
    const std::string *s = getAttribute( canonical_name );
    return s ? LLQuaternion::parseQuat(*s, &value ) : false;
}

bool LLXmlTreeNode::getFastAttributeUUID(LLStdStringHandle canonical_name, LLUUID& value)
{
    const std::string *s = getAttribute( canonical_name );
    return s ? LLUUID::parseUUID(*s, &value ) : false;
}

bool LLXmlTreeNode::getFastAttributeString(LLStdStringHandle canonical_name, std::string& value)
{
    const std::string *s = getAttribute( canonical_name );
    if( !s )
    {
        return false;
    }

    value = *s;
    return true;
}


//////////////////////////////////////////////////////////////

bool LLXmlTreeNode::getAttributeBOOL(const std::string& name, bool& value)
{
    LLStdStringHandle canonical_name = LLXmlTree::sAttributeKeys.addString( name );
    return getFastAttributeBOOL(canonical_name, value);
}

bool LLXmlTreeNode::getAttributeU8(const std::string& name, U8& value)
{
    LLStdStringHandle canonical_name = LLXmlTree::sAttributeKeys.addString( name );
    return getFastAttributeU8(canonical_name, value);
}

bool LLXmlTreeNode::getAttributeS8(const std::string& name, S8& value)
{
    LLStdStringHandle canonical_name = LLXmlTree::sAttributeKeys.addString( name );
    return getFastAttributeS8(canonical_name, value);
}

bool LLXmlTreeNode::getAttributeS16(const std::string& name, S16& value)
{
    LLStdStringHandle canonical_name = LLXmlTree::sAttributeKeys.addString( name );
    return getFastAttributeS16(canonical_name, value);
}

bool LLXmlTreeNode::getAttributeU16(const std::string& name, U16& value)
{
    LLStdStringHandle canonical_name = LLXmlTree::sAttributeKeys.addString( name );
    return getFastAttributeU16(canonical_name, value);
}

bool LLXmlTreeNode::getAttributeU32(const std::string& name, U32& value)
{
    LLStdStringHandle canonical_name = LLXmlTree::sAttributeKeys.addString( name );
    return getFastAttributeU32(canonical_name, value);
}

bool LLXmlTreeNode::getAttributeS32(const std::string& name, S32& value)
{
    LLStdStringHandle canonical_name = LLXmlTree::sAttributeKeys.addString( name );
    return getFastAttributeS32(canonical_name, value);
}

bool LLXmlTreeNode::getAttributeF32(const std::string& name, F32& value)
{
    LLStdStringHandle canonical_name = LLXmlTree::sAttributeKeys.addString( name );
    return getFastAttributeF32(canonical_name, value);
}

bool LLXmlTreeNode::getAttributeF64(const std::string& name, F64& value)
{
    LLStdStringHandle canonical_name = LLXmlTree::sAttributeKeys.addString( name );
    return getFastAttributeF64(canonical_name, value);
}

bool LLXmlTreeNode::getAttributeColor(const std::string& name, LLColor4& value)
{
    LLStdStringHandle canonical_name = LLXmlTree::sAttributeKeys.addString( name );
    return getFastAttributeColor(canonical_name, value);
}

bool LLXmlTreeNode::getAttributeColor4(const std::string& name, LLColor4& value)
{
    LLStdStringHandle canonical_name = LLXmlTree::sAttributeKeys.addString( name );
    return getFastAttributeColor4(canonical_name, value);
}

bool LLXmlTreeNode::getAttributeColor4U(const std::string& name, LLColor4U& value)
{
    LLStdStringHandle canonical_name = LLXmlTree::sAttributeKeys.addString( name );
    return getFastAttributeColor4U(canonical_name, value);
}

bool LLXmlTreeNode::getAttributeVector3(const std::string& name, LLVector3& value)
{
    LLStdStringHandle canonical_name = LLXmlTree::sAttributeKeys.addString( name );
    return getFastAttributeVector3(canonical_name, value);
}

bool LLXmlTreeNode::getAttributeVector3d(const std::string& name, LLVector3d& value)
{
    LLStdStringHandle canonical_name = LLXmlTree::sAttributeKeys.addString( name );
    return getFastAttributeVector3d(canonical_name, value);
}

bool LLXmlTreeNode::getAttributeQuat(const std::string& name, LLQuaternion& value)
{
    LLStdStringHandle canonical_name = LLXmlTree::sAttributeKeys.addString( name );
    return getFastAttributeQuat(canonical_name, value);
}

bool LLXmlTreeNode::getAttributeUUID(const std::string& name, LLUUID& value)
{
    LLStdStringHandle canonical_name = LLXmlTree::sAttributeKeys.addString( name );
    return getFastAttributeUUID(canonical_name, value);
}

bool LLXmlTreeNode::getAttributeString(const std::string& name, std::string& value)
{
    LLStdStringHandle canonical_name = LLXmlTree::sAttributeKeys.addString( name );
    return getFastAttributeString(canonical_name, value);
}

/*
  The following xml <message> nodes will all return the string from getTextContents():
  "The quick brown fox\n  Jumps over the lazy dog"

  1. HTML paragraph format:
        <message>
        <p>The quick brown fox</p>
        <p>  Jumps over the lazy dog</p>
        </message>
  2. Each quoted section -> paragraph:
        <message>
        "The quick brown fox"
        "  Jumps over the lazy dog"
        </message>
  3. Literal text with beginning and trailing whitespace removed:
        <message>
The quick brown fox
  Jumps over the lazy dog
        </message>

*/

std::string LLXmlTreeNode::getTextContents()
{
    std::string msg;
    LLXmlTreeNode* p = getChildByName("p");
    if (p)
    {
        // Case 1: node has <p>text</p> tags
        while (p)
        {
            msg += p->getContents() + "\n";
            p = getNextNamedChild();
        }
    }
    else
    {
        std::string::size_type n = mContents.find_first_not_of(" \t\n");
        if (n != std::string::npos && mContents[n] == '\"')
        {
            // Case 2: node has quoted text
            S32 num_lines = 0;
            while(1)
            {
                // mContents[n] == '"'
                ++n;
                std::string::size_type t = n;
                std::string::size_type m = 0;
                // fix-up escaped characters
                while(1)
                {
                    m = mContents.find_first_of("\\\"", t); // find first \ or "
                    if ((m == std::string::npos) || (mContents[m] == '\"'))
                    {
                        break;
                    }
                    mContents.erase(m,1);
                    t = m+1;
                }
                if (m == std::string::npos)
                {
                    break;
                }
                // mContents[m] == '"'
                num_lines++;
                msg += mContents.substr(n,m-n) + "\n";
                n = mContents.find_first_of("\"", m+1);
                if (n == std::string::npos)
                {
                    if (num_lines == 1)
                    {
                        msg.erase(msg.size()-1); // remove "\n" if only one line
                    }
                    break;
                }
            }
        }
        else
        {
            // Case 3: node has embedded text (beginning and trailing whitespace trimmed)
            msg = mContents;
        }
    }
    return msg;
}


//////////////////////////////////////////////////////////////
// LLXmlTreeParser

LLXmlTreeParser::LLXmlTreeParser(LLXmlTree* tree)
    : mTree(tree),
      mRoot( nullptr ),
      mKeepContents(false),
      mErrorLine(0)
{
}

LLXmlTreeParser::~LLXmlTreeParser()
{
}

bool LLXmlTreeParser::parseFile(const std::string &path, LLXmlTreeNode** root, bool keep_contents)
{
    llassert( !mRoot );

    mKeepContents = keep_contents;
    mErrorLine = 0;
    mErrorString.clear();

    ALXmlDocument document;
    if (!document.loadFile(path))
    {
        mErrorLine = document.errorLine();
        mErrorString = document.errorDescription();
        *root = nullptr;
        return false;
    }

    buildTree(document.document().document_element());

    *root = mRoot;
    mRoot = nullptr;
    return true;
}

void LLXmlTreeParser::buildTree(const pugi::xml_node& root_element)
{
    // An explicit stack rather than recursion, since nothing bounds how deeply
    // a document nests and some of them arrive over the network.
    std::vector<std::pair<pugi::xml_node, LLXmlTreeNode*>> pending;
    pending.emplace_back(root_element, nullptr);

    while (!pending.empty())
    {
        const pugi::xml_node element = pending.back().first;
        LLXmlTreeNode* const parent = pending.back().second;
        pending.pop_back();

        LLXmlTreeNode* node = CreateXmlTreeNode( element.name(), parent );

        for (pugi::xml_attribute attribute : element.attributes())
        {
            node->addAttribute( attribute.name(), attribute.value() );
        }

        if( parent )
        {
            parent->addChild( node );
        }
        else
        {
            mRoot = node;
        }

        const size_t first_child = pending.size();
        for (pugi::xml_node child : element.children())
        {
            switch (child.type())
            {
            case pugi::node_element:
                pending.emplace_back(child, node);
                break;

            case pugi::node_pcdata:
            case pugi::node_cdata:
                if (mKeepContents)
                {
                    const char* contents = child.value();
                    node->appendContents( contents, strlen(contents) );
                }
                break;

            default:
                break;
            }
        }
        // Reversed, so that popping reaches the children in document order.
        std::reverse(pending.begin() + first_child, pending.end());

        if( !node->mContents.empty() )
        {
            LLStringUtil::trim(node->mContents);
            LLStringUtil::removeCRLF(node->mContents);
        }
    }
}

LLXmlTreeNode* LLXmlTreeParser::CreateXmlTreeNode(std::string name, LLXmlTreeNode* parent)
{
    return new LLXmlTreeNode(std::move(name), parent, mTree);
}

void test_llxmltree()
{
    LLXmlTree tree;
    bool success = tree.parseFile( "test.xml" );
    if( success )
    {
        tree.dump();
    }
}

