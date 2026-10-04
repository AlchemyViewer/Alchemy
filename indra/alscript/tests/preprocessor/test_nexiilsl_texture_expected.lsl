// NexiiLSL's texture.lsl (include/NexiiLSL/LICENSE): macros whose bodies
// run over several lines, a backslash straight after a comma or an open
// bracket, calls to another of them inside a body over several lines, a
// cast in a body, numbers written .5 and 2., and a name a body takes from
// where it is used (`digit`) rather than from its parameters.

// This is the heart of managing texture coordinates, by converting pixel coordinates into SL texture uv coords















// For rendering icons placed neatly into an even grid layout in a square texture resolution








// For non-uniform grid layouts as well as textures with non-square aspect ratio









// Compact spritesheet functions by hardcoding resolution and icon sizes


// equivalent to #define CURSOR(a, b) TEXTURE_REPEAT(256, 256, 2048., 512.), TEXTURE_OFFSET(, , 2048., 512.)



// Old macros moved from utilities.lsl
// #define TEXTURE_SCALE(w, h) <w / RESOLUTION, h / RESOLUTION, 0>
// #define TEXTURE_OFFSET(w, h, x, y) <(w*.5 + x - RESOLUTION*.5) / RESOLUTION, (RESOLUTION*.5 - (h*.5 + y)) / RESOLUTION, 0>
// #define TEXTURE_SIZE(w, h) w / RESOLUTION*.5, h / RESOLUTION*.5


/*
Texture maths, pixels to coords

default
{
    state_entry()
    {
        #define RESOLUTION 1024.
        #define FACE 4
        
        float width = 181.5;
        float height = 116.5;
        float x = 30;
        float y = 608;
        
        llScaleTexture(
            width / RESOLUTION,
            height / RESOLUTION,
            FACE
        );
        llOffsetTexture(
            (width*.5 + x - RESOLUTION*.5) / RESOLUTION,
            (RESOLUTION*.5 - (height*.5 + y)) / RESOLUTION,
            FACE
        );
        llSetScale(<
            0.04,
            width/RESOLUTION*.5,
            height/RESOLUTION*.5
        >);
    }
}

*/
default
{
    state_entry()
    {
        integer digit = 3;
        float width = 10;
        float height = 20;
        llSetLinkPrimitiveParamsFast(LINK_THIS, [PRIM_TEXTURE, 0, "18dcdc40-eb58-c42f-b5ef-5e37d7f5f8bd", <256/2048., 256/512., 0>, <(128 + (256*1) - 1024)/2048., (256 - 128 + (256*2))/512., 0>, 0]);
        list repeat = [<width / ((float)(64)), height / ((float)(128)), 0>];
        vector offset = <((1) - ((float)(64))/2) / ((float)(64)), -((2) - ((float)(128))/2) / ((float)(128)), 0>;
        list coords = [<3 / ((float)(64)), 4 / ((float)(128)), 0>, <((1) - ((float)(64))/2.) / ((float)(64)), -((2) - ((float)(128))/2.) / ((float)(128)), 0>];
        list rect = [<3 / ((float)(64)), 4 / ((float)(128)), 0>, <((1) + 3/2. - ((float)(64))/2.) / ((float)(64)), -((2) + 4/2. - ((float)(128))/2.) / ((float)(128)), 0>];
        llSetLinkPrimitiveParamsFast(LINK_THIS, [PRIM_TEXTURE, 1, "18dcdc40-eb58-c42f-b5ef-5e37d7f5f8bd", <64 / ((float)(1024)), 64 / ((float)(1024)), 0>, <((64*.5 + 64 * 5 % (1024/64)) - ((float)(1024))/2.) / ((float)(1024)), -((64*.5 - 64 * 5 / llFloor(1024/64)) - ((float)(1024))/2.) / ((float)(1024)), 0>, 0]);
        llSetLinkPrimitiveParamsFast(LINK_THIS, [PRIM_TEXTURE, 2, "18dcdc40-eb58-c42f-b5ef-5e37d7f5f8bd", <32 / ((float)(512)), 16 / ((float)(256)), 0>, <((32*.5 + 32 * digit % llFloor(512/32)) - ((float)(512))/2.) / ((float)(512)), -((16*.5 - 16 * digit / llFloor(512/32)) - ((float)(256))/2.) / ((float)(256)), 0>, 0]);
    }
}
