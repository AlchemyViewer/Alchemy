/**
 * @file llxmltree.h
 * @author Aaron Yonas, Richard Nelson
 * @brief LLXmlTree class definition
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
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

#ifndef LL_LLXMLTREE_H
#define LL_LLXMLTREE_H

#include <map>
#include <list>
#include "llstring.h"
#include "llstringtable.h"

namespace pugi { class xml_node; }

class LLColor4;
class LLColor4U;
class LLQuaternion;
class LLUUID;
class LLVector3;
class LLVector3d;
class LLXmlTreeNode;
class LLXmlTreeParser;

//////////////////////////////////////////////////////////////
// LLXmlTree

class LLXmlTree
{
    friend class LLXmlTreeNode;

public:
    LLXmlTree();
    virtual ~LLXmlTree();

    // Owns mRoot and deletes it, so a copy would free the tree twice. Moving
    // hands the root over and leaves the source empty.
    LLXmlTree(const LLXmlTree&) = delete;
    LLXmlTree& operator=(const LLXmlTree&) = delete;
    LLXmlTree(LLXmlTree&& other) noexcept;
    LLXmlTree& operator=(LLXmlTree&& other) noexcept;
    void cleanup();

    virtual bool    parseFile(const std::string &path, bool keep_contents = true);

    LLXmlTreeNode*  getRoot() { return mRoot; }

    void            dump();
    void            dumpNode( LLXmlTreeNode* node, const std::string& prefix );

    // A view, so the fifty-odd call sites handing this a literal stop
    // building a std::string to be thrown away.
    static LLStdStringHandle addAttributeString(std::string_view name)
    {
        return sAttributeKeys.addString(name);
    }

public:
    // global
    static LLStdStringTable sAttributeKeys;

protected:
    LLXmlTreeNode* mRoot;

    // local
    LLStdStringTable mNodeNames;
};

//////////////////////////////////////////////////////////////
// LLXmlTreeNode

class LLXmlTreeNode
{
    friend class LLXmlTree;
    friend class LLXmlTreeParser;

protected:
    // Protected since nodes are only created and destroyed by friend classes and other LLXmlTreeNodes
    LLXmlTreeNode( std::string name, LLXmlTreeNode* parent, LLXmlTree* tree );

public:
    // Not virtual, and so this class has no vtable: nothing derives from it.
    // The one thing that could hand back something that did is
    // LLXmlTreeParser::CreateXmlTreeNode, which nothing overrides. Giving
    // that hook a real subclass means making this virtual again -- a node is
    // deleted through an LLXmlTreeNode* by its parent and by LLXmlTree.
    ~LLXmlTreeNode();

    // Owns its children and its attribute values as raw pointers and deletes
    // every one of them, so a copy would free them twice.
    LLXmlTreeNode(const LLXmlTreeNode&) = delete;
    LLXmlTreeNode& operator=(const LLXmlTreeNode&) = delete;

    const std::string&  getName()
    {
        return mName;
    }
    bool hasName( std::string_view name )
    {
        return mName == name;
    }

    bool hasAttribute( std::string_view name );

    // Fast versions use cannonical_name handlee to entru in LLXmlTree::sAttributeKeys string table
    bool            getFastAttributeBOOL(       LLStdStringHandle cannonical_name, bool& value );
    bool            getFastAttributeU8(         LLStdStringHandle cannonical_name, U8& value );
    bool            getFastAttributeS8(         LLStdStringHandle cannonical_name, S8& value );
    bool            getFastAttributeU16(        LLStdStringHandle cannonical_name, U16& value );
    bool            getFastAttributeS16(        LLStdStringHandle cannonical_name, S16& value );
    bool            getFastAttributeU32(        LLStdStringHandle cannonical_name, U32& value );
    bool            getFastAttributeS32(        LLStdStringHandle cannonical_name, S32& value );
    bool            getFastAttributeF32(        LLStdStringHandle cannonical_name, F32& value );
    bool            getFastAttributeF64(        LLStdStringHandle cannonical_name, F64& value );
    bool            getFastAttributeColor(      LLStdStringHandle cannonical_name, LLColor4& value );
    bool            getFastAttributeColor4(     LLStdStringHandle cannonical_name, LLColor4& value );
    bool            getFastAttributeColor4U(    LLStdStringHandle cannonical_name, LLColor4U& value );
    bool            getFastAttributeVector3(    LLStdStringHandle cannonical_name, LLVector3& value );
    bool            getFastAttributeVector3d(   LLStdStringHandle cannonical_name, LLVector3d& value );
    bool            getFastAttributeQuat(       LLStdStringHandle cannonical_name, LLQuaternion& value );
    bool            getFastAttributeUUID(       LLStdStringHandle cannonical_name, LLUUID& value );
    bool            getFastAttributeString(     LLStdStringHandle cannonical_name, std::string& value );

    // Normal versions find 'name' in LLXmlTree::sAttributeKeys then call
    // the fast versions. Not virtual: nothing derives from this class, and
    // the tree hands out LLXmlTreeNode* rather than anything to dispatch on.
    bool                getAttributeBOOL(       std::string_view name, bool& value );
    bool                getAttributeU8(         std::string_view name, U8& value );
    bool                getAttributeS8(         std::string_view name, S8& value );
    bool                getAttributeU16(        std::string_view name, U16& value );
    bool                getAttributeS16(        std::string_view name, S16& value );
    bool                getAttributeU32(        std::string_view name, U32& value );
    bool                getAttributeS32(        std::string_view name, S32& value );
    bool                getAttributeF32(        std::string_view name, F32& value );
    bool                getAttributeF64(        std::string_view name, F64& value );
    bool                getAttributeColor(      std::string_view name, LLColor4& value );
    bool                getAttributeColor4(     std::string_view name, LLColor4& value );
    bool                getAttributeColor4U(    std::string_view name, LLColor4U& value );
    bool                getAttributeVector3(    std::string_view name, LLVector3& value );
    bool                getAttributeVector3d(   std::string_view name, LLVector3d& value );
    bool                getAttributeQuat(       std::string_view name, LLQuaternion& value );
    bool                getAttributeUUID(       std::string_view name, LLUUID& value );
    bool                getAttributeString(     std::string_view name, std::string& value );

    const std::string& getContents()
    {
        return mContents;
    }
    std::string getTextContents();

    LLXmlTreeNode*  getParent()                         { return mParent; }
    LLXmlTreeNode*  getFirstChild();
    LLXmlTreeNode*  getNextChild();
    S32             getChildCount()                     { return (S32)mChildren.size(); }
    LLXmlTreeNode*  getChildByName( std::string_view name );  // returns first child with name, nullptr if none
    LLXmlTreeNode*  getNextNamedChild();                // returns next child with name, nullptr if none

protected:
    const std::string* getAttribute( LLStdStringHandle name)
    {
        attribute_map_t::iterator iter = mAttributes.find(name);
        return (iter == mAttributes.end()) ? 0 : iter->second;
    }

private:
    void            addAttribute( std::string_view name, std::string value );
    void            appendContents( const char* str, std::string::size_type len );
    void            addChild( LLXmlTreeNode* child );

    void            dump( const std::string& prefix );

protected:
    using attribute_map_t = std::map<LLStdStringHandle, const std::string*>;
    attribute_map_t                     mAttributes;

private:
    std::string                         mName;
    std::string                         mContents;

    using children_t = std::vector<class LLXmlTreeNode*>;
    children_t                          mChildren;
    children_t::iterator                mChildrenIter;

    using child_map_t = std::multimap<LLStdStringHandle, LLXmlTreeNode*>;
    child_map_t                         mChildMap;      // for fast name lookups
    child_map_t::iterator               mChildMapIter;
    child_map_t::iterator               mChildMapEndIter;

    LLXmlTreeNode*                      mParent;
    LLXmlTree*                          mTree;
};

//////////////////////////////////////////////////////////////
// LLXmlTreeParser

class LLXmlTreeParser
{
public:
    LLXmlTreeParser(LLXmlTree* tree);
    virtual ~LLXmlTreeParser();

    bool parseFile(const std::string &path, LLXmlTreeNode** root, bool keep_contents );

    S32                 getCurrentLineNumber() const { return mErrorLine; }
    const std::string&  getErrorString() const { return mErrorString; }

protected:
    void buildTree(const pugi::xml_node& root_element);

    // Template method pattern, unused: no subclass of this parser exists, so
    // every node the tree holds is an LLXmlTreeNode. See its destructor
    // before returning anything else from here.
    virtual LLXmlTreeNode* CreateXmlTreeNode(std::string name, LLXmlTreeNode* parent);

protected:
    LLXmlTree*      mTree;
    LLXmlTreeNode*  mRoot;
    bool            mKeepContents;
    S32             mErrorLine;
    std::string     mErrorString;
};

#endif  // LL_LLXMLTREE_H
