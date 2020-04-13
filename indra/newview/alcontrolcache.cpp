#include "llviewerprecompiledheaders.h"

#include "alcontrolcache.h"
#include "llviewercontrol.h"

bool ALControlCache::AutoSnapshot = false;
bool ALControlCache::AutomaticFly = true;
bool ALControlCache::DebugAvatarRezTime = false;
bool ALControlCache::EditLinkedParts = false;
F32  ALControlCache::GridDrawSize;
F32  ALControlCache::GridOpacity;
F32  ALControlCache::GridResolution;
bool ALControlCache::LimitSelectDistance = true;
bool ALControlCache::MapShowInfohubs;
bool ALControlCache::MapShowEvents;
bool ALControlCache::MapShowLandForSale;
bool ALControlCache::MapShowPeople;
bool ALControlCache::MapShowTelehubs;
F32  ALControlCache::MaxSelectDistance = 512.f;
bool ALControlCache::NavBarShowParcelProperties = true;
F32	 ALControlCache::NearMeRange = 4096.f;
U32  ALControlCache::PreferredMaturity;
bool ALControlCache::ShowAdultEvents;
bool ALControlCache::ShowMatureEvents;
bool ALControlCache::SnapEnabled;
S32  ALControlCache::ToastGap;


#define DECLARE_CTRL(ctrl, type, ctrl_type) \
	{ \
		LLControlVariable* cntrl_ptr = gSavedSettings.getControl(#ctrl); \
		if (!cntrl_ptr) \
		{ \
			LL_WARNS() << "Global setting name not found:" << #ctrl << LL_ENDL; \
		} \
		else \
		{ \
			cntrl_ptr->getSignal()->connect(0, [&](LLControlVariable* control, const LLSD& new_val, const LLSD&)  \
				{ \
					ctrl = convert_from_llsd<type>(new_val, ctrl_type, #ctrl); \
				}); \
			ctrl = convert_from_llsd<type>(cntrl_ptr->getValue(), ctrl_type, #ctrl); \
			LL_INFOS() << "Global cached setting: " << #ctrl << " initialized with value: " << ctrl << LL_ENDL; \
		} \
	} \

#define DECLARE_CTRL_BOOL(ctrl) DECLARE_CTRL(ctrl, bool, TYPE_BOOLEAN);
#define DECLARE_CTRL_F32(ctrl) DECLARE_CTRL(ctrl, F32, TYPE_F32);
#define DECLARE_CTRL_U32(ctrl) DECLARE_CTRL(ctrl, U32, TYPE_U32);
#define DECLARE_CTRL_S32(ctrl) DECLARE_CTRL(ctrl, S32, TYPE_S32);

// static
void ALControlCache::initControls()
{
	// Keep this list alphabatized.
	DECLARE_CTRL_BOOL(AutoSnapshot);
	DECLARE_CTRL_BOOL(AutomaticFly);
	DECLARE_CTRL_BOOL(DebugAvatarRezTime);
	DECLARE_CTRL_BOOL(EditLinkedParts);
	DECLARE_CTRL_F32(GridDrawSize);
	DECLARE_CTRL_F32(GridOpacity);
	DECLARE_CTRL_F32(GridResolution);
	DECLARE_CTRL_BOOL(LimitSelectDistance);
	DECLARE_CTRL_BOOL(MapShowInfohubs);
	DECLARE_CTRL_BOOL(MapShowEvents);
	DECLARE_CTRL_BOOL(MapShowLandForSale);
	DECLARE_CTRL_BOOL(MapShowPeople);
	DECLARE_CTRL_BOOL(MapShowTelehubs);
	DECLARE_CTRL_F32(MaxSelectDistance);
	DECLARE_CTRL_BOOL(NavBarShowParcelProperties);
	DECLARE_CTRL_F32(NearMeRange);
	DECLARE_CTRL_U32(PreferredMaturity);
	DECLARE_CTRL_BOOL(ShowAdultEvents);
	DECLARE_CTRL_BOOL(ShowMatureEvents);
	DECLARE_CTRL_BOOL(SnapEnabled);
	DECLARE_CTRL_S32(ToastGap);
}
