/**
 *
 * Copyright (c) 2009-2020, Kitty Barnett
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

#pragma once

#include <absl/strings/string_view.h>

// ============================================================================
// Defines
//

// Version of the specifcation we report
const S32 RLV_VERSION_MAJOR = 3;
const S32 RLV_VERSION_MINOR = 4;
const S32 RLV_VERSION_PATCH = 3;
const S32 RLV_VERSION_BUILD = 0;

// Version of the specifcation we report (in compatibility mode)
const S32 RLV_VERSION_MAJOR_COMPAT = 2;
const S32 RLV_VERSION_MINOR_COMPAT = 9;
const S32 RLV_VERSION_PATCH_COMPAT = 28;
const S32 RLV_VERSION_BUILD_COMPAT = 0;

// Implementation version
const S32 RLVa_VERSION_MAJOR = 2;
const S32 RLVa_VERSION_MINOR = 4;
const S32 RLVa_VERSION_PATCH = 1;
const S32 RLVa_IMPL_ID = 13;

// Uncomment before a final release
//#define RLV_RELEASE

// Defining these makes it easier if we ever need to change our tag
#define RLV_WARNS		LL_WARNS("RLV")
#define RLV_INFOS		LL_INFOS("RLV")
#define RLV_DEBUGS		LL_DEBUGS("RLV")
#define RLV_ENDL		LL_ENDL
#define RLV_VERIFY(f)	if (!(f)) { RlvUtil::notifyFailedAssertion(#f, __FILE__, __LINE__); }

#if LL_RELEASE_WITH_DEBUG_INFO || LL_DEBUG
	// Turn on extended debugging information
	#define RLV_DEBUG
	// Make sure we halt execution on errors
	#define RLV_ERRS				LL_ERRS("RLV")
	// Keep our asserts separate from LL's
	#define RLV_ASSERT(f)			if (!(f)) { RLV_ERRS << "ASSERT (" << #f << ")" << RLV_ENDL; }
	#define RLV_ASSERT_DBG(f)		RLV_ASSERT(f)
#else
	// Don't halt execution on errors in release
	#define RLV_ERRS				LL_WARNS("RLV")
	// We don't want to check assertions in release builds
	#ifndef RLV_RELEASE
		#define RLV_ASSERT(f)		RLV_VERIFY(f)
		#define RLV_ASSERT_DBG(f)
	#else
		#define RLV_ASSERT(f)
		#define RLV_ASSERT_DBG(f)
	#endif // RLV_RELEASE
#endif // LL_RELEASE_WITH_DEBUG_INFO || LL_DEBUG

#define RLV_ROOT_FOLDER					"#RLV"
#define RLV_CMD_PREFIX					'@'
#define RLV_MODIFIER_ANIMATION_FREQUENCY 10
#define RLV_MODIFIER_TPLOCAL_DEFAULT    256.f			// Any teleport that's more than a region away is non-local
#define RLV_MODIFIER_FARTOUCH_DEFAULT   1.5f			// Specifies the default @fartouch distance
#define RLV_MODIFIER_SITTP_DEFAULT      1.5f			// Specifies the default @sittp distance
#define RLV_OPTION_SEPARATOR			";"				// Default separator used in command options
#define RLV_PUTINV_PREFIX				"#RLV/~"
#define RLV_PUTINV_SEPARATOR			"/"
#define RLV_PUTINV_MAXDEPTH				4
#define RLV_SETGROUP_THROTTLE           60.f
#define RLV_SETROT_OFFSET				F_PI_BY_TWO		// @setrot is off by 90 degrees with the rest of SL
#define RLV_STRINGS_FILE				"rlva_strings.xml"

#define RLV_FOLDER_FLAG_NOSTRIP			"nostrip"
#define RLV_FOLDER_PREFIX_HIDDEN		'.'
#define RLV_FOLDER_PREFIX_PUTINV		'~'
#define RLV_FOLDER_INVALID_CHARS		"/"

// ============================================================================
// Enumeration declarations
//

// NOTE: any changes to this enumeration should be reflected in the RlvBehaviourDictionary constructor
enum ERlvBehaviour {
	RLV_BHVR_DETACH = 0,			// "detach"
	RLV_BHVR_ADDATTACH,				// "addattach"
	RLV_BHVR_REMATTACH,				// "remattach"
	RLV_BHVR_ADDOUTFIT,				// "addoutfit"
	RLV_BHVR_REMOUTFIT,				// "remoutfit"
	RLV_BHVR_SHAREDWEAR,			// "sharedwear"
	RLV_BHVR_SHAREDUNWEAR,			// "sharedunwear"
	RLV_BHVR_UNSHAREDWEAR,			// "unsharedwear"
	RLV_BHVR_UNSHAREDUNWEAR,		// "unsharedunwear"
	RLV_BHVR_EMOTE,					// "emote"
	RLV_BHVR_SENDCHAT,				// "sendchat"
	RLV_BHVR_RECVCHAT,				// "recvchat"
	RLV_BHVR_RECVCHATFROM,			// "recvchatfrom"
	RLV_BHVR_RECVEMOTE,				// "recvemote"
	RLV_BHVR_RECVEMOTEFROM,			// "recvemotefrom"
	RLV_BHVR_REDIRCHAT,				// "redirchat"
	RLV_BHVR_REDIREMOTE,			// "rediremote"
	RLV_BHVR_CHATWHISPER,			// "chatwhisper"
	RLV_BHVR_CHATNORMAL,			// "chatnormal"
	RLV_BHVR_CHATSHOUT,				// "chatshout"
	RLV_BHVR_SENDCHANNEL,
	RLV_BHVR_SENDCHANNELEXCEPT,
	RLV_BHVR_SENDIM,				// "sendim"
	RLV_BHVR_SENDIMTO,				// "sendimto"
	RLV_BHVR_RECVIM,				// "recvim"
	RLV_BHVR_RECVIMFROM,			// "recvimfrom"
	RLV_BHVR_STARTIM,				// "startim"
	RLV_BHVR_STARTIMTO,				// "startimto"
	RLV_BHVR_SENDGESTURE,
	RLV_BHVR_PERMISSIVE,			// "permissive"
	RLV_BHVR_NOTIFY,				// "notify"
	RLV_BHVR_SHARE,
	RLV_BHVR_SHOWINV,				// "showinv"
	RLV_BHVR_SHOWMINIMAP,			// "showminimap"
	RLV_BHVR_SHOWWORLDMAP,			// "showworldmap"
	RLV_BHVR_SHOWLOC,				// "showloc"
	RLV_BHVR_SHOWNAMES,				// "shownames"
	RLV_BHVR_SHOWNAMETAGS,			// "shownametags"
	RLV_BHVR_SHOWNEARBY,
	RLV_BHVR_SHOWHOVERTEXT,			// "showhovertext"
	RLV_BHVR_SHOWHOVERTEXTHUD,		// "showhovertexthud"
	RLV_BHVR_SHOWHOVERTEXTWORLD,	// "showhovertextworld"
	RLV_BHVR_SHOWHOVERTEXTALL,		// "showhovertextall"
	RLV_BHVR_SHOWSELF,
	RLV_BHVR_SHOWSELFHEAD,
	RLV_BHVR_TPLM,					// "tplm"
	RLV_BHVR_TPLOC,					// "tploc"
	RLV_BHVR_TPLOCAL,
	RLV_BHVR_TPLURE,				// "tplure"
	RLV_BHVR_TPREQUEST,				// "tprequest"
	RLV_BHVR_VIEWNOTE,				// "viewnote"
	RLV_BHVR_VIEWSCRIPT,			// "viewscript"
	RLV_BHVR_VIEWTEXTURE,			// "viewtexture"
	RLV_BHVR_ACCEPTPERMISSION,		// "acceptpermission"
	RLV_BHVR_ACCEPTTP,				// "accepttp"
	RLV_BHVR_ACCEPTTPREQUEST,		// "accepttprequest"
	RLV_BHVR_ALLOWIDLE,				// "allowidle"
	RLV_BHVR_BUY,					// "buy"
	RLV_BHVR_EDIT,					// "edit"
	RLV_BHVR_EDITATTACH,
	RLV_BHVR_EDITOBJ,				// "editobj"
	RLV_BHVR_EDITWORLD,
	RLV_BHVR_VIEWTRANSPARENT,
	RLV_BHVR_VIEWWIREFRAME,
	RLV_BHVR_PAY,					// "pay"
	RLV_BHVR_REZ,					// "rez"
	RLV_BHVR_FARTOUCH,				// "fartouch"
	RLV_BHVR_INTERACT,				// "interact"
	RLV_BHVR_TOUCHTHIS,				// "touchthis"
	RLV_BHVR_TOUCHATTACH,			// "touchattach"
	RLV_BHVR_TOUCHATTACHSELF,		// "touchattachself"
	RLV_BHVR_TOUCHATTACHOTHER,		// "touchattachother"
	RLV_BHVR_TOUCHHUD,				// "touchhud"
	RLV_BHVR_TOUCHWORLD,			// "touchworld"
	RLV_BHVR_TOUCHALL,				// "touchall"
	RLV_BHVR_TOUCHME,				// "touchme"
	RLV_BHVR_FLY,					// "fly"
	RLV_BHVR_JUMP,                  // "jump"
	RLV_BHVR_SETGROUP,				// "setgroup"
	RLV_BHVR_UNSIT,					// "unsit"
	RLV_BHVR_SIT,					// "sit"
	RLV_BHVR_SITGROUND,
	RLV_BHVR_SITTP,					// "sittp"
	RLV_BHVR_STANDTP,				// "standtp"
	RLV_BHVR_SETDEBUG,				// "setdebug"
	RLV_BHVR_SETENV,				// "setenv"
	RLV_BHVR_ALWAYSRUN,				// "alwaysrun"
	RLV_BHVR_TEMPRUN,				// "temprun"
	RLV_BHVR_DETACHME,				// "detachme"
	RLV_BHVR_ATTACHTHIS,			// "attachthis"
	RLV_BHVR_ATTACHTHISEXCEPT,		// "attachthis_except"
	RLV_BHVR_DETACHTHIS,			// "detachthis"
	RLV_BHVR_DETACHTHISEXCEPT,		// "detachthis_except"
	RLV_BHVR_ADJUSTHEIGHT,			// "adjustheight"
	RLV_BHVR_GETHEIGHTOFFSET,       // "getheightoffset"
	RLV_BHVR_TPTO,					// "tpto"
	RLV_BHVR_VERSION,				// "version"
	RLV_BHVR_VERSIONNEW,			// "versionnew"
	RLV_BHVR_VERSIONNUM,			// "versionnum"
	RLV_BHVR_GETATTACH,				// "getattach"
	RLV_BHVR_GETATTACHNAMES,		// "getattachnames"
	RLV_BHVR_GETADDATTACHNAMES,		// "getaddattachnames"
	RLV_BHVR_GETREMATTACHNAMES,		// "getremattachnames"
	RLV_BHVR_GETOUTFIT,				// "getoutfit"
	RLV_BHVR_GETOUTFITNAMES,		// "getoutfitnames"
	RLV_BHVR_GETADDOUTFITNAMES,		// "getaddoutfitnames"
	RLV_BHVR_GETREMOUTFITNAMES,		// "getremoutfitnames"
	RLV_BHVR_FINDFOLDER,			// "findfolder"
	RLV_BHVR_FINDFOLDERS,			// "findfolders"
	RLV_BHVR_GETPATH,				// "getpath"
	RLV_BHVR_GETPATHNEW,			// "getpathnew"
	RLV_BHVR_GETINV,				// "getinv"
	RLV_BHVR_GETINVWORN,			// "getinvworn"
	RLV_BHVR_GETGROUP,				// "getgroup"
	RLV_BHVR_GETSITID,				// "getsitid"
	RLV_BHVR_GETCOMMAND,			// "getcommand"
	RLV_BHVR_GETSTATUS,				// "getstatus"
	RLV_BHVR_GETSTATUSALL,			// "getstatusall"
	RLV_CMD_FORCEWEAR,				// Internal representation of all force wear commands

	// Camera (behaviours)
	RLV_BHVR_SETCAM,                // Gives an object exclusive control of the user's camera
	RLV_BHVR_SETCAM_AVDIST,			// Distance at which nearby avatars turn into a silhouette
	RLV_BHVR_SETCAM_AVDISTMIN,		// Enforces a minimum distance from the avatar (in m)
	RLV_BHVR_SETCAM_AVDISTMAX,		// Enforces a maximum distance from the avatar (in m)
	RLV_BHVR_SETCAM_ORIGINDISTMIN,	// Enforces a minimum distance from the camera origin (in m)
	RLV_BHVR_SETCAM_ORIGINDISTMAX,	// Enforces a maximum distance from the camera origin (in m)
	RLV_BHVR_SETCAM_EYEOFFSET,      // Changes the default camera offset
	RLV_BHVR_SETCAM_EYEOFFSETSCALE, // Changes the default camera offset scale
	RLV_BHVR_SETCAM_FOCUSOFFSET,    // Changes the default camera focus offset
	RLV_BHVR_SETCAM_FOCUS,			// Forces the camera focus and/or position to a specific object, avatar or position
	RLV_BHVR_SETCAM_FOV,			// Changes the current - vertical - field of view
	RLV_BHVR_SETCAM_FOVMIN,			// Enforces a minimum - vertical - FOV (in degrees)
	RLV_BHVR_SETCAM_FOVMAX,			// Enforces a maximum - vertical - FOV (in degrees)
	RLV_BHVR_SETCAM_MOUSELOOK,		// Prevent the user from going into mouselook
	RLV_BHVR_SETCAM_TEXTURES,		// Replaces all textures with the specified texture (or the default unrezzed one)
	RLV_BHVR_SETCAM_UNLOCK,			// Forces the camera focus to the user's avatar
	// Camera (behaviours - deprecated)
	RLV_BHVR_CAMZOOMMIN,			// Enforces a minimum - vertical - FOV angle of 60° / multiplier
	RLV_BHVR_CAMZOOMMAX,			// Enforces a maximum - vertical - FOV angle of 60° / multiplier
	// Camera (reply)
	RLV_BHVR_GETCAM_AVDIST,			// Returns the current minimum distance between the camera and the user's avatar
	RLV_BHVR_GETCAM_AVDISTMIN,		// Returns the active (if any) minimum distance between the camera and the user's avatar
	RLV_BHVR_GETCAM_AVDISTMAX,		// Returns the active (if any) maxmimum distance between the camera and the user's avatar
	RLV_BHVR_GETCAM_FOV,			// Returns the current field of view angle (in radians)
	RLV_BHVR_GETCAM_FOVMIN,			// Returns the active (if any) minimum field of view angle (in radians)
	RLV_BHVR_GETCAM_FOVMAX,			// Enforces a maximum (if any) maximum field of view angle (in radians)
	RLV_BHVR_GETCAM_TEXTURES,		// Returns the active (if any) replace texture UUID
	// Camera (force)
	RLV_BHVR_SETCAM_MODE,			// Switch the user's camera into the specified mode (e.g. mouselook or thirdview)

	// Effects
	RLV_BHVR_SETSPHERE,				// Gives an object exclusive control of the 'vision spheres' effect
	RLV_BHVR_SETOVERLAY,			// Gives an object exclusive control of the overlay
	RLV_BHVR_SETOVERLAY_TWEEN,		// Animate between the current overlay settings and the supplied values

	RLV_BHVR_COUNT,
	RLV_BHVR_UNKNOWN
};

enum ERlvBehaviourModifier
{
	RLV_MODIFIER_FARTOUCHDIST,			// Radius of a sphere around the user in which they can interact with the world
	RLV_MODIFIER_RECVIMDISTMIN,			// Minimum distance to receive an IM from an otherwise restricted sender (squared value)
	RLV_MODIFIER_RECVIMDISTMAX,			// Maximum distance to receive an IM from an otherwise restricted sender (squared value)
	RLV_MODIFIER_SENDIMDISTMIN,			// Minimum distance to send an IM to an otherwise restricted recipient (squared value)
	RLV_MODIFIER_SENDIMDISTMAX,			// Maximum distance to send an IM to an otherwise restricted recipient (squared value)
	RLV_MODIFIER_STARTIMDISTMIN,		// Minimum distance to start an IM to an otherwise restricted recipient (squared value)
	RLV_MODIFIER_STARTIMDISTMAX,		// Maximum distance to start an IM to an otherwise restricted recipient (squared value)
	RLV_MODIFIER_SETCAM_AVDIST,			// Distance at which nearby avatars turn into a silhouette (normal value)
	RLV_MODIFIER_SETCAM_AVDISTMIN,		// Minimum distance between the camera position and the user's avatar (normal value)
	RLV_MODIFIER_SETCAM_AVDISTMAX,		// Maximum distance between the camera position and the user's avatar (normal value)
	RLV_MODIFIER_SETCAM_ORIGINDISTMIN,	// Minimum distance between the camera position and the origin point (normal value)
	RLV_MODIFIER_SETCAM_ORIGINDISTMAX,	// Maximum distance between the camera position and the origin point (normal value)
	RLV_MODIFIER_SETCAM_EYEOFFSET,		// Specifies the default camera's offset from the camera (vector)
	RLV_MODIFIER_SETCAM_EYEOFFSETSCALE,	// Specifies the default camera's offset scale (multiplier)
	RLV_MODIFIER_SETCAM_FOCUSOFFSET,	// Specifies the default camera's focus (vector)
	RLV_MODIFIER_SETCAM_FOVMIN,			// Minimum value for the camera's field of view (angle in radians)
	RLV_MODIFIER_SETCAM_FOVMAX,			// Maximum value for the camera's field of view (angle in radians)
	RLV_MODIFIER_SETCAM_TEXTURE,		// Specifies the UUID of the texture used to texture the world view
	RLV_MODIFIER_SHOWNAMETAGSDIST,		// Distance at which name tags will still be shown
	RLV_MODIFIER_SITTPDIST,
	RLV_MODIFIER_TPLOCALDIST,

	RLV_MODIFIER_COUNT,
	RLV_MODIFIER_UNKNOWN
};

enum class ERlvLocalBhvrModifier
{
	// @setoverlay
	OverlayAlpha,						// Transparency level of the overlay texture (in addition to the texture's own alpha channel)
	OverlayTexture,						// Specifies the UUID of the overlay texture
	OverlayTint,						// The tint that's applied to the overlay texture
	OverlayTouch,						// Determines whether the overlay texture's alpha channel will be used to allow/block world interaction
	// @setsphere
	SphereMode,                         // The type of effect that will apply to any pixel that intersects with the sphere (e.g. blend, blur, ...)
	SphereOrigin,                       // The origin of the sphere can either be the avatar or the camera position
	SphereColor,                        // [Blend only] Colour to mix with the actual pixel colour (stored as params)
	SphereParams,                       // Effect parameters (dependent on mode - see RlvSphereEffect)
	SphereDistMin,                      // Distance at which the effect starts and has weight minValue; e.g. for blend this would be colour = mix(colour, sphere_colour, min_alpha)
	SphereDistMax,                      // Distance at which the effect starts and has weight maxValue; e.g. for blend this would be colour = mix(colour, sphere_colour, max_alpha)
	SphereDistExtend,                   // Specifies the value beyond min dist or max dist (by default the sphere extends beyond max distance at max vlaue)
	SphereValueMin,                     // Value of the effect at minimum distance
	SphereValueMax,                     // Value of the effect at maximum distance
	SphereTween,                        // Amount of seconds it takes to lerp from value A to value B

	Unknown,
};

enum ERlvBehaviourOptionType
{
	RLV_OPTION_NONE,				// Behaviour takes no parameters
	RLV_OPTION_EXCEPTION,			// Behaviour requires an exception as a parameter
	RLV_OPTION_NONE_OR_EXCEPTION,	// Behaviour takes either no parameters or an exception
	RLV_OPTION_MODIFIER,			// Behaviour requires a modifier as a parameter
	RLV_OPTION_NONE_OR_MODIFIER		// Behaviour takes either no parameters or a modifier
};

enum ERlvParamType {
	RLV_TYPE_UNKNOWN = 0x00,
	RLV_TYPE_ADD     = 0x01,		// <param> == "n"|"add"
	RLV_TYPE_REMOVE  = 0x02,		// <param> == "y"|"rem"
	RLV_TYPE_FORCE   = 0x04,		// <param> == "force"
	RLV_TYPE_REPLY   = 0x08,		// <param> == <number>
	RLV_TYPE_CLEAR   = 0x10,
	RLV_TYPE_ADDREM  = RLV_TYPE_ADD | RLV_TYPE_REMOVE
};

enum ERlvCmdRet {
	RLV_RET_UNKNOWN		= 0x0000,	// Unknown error (should only be used internally)
	RLV_RET_RETAINED,				// Command was retained
	RLV_RET_SUCCESS		= 0x0100,	// Command executed succesfully
	RLV_RET_SUCCESS_UNSET,			// Command executed succesfully (RLV_TYPE_REMOVE for an unrestricted behaviour)
	RLV_RET_SUCCESS_DUPLICATE,		// Command executed succesfully (RLV_TYPE_ADD for an already restricted behaviour)
	RLV_RET_SUCCESS_DEPRECATED,		// Command executed succesfully but has been marked as deprecated
	RLV_RET_SUCCESS_DELAYED,		// Command parsed valid but will execute at a later time
	RLV_RET_FAILED		= 0x0200,	// Command failed (general failure)
	RLV_RET_FAILED_SYNTAX,			// Command failed (syntax error)
	RLV_RET_FAILED_OPTION,			// Command failed (invalid option)
	RLV_RET_FAILED_PARAM,			// Command failed (invalid param)
	RLV_RET_FAILED_LOCK,			// Command failed (command is locked by another object)
	RLV_RET_FAILED_DISABLED,		// Command failed (command disabled by user)
	RLV_RET_FAILED_UNKNOWN,			// Command failed (unknown command)
	RLV_RET_FAILED_NOSHAREDROOT,	// Command failed (missing #RLV)
	RLV_RET_FAILED_DEPRECATED,		// Command failed (deprecated and no longer supported)
	RLV_RET_FAILED_NOBEHAVIOUR,		// Command failed (force modifier on an object with no active restrictions)
	RLV_RET_FAILED_UNHELDBEHAVIOUR,	// Command failed (local modifier on an object that doesn't hold the base behaviour)
	RLV_RET_FAILED_BLOCKED,			// Command failed (object is blocked)
	RLV_RET_FAILED_THROTTLED,       // Command failed (throttled)
	RLV_RET_NO_PROCESSOR			// Command doesn't have a template processor define (legacy code)
};
#define RLV_RET_SUCCEEDED(eCmdRet)  (((eCmdRet) & RLV_RET_SUCCESS) == RLV_RET_SUCCESS)

enum class ERlvExceptionCheck
{
	Permissive,						// Exception can be set by any object
	Strict,							// Exception must be set by all objects holding the restriction
	Default,						// Permissive or strict will be determined by currently enforced restrictions
};

enum ERlvLockMask
{
	RLV_LOCK_NONE   = 0x00,
	RLV_LOCK_ADD    = 0x01,
	RLV_LOCK_REMOVE = 0x02,
	RLV_LOCK_ANY    = RLV_LOCK_ADD | RLV_LOCK_REMOVE
};

enum ERlvWearMask
{
	RLV_WEAR_LOCKED	 = 0x00,		// User can not wear the item at all
	RLV_WEAR_ADD	 = 0x01,		// User can wear the item in addition to what's already worn
	RLV_WEAR_REPLACE = 0x02,		// User can wear the item and replace what's currently worn
	RLV_WEAR		 = 0x03			// Convenience: combines RLV_WEAR_ADD and RLV_WEAR_REPLACE
};

enum ERlvAttachGroupType
{
	RLV_ATTACHGROUP_HEAD = 0,
	RLV_ATTACHGROUP_TORSO,
	RLV_ATTACHGROUP_ARMS,
	RLV_ATTACHGROUP_LEGS,
	RLV_ATTACHGROUP_HUD,
	RLV_ATTACHGROUP_COUNT,
	RLV_ATTACHGROUP_INVALID
};

// ============================================================================
// Settings
//

namespace RlvSettingNames
{
	using namespace std::string_view_literals;
	inline constexpr std::string_view Main = "RestrainedLove"sv;
	inline constexpr std::string_view Debug = "RestrainedLoveDebug"sv;
	inline constexpr std::string_view CanOoc = "RestrainedLoveCanOOC"sv;
	inline constexpr std::string_view ForbidGiveToRlv = "RestrainedLoveForbidGiveToRLV"sv;
	inline constexpr std::string_view NoSetEnv = "RestrainedLoveNoSetEnv"sv;
	inline constexpr std::string_view ShowEllipsis = "RestrainedLoveShowEllipsis"sv;
	inline constexpr std::string_view WearAddPrefix = "RestrainedLoveStackWhenFolderBeginsWith"sv;
	inline constexpr std::string_view WearReplacePrefix = "RestrainedLoveReplaceWhenFolderBeginsWith"sv;

	inline constexpr std::string_view DebugHideUnsetDup = "RLVaDebugHideUnsetDuplicate"sv;
	inline constexpr std::string_view EnableIMQuery = "RLVaEnableIMQuery"sv;
	inline constexpr std::string_view EnableLegacyNaming = "RLVaEnableLegacyNaming"sv;
	inline constexpr std::string_view EnableSharedWear = "RLVaEnableSharedWear"sv;
	inline constexpr std::string_view EnableTempAttach = "RLVaEnableTemporaryAttachments"sv;
	inline constexpr std::string_view HideLockedLayer = "RLVaHideLockedLayers"sv;
	inline constexpr std::string_view HideLockedAttach = "RLVaHideLockedAttachments"sv;
	inline constexpr std::string_view HideLockedInventory = "RLVaHideLockedInventory"sv;
	inline constexpr std::string_view LoginLastLocation = "RLVaLoginLastLocation"sv;
	inline constexpr std::string_view SharedInvAutoRename = "RLVaSharedInvAutoRename"sv;
	inline constexpr std::string_view ShowAssertionFail = "RLVaShowAssertionFailures"sv;
	inline constexpr std::string_view ShowRedirectChatTyping = "RLVaShowRedirectChatTyping"sv;
	inline constexpr std::string_view SplitRedirectChat = "RLVaSplitRedirectChat"sv;
	inline constexpr std::string_view TopLevelMenu = "RLVaTopLevelMenu"sv;
	inline constexpr std::string_view WearReplaceUnlocked = "RLVaWearReplaceUnlocked"sv;
}

// ============================================================================
// Strings (see rlva_strings.xml)
//

namespace RlvStringKeys
{
	namespace Blocked
	{
		using namespace std::string_view_literals;
		inline constexpr std::string_view AutoPilot = "blocked_autopilot"sv;
		inline constexpr std::string_view Generic = "blocked_generic"sv;
		inline constexpr std::string_view GroupChange = "blocked_groupchange"sv;
		inline constexpr std::string_view InvFolder = "blocked_invfolder"sv;
		inline constexpr std::string_view PermissionAttach = "blocked_permattach"sv;
		inline constexpr std::string_view PermissionTeleport = "blocked_permteleport"sv;
		inline constexpr std::string_view RecvIm = "blocked_recvim"sv;
		inline constexpr std::string_view RecvImRemote = "blocked_recvim_remote"sv;
		inline constexpr std::string_view SendIm = "blocked_sendim"sv;
		inline constexpr std::string_view Share = make_string_view("blocked_share");
		inline constexpr std::string_view ShareGeneric = make_string_view("blocked_share_generic");
		inline constexpr std::string_view StartConference = "blocked_startconf"sv;
		inline constexpr std::string_view StartIm = "blocked_startim"sv;
		inline constexpr std::string_view Teleport = "blocked_teleport"sv;
		inline constexpr std::string_view TeleportOffer = "blocked_teleport_offer"sv;
		inline constexpr std::string_view TpLureRequestRemote = "blocked_tplurerequest_remote"sv;
		inline constexpr std::string_view ViewXxx = "blocked_viewxxx"sv;
		inline constexpr std::string_view Wireframe = "blocked_wireframe"sv;
	}

	namespace Hidden
	{
		using namespace std::string_view_literals;
		inline constexpr std::string_view Generic = "hidden_generic"sv;
		inline constexpr std::string_view Parcel = "hidden_parcel"sv;
		inline constexpr std::string_view Region = "hidden_region"sv;
	}

	namespace StopIm
	{
		using namespace std::string_view_literals;
		inline constexpr std::string_view NoSession = "stopim_nosession"sv;
		inline constexpr std::string_view EndSessionRemote = "stopim_endsession_remote"sv;
		inline constexpr std::string_view EndSessionLocal = "stopim_endsession_local"sv;
	}
}

// ============================================================================
