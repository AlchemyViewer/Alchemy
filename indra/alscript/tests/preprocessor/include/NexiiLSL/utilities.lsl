#define isSameOwner(identifier) llGetOwnerKey(identifier) == llGetOwner()
#define notSameOwner(identifier) llGetOwnerKey(identifier) != llGetOwner()

// For displaying ordinal numbers like 1st, 2nd, 3rd, 20th, 21st, 22nd, etc.
string nth(integer value)
{
    value = value % 10;
    if(value == 1) return "st";
    if(value == 2) return "nd";
    if(value == 3) return "rd";
    return "th";
}

// Returns the number of bytes a string would take up in UTF-8 encoding
integer stringBytes(string str) {
    integer bytes;
    integer length = llStringLength(str);
    integer iterator;
    for(; iterator < length; ++iterator)
    {
        integer byte = llOrd(str, iterator);
        if(byte <= 0x7F) bytes += 1;
        else if(byte <= 0x07FF) bytes += 2;
        else if(byte <= 0xFFFF) bytes += 3;
        else bytes += 4;
    }
    return bytes;
}

// Significantly faster
// From https://wiki.secondlife.com/wiki/LlStringToBase64
integer getStringBytes(string msg) {
    return (llStringLength((string)llParseString2List(llStringToBase64(msg), ["="], [])) * 3) >> 2;
}


// From https://wiki.secondlife.com/wiki/Efficient_Hex
string bits2nybbles(integer bits) {
    integer lsn; // least significant nybble
    string nybbles = "";
    do
        nybbles = llGetSubString("0123456789abcdef", lsn = (bits & 0xF), lsn) + nybbles;
    while (bits = (0xfffFFFF & (bits >> 4)));
    return nybbles;
}


string AttachmentPointAsName(integer point)
{
    if(point == ATTACH_HEAD) return "Skull";
    if(point == ATTACH_NOSE) return "Nose";
    if(point == ATTACH_MOUTH) return "Mouth";
    if(point == ATTACH_FACE_TONGUE) return "Tongue";
    if(point == ATTACH_CHIN) return "Chin";
    if(point == ATTACH_FACE_JAW) return "Jaw";
    if(point == ATTACH_LEAR) return "Left Ear";
    if(point == ATTACH_REAR) return "Right Ear";
    if(point == ATTACH_FACE_LEAR) return "Alt Left Ear";
    if(point == ATTACH_FACE_REAR) return "Alt Right Ear";
    if(point == ATTACH_LEYE) return "Left Eye";
    if(point == ATTACH_REYE) return "Right Eye";
    if(point == ATTACH_FACE_LEYE) return "Alt Left Eye";
    if(point == ATTACH_FACE_REYE) return "Alt Right Eye";
    if(point == ATTACH_NECK) return "Neck";
    if(point == ATTACH_LSHOULDER) return "Left Shoulder";
    if(point == ATTACH_RSHOULDER) return "Right Shoulder";
    if(point == ATTACH_LUARM) return "L Upper Arm";
    if(point == ATTACH_RUARM) return "R Upper Arm";
    if(point == ATTACH_LLARM) return "L Lower Arm";
    if(point == ATTACH_RLARM) return "R Lower Arm";
    if(point == ATTACH_LHAND) return "Left Hand";
    if(point == ATTACH_RHAND) return "Right Hand";
    if(point == ATTACH_LHAND_RING1) return "Left Ring Finger";
    if(point == ATTACH_RHAND_RING1) return "Right Ring Finger";
    if(point == ATTACH_LWING) return "Left Wing";
    if(point == ATTACH_RWING) return "Right Wing";
    if(point == ATTACH_CHEST) return "Chest";
    if(point == ATTACH_LEFT_PEC) return "Left Pec";
    if(point == ATTACH_RIGHT_PEC) return "Right Pec";
    if(point == ATTACH_BELLY) return "Stomach";
    if(point == ATTACH_BACK) return "Spine";
    if(point == ATTACH_TAIL_BASE) return "Tail Base";
    if(point == ATTACH_TAIL_TIP) return "Tail Tip";
    if(point == ATTACH_AVATAR_CENTER) return "Avatar Center";
    if(point == ATTACH_PELVIS) return "Pelvis";
    if(point == ATTACH_GROIN) return "Groin";
    if(point == ATTACH_LHIP) return "Left Hip";
    if(point == ATTACH_RHIP) return "Right Hip";
    if(point == ATTACH_LULEG) return "L Upper Leg";
    if(point == ATTACH_RULEG) return "R Upper Leg";
    if(point == ATTACH_RLLEG) return "R Lower Leg";
    if(point == ATTACH_LLLEG) return "L Lower Leg";
    if(point == ATTACH_LFOOT) return "Left Foot";
    if(point == ATTACH_RFOOT) return "Right Foot";
    if(point == ATTACH_HIND_LFOOT) return "Left Hind Foot";
    if(point == ATTACH_HIND_RFOOT) return "Right Hind Foot";
    if(point == ATTACH_HUD_CENTER_2) return "HUD Center 2";
    if(point == ATTACH_HUD_TOP_RIGHT) return "HUD Top Right";
    if(point == ATTACH_HUD_TOP_CENTER) return "HUD Top";
    if(point == ATTACH_HUD_TOP_LEFT) return "HUD Top Left";
    if(point == ATTACH_HUD_CENTER_1) return "HUD Center";
    if(point == ATTACH_HUD_BOTTOM_LEFT) return "HUD Bottom Left";
    if(point == ATTACH_HUD_BOTTOM) return "HUD Bottom";
    if(point == ATTACH_HUD_BOTTOM_RIGHT) return "HUD Bottom Right";
    return (string)point;
}







