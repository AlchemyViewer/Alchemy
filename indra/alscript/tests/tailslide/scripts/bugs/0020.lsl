// Reported By: strife
// Date: 2007-04-02
// Version: 0.2.7
// Error: llStringTrim, STRING_TRIM*, llSetLinkTexture, llSetLinkPrimitiveParams

default {
    state_entry() {
        llSetLinkPrimitiveParams( LINK_SET, [PRIM_TYPE, "woop woop"] );
        llSetLinkTexture( LINK_SET, NULL_KEY, ALL_SIDES );
        llStringTrim( " hello ", STRING_TRIM_HEAD );
        llStringTrim( " hello ", STRING_TRIM_TAIL );
        llStringTrim( " hello ", STRING_TRIM );
    }
}
