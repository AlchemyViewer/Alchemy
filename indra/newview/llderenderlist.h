/** 
 *
 * Copyright (c) 2011-2014, Kitty Barnett
 * 
 * The source code in this file is provided to you under the terms of the 
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A 
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-licence.txt 
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 * 
 * By copying, modifying or distributing this software, you acknowledge that
 * you have read and understood your obligations described above, and agree to 
 * abide by those obligations.
 * 
 */
#ifndef LL_DERENDERLIST_H
#define LL_DERENDERLIST_H

#include "llsingleton.h"
#include "lluuid.h"

// ============================================================================
// Forward declarations
//

class LLSelectNode;
class LLVOCacheEntry;
class LLViewerObject;

// ============================================================================
// LLDerenderEntry
//

class LLDerenderEntry
{
	/*
	 * Type declarations
	 */
public:
	enum EEntryType
	{
		TYPE_OBJECT = 0,
		TYPE_AVATAR,
		TYPE_ALL,
		TYPE_NONE
	};

	enum
	{
		FLAG_NONE    = 0x00,
		FLAG_PERSIST = 0x01
	};

	/*
	 * Constructors
	 */
protected:
	LLDerenderEntry(EEntryType eType, bool fPersists);
	LLDerenderEntry(EEntryType eType, const LLSD& sdData);
public:
	virtual ~LLDerenderEntry() {}

	/*
	 * Member functions
	 */
public:
	std::string   getName() const { return m_strEntryName; }
	const LLUUID& getID() const   { return m_idEntry; }
	EEntryType    getType() const { return m_eType; }
	bool          isPersistent() const { return m_nFlags & FLAG_PERSIST; }
	bool          isValid() const { return (m_eType != TYPE_NONE) && (m_idEntry.notNull()); }

	static std::unique_ptr<LLDerenderEntry> fromLLSD(const LLSD& sdData);
	virtual LLSD            toLLSD() const;

	/*
	 * Member variables
	 */
protected:
	EEntryType  m_eType;
	S32         m_nFlags;
	LLUUID      m_idEntry;
	std::string m_strEntryName;
};

// ============================================================================
// LLDerenderObject
//

class LLDerenderObject : public LLDerenderEntry
{
	/*
	 * Constructors
	 */
public:
	LLDerenderObject(const LLSelectNode* pNode, bool fPersist);
	LLDerenderObject(const LLSD& sdData);

	/*
	 * Base class overrides
	 */
public:
	/*virtual*/ LLSD toLLSD() const;

	/*
	 * Member variables
	 */
public:
	std::string    strRegionName;		// Region name
	LLVector3      posRegion;			// Position of the object on the region
	U64            idRegion;			// Region handle
	U32            idRootLocal;			// Local object ID of the root (region-specific)
	std::list<U32> idsChildLocal;		// Local object ID of all child prims
};

// ============================================================================
// LLDerenderAvatar
//

class LLDerenderAvatar : public LLDerenderEntry
{
public:
	LLDerenderAvatar(bool fPersists)
		: LLDerenderEntry(TYPE_AVATAR, fPersists)
	{
	}
	LLDerenderAvatar(const LLSD& sdData)
		: LLDerenderEntry(TYPE_AVATAR, sdData)
	{
	}
};

// ============================================================================
// LLDerenderList
//

class LLDerenderList : public LLSingleton<LLDerenderList>
{
	LLSINGLETON(LLDerenderList);
protected:
	/*virtual*/ ~LLDerenderList();

	void load();
	void save() const;

	/*
	 * Entry helper functions
	 */
public:
	using entry_list_t = std::list<std::unique_ptr<LLDerenderEntry>>;

	const entry_list_t& getEntries() const { return m_Entries; }
	void removeObject(LLDerenderEntry::EEntryType eType, const LLUUID& idObject);
	void removeObjects(LLDerenderEntry::EEntryType eType, const uuid_vec_t& idsObject);
protected:
	entry_list_t::iterator findEntry(LLDerenderEntry::EEntryType eType, const LLUUID& idEntry);
	entry_list_t::iterator findObjectEntry(U64 idRegion, const LLUUID& idObject, U32 idRootLocal);

	/*
	 * Object helper functions
	 */
public:
	bool              addSelection(bool fPersist, std::vector<LLUUID>* pIdList = NULL);
	static bool       canAdd(const LLViewerObject* pObj);
	static bool       canAddSelection();
	bool              processObjectUpdate(U64 idRegion, const LLUUID& idObject, const LLVOCacheEntry* pEntry);
	bool              processObjectUpdate(U64 idRegion, const LLUUID& idObject, U32 idObjectLocal, U32 idRootLocal);
	bool              processObjectUpdate(U64 idRegion, const LLUUID& idObject, U32 idObjectLocal, const U8* pBuffer);
protected:
	LLDerenderObject* getObjectEntry(const LLUUID& idObject) /*const*/;
	LLDerenderObject* getObjectEntry(U64 idRegion, const LLUUID& idObject, U32 idRootLocal) /*const*/;
	bool              isDerendered(const LLUUID& idObject) /*const*/                                { return getObjectEntry(idObject) != NULL; }
	bool              isDerendered(U64 idRegion, const LLUUID& idObject, U32 idRootLocal) /*const*/ { return getObjectEntry(idRegion, idObject, idRootLocal) != NULL; }

	/*
	 * Static member functions
	 */
public:
	typedef boost::signals2::signal<void()> change_signal_t;
	static boost::signals2::connection setChangeCallback(const change_signal_t::slot_type& cb) { return s_ChangeSignal.connect(cb); }

protected:
	entry_list_t m_Entries;

	static change_signal_t	s_ChangeSignal;
	static std::string		s_PersistFilename;
};

// ============================================================================

#endif // LL_DERENDERLIST_H
