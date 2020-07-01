/** 
 * @file qtoolalign.h
 * @brief A tool to align objects
 */

#ifndef AL_ALTOOLALIGN_H
#define AL_ALTOOLALIGN_H

#include "llsingleton.h"
#include "lltool.h"
#include "llbbox.h"

class LLViewerObject;
class LLPickInfo;
class LLToolSelectRect;

class ALToolAlign final
:	public LLTool, public LLSingleton<ALToolAlign>
{
	LLSINGLETON(ALToolAlign);
	~ALToolAlign() = default;
    
public:
	void	handleSelect() override;
	void	handleDeselect() override;
	BOOL	handleMouseDown(S32 x, S32 y, MASK mask) override;
	BOOL    handleHover(S32 x, S32 y, MASK mask) override;
	void	render() override;
	BOOL	canAffectSelection();

	static void pickCallback(const LLPickInfo& pick_info);

private:
	void            align();
	void            computeManipulatorSize();
	void            renderManipulators();
	BOOL            findSelectedManipulator(S32 x, S32 y);
	
	LLBBox          mBBox;
	F32             mManipulatorSize;
	S32             mHighlightedAxis;
	F32             mHighlightedDirection;
	bool            mForce;
};

#endif // AL_ALTOOLALIGN_H
