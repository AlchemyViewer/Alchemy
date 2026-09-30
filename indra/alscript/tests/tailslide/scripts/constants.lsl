integer PRIM_GLOW() { // $[E10026] $[E20009]
    return 1;
}

default {
   state_entry() {
      integer x = PRIM_TEXTURE; // $[E20009]
      integer y = PRIM_GLOW + PRIM_TEXTURE;  // $[E20009]
      integer PARCEL_DETAILS_DESC; // $[E20009]  // $[E10026]
      integer PARCEL_DETAILS_NAME = 5;  // $[E20009]  // $[E10026]

      PRI_GLOW = 2;  // $[E10006]
      PRIM_GLOW = 2;  // $[E10025]
      ZERO_VECTOR.x = 0.0; // $[E10025] $[E10008]
   }
}
