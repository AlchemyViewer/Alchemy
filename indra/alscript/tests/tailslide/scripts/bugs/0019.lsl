// Reported By: masa
// Date: 2006-12-06
// Version: 0.2.6
// Error: 1.13.0 constants/functions, 1.11.2 constants

default {
    state_entry() {
        integer prim_count        = llGetObjectPrimCount( llGetKey() );
        list    prim_owners       = llGetParcelPrimOwners( llGetPos() );
        integer parcel_max_prims  = llGetParcelMaxPrims( llGetPos(), FALSE );
        integer parcel_prim_count = llGetParcelPrimCount( llGetPos(), PARCEL_COUNT_TOTAL |
                                                          PARCEL_COUNT_OWNER | PARCEL_COUNT_GROUP |
                                                          PARCEL_COUNT_OTHER | PARCEL_COUNT_TEMP |
                                                          PARCEL_COUNT_SELECTED, FALSE );
        list    parcel_details    = llGetParcelDetails( llGetPos(), [ PARCEL_DETAILS_NAME,
                                                        PARCEL_DETAILS_DESC, PARCEL_DETAILS_OWNER,
                                                        PARCEL_DETAILS_GROUP, PARCEL_DETAILS_AREA ] );

        list    use_stuff = [ prim_count, prim_owners, parcel_max_prims, // $[E10034] $[E10034]
                              parcel_prim_count, parcel_details,
                              PARCEL_FLAG_RESTRICT_PUSHOBJECT |
                              REGION_FLAG_RESTRICT_PUSHOBJECT ];

        llRequestAgentData( llGetOwner(), DATA_PAYINFO );

        llOwnerSay( llList2CSV(use_stuff) );
    }
}
