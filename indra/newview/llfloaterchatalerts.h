/** 
 *
 * Copyright (c) 2012, Kitty Barnett
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
#ifndef LLFLOATERCHATALERTS_H
#define LLFLOATERCHATALERTS_H

#include "llfloater.h"

class LLButton;
class LLCheckBoxCtrl;
class LLColorSwatchCtrl;
class LLIconCtrl;
class LLLineEditor;
class LLScrollListCtrl;

// ============================================================================

class LLFloaterChatAlerts : public LLFloater
{
	friend class LLFloaterReg;
private:
	LLFloaterChatAlerts(const LLSD& sdKey);
public:
	/*virtual*/ ~LLFloaterChatAlerts();
	/*virtual*/ BOOL canClose();
	/*virtual*/ void onOpen(const LLSD& sdKey);
	/*virtual*/ void onClose(bool app_quitting);
	/*virtual*/ BOOL postBuild();
	/*virtual*/ S32  notifyParent(const LLSD& sdInfo);

public:
	bool isEntryDirty() const;
protected:
	void onEntryNew();
	void onEntryDelete();
	void onEntrySave();
	void onEntrySaveChanges(const LLUUID& idNewEntry, bool fCloseFloater);
	void onEntrySaveChangesCallback(const LLSD& notification, const LLSD& response);
	void onEntryRevert();
	void onEntrySelect();
	void onSoundClearItem();
	void onToggleChatAlerts(const LLSD& sdValue);
	void onToggleTriggerType();
	void refresh();
	void refreshList();
	void refreshEntry(bool fNewEntry);
	void refreshSound();

protected:
	LLScrollListCtrl*  m_pAlertList;
	bool               m_fNewEntry;
	LLUUID             m_idCurEntry;
	LLLineEditor*      m_pKeywordEditor;
	LLCheckBoxCtrl*    m_pKeywordCase;
	LLColorSwatchCtrl* m_pColorCtrl;

	bool               m_fSoundChanged;
	LLUUID             m_idSoundItem;
	LLIconCtrl*        m_pSoundIconCtrl;
	LLLineEditor*      m_pSoundEditor;
	LLButton*          m_pSoundClearBtn;

	LLCheckBoxCtrl*    m_pTriggerChat;
	LLCheckBoxCtrl*    m_pTriggerIM;
	LLCheckBoxCtrl*    m_pTriggerGroup;
	bool               m_fPendingSave;

	bool m_fChatAlertsEnabled;
	boost::signals2::connection mChatAlertsConnection;
};

// ============================================================================
// LLSoundDropTarget helper class
//

class LLSoundDropTarget : public LLView
{
public:
	struct Params : public LLInitParam::Block<Params, LLView::Params>
	{
		Params()
		{
			changeDefault(mouse_opaque, false);
			changeDefault(follows.flags, FOLLOWS_ALL);
		}
	};

	LLSoundDropTarget(const Params&);
	virtual ~LLSoundDropTarget();

	/*
	 * Member functions
	 */
public:
	typedef boost::signals2::signal<void (const LLUUID&)> drop_signal_t;
	boost::signals2::connection setDropCallback(const drop_signal_t::slot_type& cb);

	/*
	 * LLView overrides
	 */
public:
	/*virtual*/ BOOL handleDragAndDrop(S32 x, S32 y, MASK mask, BOOL drop, EDragAndDropType cargo_type, void* cargo_data, EAcceptance* accept, std::string& tooltip_msg);

	/*
	 * Member variables
	 */
protected:
	drop_signal_t* m_pDropSignal;
};

// ============================================================================

#endif  // LLFLOATERCHATALERTS_H
