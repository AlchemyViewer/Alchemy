// Reported By: Lazink Maeterlinck, Kermitt Quirk, Natalie Weeks, Trevor Langdon
// Date: 2006-06-21
// Version: 0.2.5
// Error: Missing constants/functions

default {
    state_entry() {
        llSin(1.0);                   // Lazink
        integer a = PRIM_POINT_LIGHT; // Kermitt
        llListSort([], 1, 2);         // Natalie, Trevor
        a = HTTP_VERIFY_CERT;         // 1.10.4
        a = HTTP_BODY_MAXLENGTH;      // masa
        a = PRIM_FLEXIBLE;
        a = PRIM_TEXGEN;
        a = PRIM_TEXGEN_DEFAULT;
        a = PRIM_TEXGEN_PLANAR;
        a = PARCEL_FLAG_ALLOW_FLY | PARCEL_FLAG_ALLOW_SCRIPTS | PARCEL_FLAG_ALLOW_LANDMARK |
            PARCEL_FLAG_ALLOW_TERRAFORM | PARCEL_FLAG_ALLOW_DAMAGE | PARCEL_FLAG_ALLOW_CREATE_OBJECTS |
            PARCEL_FLAG_USE_ACCESS_GROUP | PARCEL_FLAG_USE_ACCESS_LIST | PARCEL_FLAG_USE_BAN_LIST |
            PARCEL_FLAG_USE_LAND_PASS_LIST | PARCEL_FLAG_LOCAL_SOUND_ONLY;
    }
}
