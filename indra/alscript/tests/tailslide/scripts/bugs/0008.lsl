// Reported By: Strife Onizuka
// Date: 2006-02-12
// Version: 0.2.1
// Error: missing vector/rotation operators

default {
    state_entry() {
        if ( - <1,2,3> == <-1,-2,-3> ) return;                  // $[E20012]  should be always true
        if ( - <4,3,2,1> == <-4,-3,-2,-1> ) return;             // $[E20012]
        if ( <1,2,3,4> - <0,1,2,3> == <1,1,1,1> ) return;       // $[E20012]
        if ( <1,2,3> / <1,2,3,4> == <30, 60, 90> ) return;      // no vector/rotation support yet
    }
}

