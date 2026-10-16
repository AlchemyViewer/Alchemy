// NexiiLSL's library for packing strings small (include/NexiiLSL/
// LICENSE), string-compression.lsl -- its name kept off the first line,
// which would ask the golden run for compression: an include of an
// include by the path the repository gives it, #defines
// inside blocks, a name used before the #define that makes it a macro
// (`RANGE`, left as it is, as C leaves it), and a cast with its line's
// brackets unbalanced around it. Its constants it does not use itself are
// used in a state after it.
#include "NexiiLSL/string-compression.lsl"

state limits
{
    state_entry()
    {
        list limits = [MAXIMUM, UTF8_1B_MAX, UTF8_2B_MAX, UTF8_3B_MAX, UTF8_4B_MAX,
            UTF8_SURROGATE_START, UTF8_SURROGATE_END, UTF8_NONCHARACTER_1, UTF8_NONCHARACTER_2];
    }
}
