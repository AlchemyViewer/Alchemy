// Reported By: Strife Onizuka
// Date: 2006-02-13
// Version: 0.2.1
// Error: crash when using global vectors/rotations in operations

vector      v = <1,2,3>;
rotation    r = <9,8,9,1>;

default {
    state_entry() {
        if ( r == <v.x, v.y, v.z, 1> ) return;      // $[E20013] always false
        if ( v == <r.x, r.y, r.z> ) return;         // $[E20013]
    }
}
