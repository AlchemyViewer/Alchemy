// NexiiLSL's library for packing strings small (include/NexiiLSL/
// LICENSE), string-compression.lsl -- its name kept off the first line,
// which would ask the golden run for compression: an include of an
// include by the path the repository gives it, #defines
// inside blocks, a name used before the #define that makes it a macro
// (`RANGE`, left as it is, as C leaves it), and a cast with its line's
// brackets unbalanced around it. Its constants it does not use itself are
// used in a state after it.





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








// Available bytes for UTF8 packing:










default
{
    state_entry()
    {
        //////////////////////////////////////////////////////////////////////////////////////////////////////
        /// Compact unsigned integer (0 to UTF8_4B_MAX-1)
        // Can save a byte or two but importantly provides array-like access
        {
            integer input = 0x10FFFF - 1;
            
            string compressed = llChar(1 + input);
            
            integer output = llOrd(compressed, 0) - 1;
            
            llOwnerSay(
                "Compact int: " + (string)input + " => " + compressed + " => " + (string)output + "\n" +
                "Compression " + (string)stringBytes((string)input) + "b => " + (string)stringBytes(compressed) + "b"
            );
        }
        
        //////////////////////////////////////////////////////////////////////////////////////////////////////
        /// Compact unsigned float
        {

            float quantizer = (0x7F - 1) / RANGE;
            
            float input = 1337.256;
            
            string compressed = llChar(1 + ((integer)(input * quantizer));
            
            float output = (llOrd(compressed, 0) - 1) / quantizer;
        }
        
        //////////////////////////////////////////////////////////////////////////////////////////////////////
        /// Compact signed float
        {

            float quantizer = (0x7F - 1) / 2048.0 / 2;
            
            float input = 1337.256;
            
            string compressed = llChar(1 + ((integer)(quantizer + input * quantizer));
            
            float output = ((llOrd(compressed, 0) - 1) - quantizer) / quantizer;
        }
        
        //////////////////////////////////////////////////////////////////////////////////////////////////////
        /// Compact vector (region coords <0,0,0> to <256,256,4096>)
        {
            // Quantization from 0-4096 / 0-256
            float xyquant = (0x7F - 1) / 4096.;
            float zquant = (0x7F - 1) / 256.;
            vector a = <250.525, 128.128128, 3059.999>;
            
            // Compress
            string b = llChar(1 + ((integer)(a.x * xyquant)))
                     + llChar(1 + ((integer)(a.y * xyquant)))
                     + llChar(1 + ((integer)(a.z * zquant)));
            
            // Decompress
            vector c = <
                (llOrd(b, 0) - 1) / xyquant,
                (llOrd(b, 1) - 1) / xyquant,
                (llOrd(b, 2) - 1) / zquant
            >;
        }
        //////////////////////////////////////////////////////////////////////////////////////////////////////
        /// Compact keys (36 -> 8 chars); Keys are really 16 byte values, so we can pack them tightly
        // There is probably a better way to do this to fit into the 4 byte UTF-8 chars and deserialise it inline
        {
            key a = "64064c89-25aa-008b-cfe7-0c34e28ae523";
            
            // Compress
            string b = llChar(((integer)("0x" + llGetSubString(a, 0, 3))))
                     + llChar(((integer)("0x" + llGetSubString(a, 4, 7))))
                     + llChar(((integer)("0x" + llGetSubString(a, 9, 12))))
                     + llChar(((integer)("0x" + llGetSubString(a, 14, 17))))
                     + llChar(((integer)("0x" + llGetSubString(a, 19, 22))))
                     + llChar(((integer)("0x" + llGetSubString(a, 24, 27))))
                     + llChar(((integer)("0x" + llGetSubString(a, 28, 31))))
                     + llChar(((integer)("0x" + llGetSubString(a, 32, 35))));
            
            // Decompress
            key c = bits2nybbles(llOrd(b, 0)) + bits2nybbles(llOrd(b, 1))
                  + "-" + bits2nybbles(llOrd(b, 2)) + "-" + bits2nybbles(llOrd(b, 3))
                  + "-" + bits2nybbles(llOrd(b, 4)) + "-" + bits2nybbles(llOrd(b, 5))
                  + bits2nybbles(llOrd(b, 6)) + bits2nybbles(llOrd(b, 7));
            
            llOwnerSay(
                "Compact Key (36 -> 8): " + (string)a + " => '" + b + "' => " + (string)c + "\n" +
                "Compression " + (string)stringBytes(a) + "b => " + (string)stringBytes(b) + "b"
            );
        }
        
        //////////////////////////////////////////////////////////////////////////////////////////////////////
        // Compact rotations (<-1,-1,-1,-1> to <1,1,1,1>)
        {
            float quantizer = 0x10FFFF / 2;
            vector a = <-2, 27, 120>;
            rotation b = llEuler2Rot(a * DEG_TO_RAD);
            
            // Compress
            string c = llChar(1 + ((integer)(quantizer + b.x * quantizer)))
                     + llChar(1 + ((integer)(quantizer + b.y * quantizer)))
                     + llChar(1 + ((integer)(quantizer + b.z * quantizer)))
                     + llChar(1 + ((integer)(quantizer + b.s * quantizer)));
            
            // Decompress
            rotation d = <
                ((llOrd(c, 0) - 1) - quantizer) / quantizer,
                ((llOrd(c, 1) - 1) - quantizer) / quantizer,
                ((llOrd(c, 2) - 1) - quantizer) / quantizer,
                ((llOrd(c, 3) - 1) - quantizer) / quantizer
            >;
            
            vector e = llRot2Euler(d) * RAD_TO_DEG;
            
            llOwnerSay(
                "Compact rot: " + (string)a + " => " + (string)b + " => '" + c + "' => " + (string)d + " => " + (string)e + "\n" +
                "Compression " + (string)stringBytes(llList2CSV([a,b])) + "b => " + (string)stringBytes(c) + "b"
            );
        }
        
        //////////////////////////////////////////////////////////////////////////////////////////////////////
        
        
        
    }
}
state limits
{
    state_entry()
    {
        list limits = [2048.0, 0x7F, 0x07FF, 0xFFFF, 0x10FFFF,
            0xD800, 0xDFFF, 0xFFFE, 0xFFFF];
    }
}
