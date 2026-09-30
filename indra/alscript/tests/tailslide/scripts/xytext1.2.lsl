////////////////////////////////////////////
// XyText v1.2 Script (5 Face, Single Texture)
//
// Written by Xylor Baysklef
//
// Modified by Kermitt Quirk 19/01/2006
// To add support for 5 face prim instead of 3
//
////////////////////////////////////////////

/////////////// CONSTANTS ///////////////////
// XyText Message Map.
integer DISPLAY_STRING      = 204000;
integer DISPLAY_EXTENDED    = 204001;
integer REMAP_INDICES       = 204002;
integer RESET_INDICES       = 204003;
integer SET_CELL_INFO       = 204004;
integer SET_FONT_TEXTURE    = 204005;
integer SET_THICKNESS       = 204006;
integer SET_COLOR           = 204007;

// This is an extended character escape sequence.
string  ESCAPE_SEQUENCE = "\\e";

// This is used to get an index for the extended character.
string  EXTENDED_INDEX  = "12345";

// Face numbers.
integer FACE_1          = 3;
integer FACE_2          = 7;
integer FACE_3          = 4;
integer FACE_4          = 6;
integer FACE_5          = 1;

// Used to hide the text after a fade-out.
key     TRANSPARENT     = "701917a8-d614-471f-13dd-5f4644e36e3c";
///////////// END CONSTANTS ////////////////

///////////// GLOBAL VARIABLES ///////////////
// This is the key of the font we are displaying.
key     gFontTexture        = "b2e7394f-5e54-aa12-6e1c-ef327b6bed9e";
// All displayable characters.  Default to ASCII order.
string gCharIndex;
// This is the channel to listen on while acting
// as a cell in a larger display.
integer gCellChannel        = -1;
// This is the starting character position in the cell channel message
// to render.
integer gCellCharPosition   = 0;
// This is whether or not to use the fade in/out special effect.
integer gCellUseFading      = FALSE;
// This is how long to display the text before fading out (if using
// fading special effect).
// Note: < 0  means don't fade out.
float   gCellHoldDelay      = 1.0;
/////////// END GLOBAL VARIABLES ////////////

