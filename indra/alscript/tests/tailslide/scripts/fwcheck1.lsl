default {
    state_entry() {
        integer a = 1; // don't warn about initial value unused (?)
        integer b;

        if ( a ) {      // always true
            b = 0;
            if ( a )    // also always true
                b = 2;  // previous value assigned to b is never used
            
            if ( b ) // always true
                b = -1;
            
            if ( llSameGroup(llGetOwner()) ) { // don't know if this is true
                a = b + 1; // use b
            } else {
                b = a + 1; // don't use b
            }

            if ( llSameGroup(llGetOwner()) ) {
                a = 1;
                if ( llSameGroup(llGetOwner()) ) {
                } else if ( b ) {   // don't know what b is here
                } else {
                    b = 1;
                }
            }

            return;     // also never used
        }

        // code is never reached
        b = 3;
    }
}


