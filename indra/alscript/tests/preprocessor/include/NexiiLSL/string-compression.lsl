#include "NexiiLSL/utilities.lsl"

// Available bytes for UTF8 packing:
#define UTF8_1B_MAX 0x7F
#define UTF8_2B_MAX 0x07FF
#define UTF8_3B_MAX 0xFFFF
#define UTF8_4B_MAX 0x10FFFF
#define UTF8_SURROGATE_START 0xD800
#define UTF8_SURROGATE_END 0xDFFF
#define UTF8_NONCHARACTER_1 0xFFFE
#define UTF8_NONCHARACTER_2 0xFFFF


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
            #define MAXIMUM 2048.0
            float quantizer = (UTF8_1B_MAX - 1) / RANGE;
            
            float input = 1337.256;
            
            string compressed = llChar(1 + integer(input * quantizer);
            
            float output = (llOrd(compressed, 0) - 1) / quantizer;
        }
        
        //////////////////////////////////////////////////////////////////////////////////////////////////////
        /// Compact signed float
        {
            #define RANGE 2048.0
            float quantizer = (UTF8_1B_MAX - 1) / RANGE / 2;
            
            float input = 1337.256;
            
            string compressed = llChar(1 + integer(quantizer + input * quantizer);
            
            float output = ((llOrd(compressed, 0) - 1) - quantizer) / quantizer;
        }
        
        //////////////////////////////////////////////////////////////////////////////////////////////////////
        /// Compact vector (region coords <0,0,0> to <256,256,4096>)
        {
            // Quantization from 0-4096 / 0-256
            float xyquant = (UTF8_1B_MAX - 1) / 4096.;
            float zquant = (UTF8_1B_MAX - 1) / 256.;
            vector a = <250.525, 128.128128, 3059.999>;
            
            // Compress
            string b = llChar(1 + integer(a.x * xyquant))
                     + llChar(1 + integer(a.y * xyquant))
                     + llChar(1 + integer(a.z * zquant));
            
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
            string b = llChar(integer("0x" + llGetSubString(a, 0, 3)))
                     + llChar(integer("0x" + llGetSubString(a, 4, 7)))
                     + llChar(integer("0x" + llGetSubString(a, 9, 12)))
                     + llChar(integer("0x" + llGetSubString(a, 14, 17)))
                     + llChar(integer("0x" + llGetSubString(a, 19, 22)))
                     + llChar(integer("0x" + llGetSubString(a, 24, 27)))
                     + llChar(integer("0x" + llGetSubString(a, 28, 31)))
                     + llChar(integer("0x" + llGetSubString(a, 32, 35)));
            
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
            float quantizer = UTF8_4B_MAX / 2;
            vector a = <-2, 27, 120>;
            rotation b = llEuler2Rot(a * DEG_TO_RAD);
            
            // Compress
            string c = llChar(1 + integer(quantizer + b.x * quantizer))
                     + llChar(1 + integer(quantizer + b.y * quantizer))
                     + llChar(1 + integer(quantizer + b.z * quantizer))
                     + llChar(1 + integer(quantizer + b.s * quantizer));
            
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