ResetCharIndex() {
   gCharIndex  = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`";
   // \" <-- Fixes LSL syntax highlighting bug.
   gCharIndex += "abcdefghijklmnopqrstuvwxyz{|}~";
   gCharIndex += "\n\n\n\n\n";
}

vector GetGridOffset(integer index) {
   // Calculate the offset needed to display this character.
   integer Row = index / 10;
   integer Col = index % 10;

   // Return the offset in the texture.
   return <-0.45 + 0.1 * Col, 0.45 - 0.1 * Row, 0.0>;
}

ShowChars(vector grid_offset1, vector grid_offset2, vector grid_offset3, vector grid_offset4, vector grid_offset5) {
   // Set the primitive textures directly.
   
   // <-0.256, 0, 0>
   // <0, 0, 0>
   // <0.130, 0, 0>
   // <0, 0, 0>
   // <-0.74, 0, 0>
   
   llSetPrimitiveParams( [
        PRIM_TEXTURE, FACE_1, (string)gFontTexture, <0.12, 0.1, 0>, grid_offset1 + <0.037, 0, 0>, 0.0,
        PRIM_TEXTURE, FACE_2, (string)gFontTexture, <0.05, 0.1, 0>, grid_offset2, 0.0,
        PRIM_TEXTURE, FACE_3, (string)gFontTexture, <-0.74, 0.1, 0>, grid_offset3 - <0.244, 0, 0>, 0.0,
        PRIM_TEXTURE, FACE_4, (string)gFontTexture, <0.05, 0.1, 0>, grid_offset4, 0.0,
        PRIM_TEXTURE, FACE_5, (string)gFontTexture, <0.12, 0.1, 0>, grid_offset5 - <0.037, 0, 0>, 0.0
        ]);
}

RenderString(string str) {
   // Get the grid positions for each pair of characters.
   vector GridOffset1 = GetGridOffset( llSubStringIndex(gCharIndex, llGetSubString(str, 0, 0)) );
   vector GridOffset2 = GetGridOffset( llSubStringIndex(gCharIndex, llGetSubString(str, 1, 1)) );
   vector GridOffset3 = GetGridOffset( llSubStringIndex(gCharIndex, llGetSubString(str, 2, 2)) );
   vector GridOffset4 = GetGridOffset( llSubStringIndex(gCharIndex, llGetSubString(str, 3, 3)) );
   vector GridOffset5 = GetGridOffset( llSubStringIndex(gCharIndex, llGetSubString(str, 4, 4)) );

   // Use these grid positions to display the correct textures/offsets.
   ShowChars(GridOffset1, GridOffset2, GridOffset3, GridOffset4, GridOffset5);
}

RenderWithEffects(string str) {
   // Get the grid positions for each pair of characters.
   vector GridOffset1 = GetGridOffset( llSubStringIndex(gCharIndex, llGetSubString(str, 0, 0)) );
   vector GridOffset2 = GetGridOffset( llSubStringIndex(gCharIndex, llGetSubString(str, 1, 1)) );
   vector GridOffset3 = GetGridOffset( llSubStringIndex(gCharIndex, llGetSubString(str, 2, 2)) );
   vector GridOffset4 = GetGridOffset( llSubStringIndex(gCharIndex, llGetSubString(str, 3, 3)) );
   vector GridOffset5 = GetGridOffset( llSubStringIndex(gCharIndex, llGetSubString(str, 4, 4)) );

   // First set the alpha to the lowest possible.
   llSetAlpha(0.05, ALL_SIDES);

   // Use these grid positions to display the correct textures/offsets.
   ShowChars(GridOffset1, GridOffset2, GridOffset3, GridOffset4, GridOffset5);          // Now turn up the alpha until it is at full strength.
    float Alpha;
    for (Alpha = 0.10; Alpha <= 1.0; Alpha += 0.05)
       llSetAlpha(Alpha, ALL_SIDES);
          // See if we want to fade out as well.
   if (gCellHoldDelay < 0.0)
       // No, bail out. (Just keep showing the string at full strength).
       return;
          // Hold the text for a while.
   llSleep(gCellHoldDelay);
      // Now fade out.
   for (Alpha = 0.95; Alpha >= 0.05; Alpha -= 0.05)
       llSetAlpha(Alpha, ALL_SIDES);
          // Make the text transparent to fully hide it.
   llSetTexture(TRANSPARENT, ALL_SIDES);
}

RenderExtended(string str) {
   // Look for escape sequences.
   list Parsed       = llParseString2List(str, [], [ESCAPE_SEQUENCE]);
   integer ParsedLen = llGetListLength(Parsed);

   // Create a list of index values to work with.
   list Indices;
   // We start with room for 5 indices.
   integer IndicesLeft = 5;

   integer i;
   string Token;
   integer Clipped;
   integer LastWasEscapeSequence = FALSE;
   // Work from left to right.
   for (i = 0; i < ParsedLen && IndicesLeft > 0; i++) {
       Token = llList2String(Parsed, i);

       // If this is an escape sequence, just set the flag and move on.
       if (Token == ESCAPE_SEQUENCE) {
           LastWasEscapeSequence = TRUE;
       }
       else { // Token != ESCAPE_SEQUENCE
           // Otherwise this is a normal token.  Check its length.
           Clipped = FALSE;
           integer TokenLength = llStringLength(Token);
           // Clip if necessary.
           if (TokenLength > IndicesLeft) {
               Token = llGetSubString(Token, 0, IndicesLeft - 1);
               TokenLength = llStringLength(Token);
               IndicesLeft = 0;
               Clipped = TRUE;
           }
           else
               IndicesLeft -= TokenLength;

           // Was the previous token an escape sequence?
           if (LastWasEscapeSequence) {
               // Yes, the first character is an escape character, the rest are normal.

               // This is the extended character.
               Indices += [llSubStringIndex(EXTENDED_INDEX, llGetSubString(Token, 0, 0)) + 95];

               // These are the normal characters.
               integer j;
               for (j = 1; j < TokenLength; j++)
                   Indices += [llSubStringIndex(gCharIndex, llGetSubString(Token, j, j))];
           }
           else { // Normal string.
               // Just add the characters normally.
               integer j;
               for (j = 0; j < TokenLength; j++)
                   Indices += [llSubStringIndex(gCharIndex, llGetSubString(Token, j, j))];
           }

           // Unset this flag, since this was not an escape sequence.
           LastWasEscapeSequence = FALSE;
       }
   }

   // Use the indices to create grid positions.
   vector GridOffset1 = GetGridOffset( llList2Integer(Indices, 0));
   vector GridOffset2 = GetGridOffset( llList2Integer(Indices, 1) );
   vector GridOffset3 = GetGridOffset( llList2Integer(Indices, 2) );
   vector GridOffset4 = GetGridOffset( llList2Integer(Indices, 3) );
   vector GridOffset5 = GetGridOffset( llList2Integer(Indices, 4) );

   // Use these grid positions to display the correct textures/offsets.
   ShowChars(GridOffset1, GridOffset2, GridOffset3, GridOffset4, GridOffset5);
}

integer ConvertIndex(integer index) {
   // This converts from an ASCII based index to our indexing scheme.
   if (index >= 32) // ' ' or higher
       index -= 32;
   else { // index < 32
       // Quick bounds check.
       if (index > 15)
           index = 15;

       index += 94; // extended characters
   }

   return index;
}

default {
   state_entry() {
       // Initialize the character index.
       ResetCharIndex();
   }

   link_message(integer sender, integer channel, string data, key id) {
       if (channel == DISPLAY_STRING) {
           RenderString(data);
           return;
       }
       if (channel == DISPLAY_EXTENDED) {
           RenderExtended(data);
           return;
       }
       if (channel == gCellChannel) {
           // Extract the characters we are interested in, and use those to render.
           string TextToRender = llGetSubString(data, gCellCharPosition, gCellCharPosition + 4);
                      // See if we need to show special effects.
           if (gCellUseFading)
               RenderWithEffects( TextToRender );
           else // !gCellUseFading
               RenderString( TextToRender );
           return;
       }
       if (channel == REMAP_INDICES) {
           // Parse the message, splitting it up into index values.
           list Parsed = llCSV2List(data);
           integer i;
           // Go through the list and swap each pair of indices.
           for (i = 0; i < llGetListLength(Parsed); i += 2) {
               integer Index1 = ConvertIndex( llList2Integer(Parsed, i) );
               integer Index2 = ConvertIndex( llList2Integer(Parsed, i + 1) );

               // Swap these index values.
               string Value1 = llGetSubString(gCharIndex, Index1, Index1);
               string Value2 = llGetSubString(gCharIndex, Index2, Index2);

               gCharIndex = llDeleteSubString(gCharIndex, Index1, Index1);
               gCharIndex = llInsertString(gCharIndex, Index1, Value2);

               gCharIndex = llDeleteSubString(gCharIndex, Index2, Index2);
               gCharIndex = llInsertString(gCharIndex, Index2, Value1);
           }
           return;
       }
       if (channel == RESET_INDICES) {
           // Restore the character index back to default settings.
           ResetCharIndex();
           return;
       }
       if (channel == SET_CELL_INFO) {
           // Change the channel we listen to for cell commands, the
           // starting character position to extract from, and
           // special effect attributes.
           list Parsed = llCSV2List(data);
           gCellChannel        = (integer) llList2String(Parsed, 0);
           gCellCharPosition   = (integer) llList2String(Parsed, 1);
           gCellUseFading      = (integer) llList2String(Parsed, 2);
           gCellHoldDelay      = (float)   llList2String(Parsed, 3);
           return;
       }
       if (channel == SET_FONT_TEXTURE) {
           // Use the new texture instead of the current one.
           gFontTexture = id;
           // Change the currently shown texture.
           llSetTexture(gFontTexture, FACE_1);
           llSetTexture(gFontTexture, FACE_2);
           llSetTexture(gFontTexture, FACE_3);
           llSetTexture(gFontTexture, FACE_4);
           llSetTexture(gFontTexture, FACE_5);
           return;
       }
       if (channel == SET_THICKNESS) {
           // Set our z scale to thickness, while staying fixed
           // in position relative the prim below us.
           vector Scale    = llGetScale();
           float Thickness = (float) data;
                      // Reposition only if this isn't the root prim.
           integer ThisLink = llGetLinkNumber();
           if (ThisLink != 0 || ThisLink != 1) {
               // This is not the root prim.
               vector Up = llRot2Up(llGetLocalRot());
               float DistanceToMove = Thickness / 2.0 - Scale.z / 2.0;
               vector Pos = llGetLocalPos();
               llSetPos(Pos + DistanceToMove * Up);
           }
                      // Apply the new thickness.
           Scale.z = Thickness;
           llSetScale(Scale);
           return;
       }
       if (channel == SET_COLOR) {
           vector newColor = (vector)data;
           llSetColor(newColor, ALL_SIDES);
       }
   }
} 

