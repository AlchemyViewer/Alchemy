// NexiiLSL's texture.lsl (include/NexiiLSL/LICENSE): macros whose bodies
// run over several lines, a backslash straight after a comma or an open
// bracket, calls to another of them inside a body over several lines, a
// cast in a body, numbers written .5 and 2., and a name a body takes from
// where it is used (`digit`) rather than from its parameters.
#include "NexiiLSL/texture.lsl"

default
{
    state_entry()
    {
        integer digit = 3;
        float width = 10;
        float height = 20;
        llSetLinkPrimitiveParamsFast(LINK_THIS, [PRIM_TEXTURE, 0, TEXTURE_CURSORS, CURSOR(1, 2), 0]);
        list repeat = [TEXTURE_REPEAT(1, 2, 64, 128)];
        vector offset = TEXTURE_OFFSET(1, 2, 64, 128);
        list coords = [TEXTURE_COORDS(1, 2, 3, 4, 64, 128)];
        list rect = [TEXTURE_RECT(1, 2, 3, 4, 64, 128)];
        llSetLinkPrimitiveParamsFast(LINK_THIS, [PRIM_TEXTURE, 1, TEXTURE_CURSORS, SPRITESHEET(5, 64, 1024), 0]);
        llSetLinkPrimitiveParamsFast(LINK_THIS, [PRIM_TEXTURE, 2, TEXTURE_CURSORS, SPRITESHEET2(5, 32, 16, 512, 256), 0]);
    }
}
