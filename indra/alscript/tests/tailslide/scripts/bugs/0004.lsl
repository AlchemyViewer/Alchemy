// Reported By: Strife Onizuka
// Date: 2006-02-06
// Version: v0.1.2
// Error: llRotateTexture is undefined
// Notes: masa: added some other functions that were excluded for the same reason

default {
    state_entry() {
        llRotateTexture( PI, ALL_SIDES );
        llSetVehicleVectorParam( 0, ZERO_VECTOR );
        llSetVehicleRotationParam( 0, ZERO_ROTATION );
        llGroundContour( ZERO_VECTOR );
        llRemoteLoadScript( NULL_KEY, "", 1, 0 );   // deprecation error $[E20020]
        llListReplaceList( [1,2,3], [1], 2, 2 );    // first fix ate these functions
        llListInsertList( [1,2,3], [1], 2 );
        llGetScriptState("");                       // another fix ate this one instead
    }
}
