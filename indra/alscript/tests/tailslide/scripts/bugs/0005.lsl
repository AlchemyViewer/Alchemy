// Reported By: Strife Onizuka
// Date: 2006-02-06
// Version: v0.1.2
// Error: list == [] shouldn't give a warning

integer a;

default {
    state_entry() {
        list l;
        list j = [];
        if ( l == [] ) // $[E20012] this is known to always be true, though
            a++;
        if ( [] == l ) // $[E20012]
            a++;
        if ( l == j ) // $[E20012]
            a++;
    }
}


