// This is the heart of managing texture coordinates, by converting pixel coordinates into SL texture uv coords
#define TEXTURE_REPEAT(w, h, textureWidth, textureHeight) <width / float(textureWidth), height / float(textureHeight), 0>
#define TEXTURE_OFFSET(x, y, textureWidth, textureHeight) \
    <((x) - float(textureWidth)/2) / float(textureWidth),\
    -((y) - float(textureHeight)/2) / float(textureHeight), 0>
#define TEXTURE_COORDS(x, y, width, height, textureWidth, textureHeight) \
    <width / float(textureWidth),\
     height / float(textureHeight), 0>,\
    <((x) - float(textureWidth)/2.) / float(textureWidth),\
    -((y) - float(textureHeight)/2.) / float(textureHeight), 0>
#define TEXTURE_RECT(x, y, width, height, textureWidth, textureHeight) \
    <width / float(textureWidth),\
     height / float(textureHeight), 0>,\
    <((x) + width/2. - float(textureWidth)/2.) / float(textureWidth),\
    -((y) + height/2. - float(textureHeight)/2.) / float(textureHeight), 0>

// For rendering icons placed neatly into an even grid layout in a square texture resolution
#define SPRITESHEET(icon, size, resolution) \
    TEXTURE_COORDS(\
        size*.5 + size * icon % (resolution/size),\
        size*.5 - size * icon / llFloor(resolution/size),\
        size, size,\
        resolution, resolution\
    )

// For non-uniform grid layouts as well as textures with non-square aspect ratio
#define SPRITESHEET2(icon, iconWidth, iconHeight, textureWidth, textureHeight) \
    TEXTURE_COORDS(\
        iconWidth*.5 + iconWidth * digit % llFloor(textureWidth/iconWidth),\
        iconHeight*.5 - iconHeight * digit / llFloor(textureWidth/iconWidth),\
        iconWidth, iconHeight,\
        textureWidth, textureHeight\
    )


// Compact spritesheet functions by hardcoding resolution and icon sizes
#define TEXTURE_CURSORS "18dcdc40-eb58-c42f-b5ef-5e37d7f5f8bd"
#define CURSOR(a, b) <256/2048., 256/512., 0>, <(128 + (256*a) - 1024)/2048., (256 - 128 + (256*b))/512., 0>
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