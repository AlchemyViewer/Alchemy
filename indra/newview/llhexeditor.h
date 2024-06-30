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
    void setValue(const LLSD& value) override;
    LLSD getValue() const override;
    void setColumns(U8 columns);
    U8   getColumns() { return mColumns; };
    U32  getLineCount() const;
    F32  getSuggestedWidth(U8 cols = -1);
    U32  getProperSelectionStart() const;
    U32  getProperSelectionEnd() const;
    void reshape(S32 width, S32 height, bool called_from_parent) override;
    void setFocus(bool b) override;

    bool handleScrollWheel(S32 x, S32 y, S32 clicks) override;
    bool handleMouseDown(S32 x, S32 y, MASK mask) override;
    bool handleHover(S32 x, S32 y, MASK mask) override;
    bool handleMouseUp(S32 x, S32 y, MASK mask) override;

    bool handleKeyHere(KEY key, MASK mask) override;
    bool handleKey(KEY key, MASK mask, bool called_from_parent) override;
    bool handleUnicodeChar(llwchar uni_char, bool called_from_parent) override;
    bool handleUnicodeCharHere(llwchar uni_char) override;

    /*virtual*/ bool    postBuild() override;
    void draw() override;

    void moveCursor(U32 pos, bool second_nibble);

    void insert(U32 pos, std::vector<U8> new_data, bool undoable);
    void overwrite(U32 first_pos, U32 last_pos, std::vector<U8> new_data, bool undoable);
    void del(U32 first_pos, U32 last_pos, bool undoable);

    virtual void    cut() override;
    virtual bool    canCut() const override;

    virtual void    copy() override;
    virtual bool    canCopy() const override;

    virtual void    paste() override;
    virtual bool    canPaste() const override;

    virtual void    doDelete() override;
    virtual bool    canDoDelete() const override;

    virtual void    selectAll() override;
    virtual bool    canSelectAll() const override;

    virtual void    deselect() override;
    virtual bool    canDeselect() const override;

    virtual void    undo() override;
    virtual bool    canUndo() const override;

    virtual void    redo() override;
    virtual bool    canRedo() const override;

private:
    std::vector<U8> mValue;
    U8 mColumns;

    std::string mName;
    U32 mCursorPos;
    bool mSecondNibble;
    bool mInData;
    bool mSelecting;
    bool mHasSelection;
    U32 mSelectionStart;
    U32 mSelectionEnd;

    LLFontGL* mGLFont;
    LLRect mTextRect;
    LLScrollbar* mScrollbar;
    LLViewBorder* mBorder;

    LLUndoBuffer* mUndoBuffer;

    void changedLength();
    void getPosAndContext(S32 x, S32 y, bool force_context, U32& pos, bool& in_data, bool& second_nibble) const;
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
