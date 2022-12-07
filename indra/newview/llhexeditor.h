/**
 * @file dohexeditor.h
 * @brief DOHexEditor Widget
 * @author Day Oh, Skills, Cinder
 * 
 * $LicenseInfo:firstyear=2009&license=WTFPLV2$
 *  
 */

#ifndef LLHexEditor_H
#define LLHexEditor_H

#define MIN_COLS 8
#define MAX_COLS 48

#ifndef COLUMN_SPAN
#define COLUMN_SPAN
#endif

#include "lluictrl.h"
#include "llscrollbar.h"
#include "llviewborder.h"
#include "llundo.h"
#include "lleditmenuhandler.h"

class LLHexEditor : public LLUICtrl, public LLEditMenuHandler
{
public:

    struct Params : LLInitParam::Block<Params, LLUICtrl::Params>
    { };

	~LLHexEditor() override;
	void setValue(const LLSD& value);
	LLSD getValue() const;
	void setColumns(U8 columns);
	U8   getColumns() { return mColumns; };
	U32  getLineCount() const;
	F32  getSuggestedWidth(U8 cols = -1);
	U32  getProperSelectionStart() const;
	U32  getProperSelectionEnd() const;
	void reshape(S32 width, S32 height, BOOL called_from_parent);
	void setFocus(BOOL b);
	
	BOOL handleScrollWheel(S32 x, S32 y, S32 clicks) override;
	BOOL handleMouseDown(S32 x, S32 y, MASK mask);
	BOOL handleHover(S32 x, S32 y, MASK mask);
	BOOL handleMouseUp(S32 x, S32 y, MASK mask);

	BOOL handleKeyHere(KEY key, MASK mask);
	BOOL handleKey(KEY key, MASK mask, BOOL called_from_parent);
	BOOL handleUnicodeChar(llwchar uni_char, BOOL called_from_parent);
	BOOL handleUnicodeCharHere(llwchar uni_char);

	/*virtual*/ BOOL 	postBuild();
	void draw();

	void moveCursor(U32 pos, BOOL second_nibble);

	void insert(U32 pos, std::vector<U8> new_data, BOOL undoable);
	void overwrite(U32 first_pos, U32 last_pos, std::vector<U8> new_data, BOOL undoable);
	void del(U32 first_pos, U32 last_pos, BOOL undoable);

	virtual void	cut() override;
	virtual BOOL	canCut() const override;

	virtual void	copy() override;
	virtual BOOL	canCopy() const override;

	virtual void	paste() override;
	virtual BOOL	canPaste() const override;
	
	virtual void	doDelete() override;
	virtual BOOL	canDoDelete() const override;

	virtual void	selectAll() override;
	virtual BOOL	canSelectAll() const override;

	virtual void	deselect() override;
	virtual BOOL	canDeselect() const override;

	virtual void	undo() override;
	virtual BOOL	canUndo() const override;

	virtual void	redo() override;
	virtual BOOL	canRedo() const override;

private:
	std::vector<U8> mValue;
	U8 mColumns;

	std::string mName;
	U32 mCursorPos;
	BOOL mSecondNibble;
	BOOL mInData;
	BOOL mSelecting;
	BOOL mHasSelection;
	U32 mSelectionStart;
	U32 mSelectionEnd;

	LLFontGL* mGLFont;
	LLRect mTextRect;
	LLScrollbar* mScrollbar;
	LLViewBorder* mBorder;

	LLUndoBuffer* mUndoBuffer;

	void changedLength();
	void getPosAndContext(S32 x, S32 y, BOOL force_context, U32& pos, BOOL& in_data, BOOL& second_nibble) const;
protected:
	LLHexEditor(const Params & p);
	friend class LLUICtrlFactory;
};

class LLUndoHex : public LLUndoBuffer::LLUndoAction
{
protected:
	LLHexEditor*    mHexEditor = nullptr;
	U32             mFirstPos  = 0;
	U32             mLastPos   = 0;
	std::vector<U8> mOldData;
	std::vector<U8> mNewData;
public:
	static LLUndoAction* create() { return new LLUndoHex(); }
	virtual void set(LLHexEditor* hex_editor,
					 void (*undo_action)(LLUndoHex*),
					 void (*redo_action)(LLUndoHex*),
					 U32 first_pos,
					 U32 last_pos,
					 std::vector<U8> old_data,
					 std::vector<U8> new_data);
	void (*mUndoAction)(LLUndoHex*);
	void (*mRedoAction)(LLUndoHex*);
    virtual void undo() override;
    virtual void redo() override;
	
	static void undoInsert(LLUndoHex* action);
	static void redoInsert(LLUndoHex* action);
	static void undoOverwrite(LLUndoHex* action);
	static void redoOverwrite(LLUndoHex* action);
	static void undoDel(LLUndoHex* action);
	static void redoDel(LLUndoHex* action);
};

class LLHexInsert : public LLUndoHex
{
    virtual void undo() override;
    virtual void redo() override;
};

class LLHexOverwrite : public LLUndoHex
{
    virtual void undo() override;
    virtual void redo() override;
};

class LLHexDel : public LLUndoHex
{
    virtual void undo() override;
    virtual void redo() override;
};

#endif // LLHexEditor_H
