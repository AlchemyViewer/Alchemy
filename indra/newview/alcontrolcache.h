#ifndef AL_ALCONTROLCACHE_H
#define AL_ALCONTROLCACHE_H

struct ALControlCache
{
	static void initControls();

	static bool		AutoSnapshot;
	static bool		AutomaticFly;
	static bool		DebugAvatarRezTime;
	static bool		EditLinkedParts;
	static F32		GridDrawSize;
	static F32		GridOpacity;
	static F32		GridResolution;
	static bool		LimitSelectDistance;
	static bool		MapShowInfohubs;
	static bool		MapShowEvents;
	static bool		MapShowLandForSale;
	static bool		MapShowPeople;
	static bool		MapShowTelehubs;
	static F32		MaxSelectDistance;
	static bool		NavBarShowParcelProperties;
	static F32		NearMeRange;
	static U32		PreferredMaturity;
	static bool		ShowAdultEvents;
	static bool		ShowMatureEvents;
	static bool		SnapEnabled;
	static S32		ToastGap;
};

#endif