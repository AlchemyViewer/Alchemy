// Constant propagation test

integer s = 2;
float minus_pi = -PI;
rotation uninit_r;
list gl = [1,2,3];
// a list global may be initialized from another list global, and the value
// propagates even though the list itself never gets inlined anywhere.
list gl_copy = gl;

default {
    state_entry() {
        integer    i = 1;
        float      f = 1.0;
        vector     v = <1,2,3>;
        quaternion r = <3,2,1,0>;
        key        k = NULL_KEY;
        key        k2 = NULL_KEY;
        list       l = [1,2,3];
        if ( s == 2 ) return;                   // $[E20012]
        string     s = "hello";                 // $[E20001]

        if ( i == f ) return;                   // $[E20012]
        if ( i != f ) return;                   // $[E20013]
        if ( i == (f+1) ) return;               // $[E20013]
        if ( (i+1) == (f+1) ) return;           // $[E20012]
        if ( f == 1.0 ) return;                 // $[E20012]
        if ( f == 1.1 ) return;                 // $[E20013]

        if ( v == <1,2,3> ) return;             // $[E20012]
        if ( v == <r.z, r.y, r.x> ) return;     // $[E20012]
        if ( v.x == v.y ) return;               // $[E20013]
        if ( r == <v.z, v.y, v.x, 0> ) return;  // $[E20012]

        if ( l == [6,5,2] ) return;             // $[E20012] $[E20011]
        if ( l == [r.z, r.y, r.x] ) return;     // $[E20012] $[E20011]
        if ( l == [] ) return;                  // $[E20013]
        if ( gl_copy == [1,2,3] ) return;       // $[E20012] $[E20011]

        if ( s == "hello" ) return;             // $[E20012]
        if ( s + " world" == "h" + "e" + "llo" + " wo" + "rld" ) return;    // $[E20012]
        if ( i ^ 1 == 1 ) return;               // $[E20013]
        if ( ~i == 0xFFffFFfe ) return;         // $[E20012]
        if ( f == 1E0 ) return;                 // $[E20012]
        if ( f == 1.E0 ) return;                // $[E20012]
        if ( f == 0.1E1F ) return;              // $[E20012]
        if ( f == .1E1F ) return;               // $[E20012]
        if ( (string)1 == "1" ) return;         // $[E20012]
        if ( (string)1 == "0" ) return;         // $[E20013]
        if ( (integer)"0xF" == 15 ) return;     // $[E20012]
        if ( (integer)"077" == 77 ) return;     // $[E20012]
        if ( (integer)"foo" == 0 ) return;      // $[E20012]
        if ( (float)"0x1" == 1.0 ) return;      // $[E20012]
        if ( (float)((float)"1.0") == 1.0 ) return;      // $[E20012]
        if ( minus_pi == -PI ) return;          // $[E20012]
        if ( (float)"inf" == (float)"inf" ) return; // $[E20012]
        // SL doesn't ever do -NaN
        if ( "NaN" == (string)(((float)"inf") * 0.0) ) return; // $[E20012]
        // uninitialized rotation is <0, 0, 0, 1>, not <0, 0, 0, 0>!
        if ( uninit_r == <0, 0, 0, 1> ) return; // $[E20012]
        if ( k == NULL_KEY && k == k2 ) return;  // $[E20012]
        // uppercase hex prefix is hex too, and a one-char string must not be
        // read past its terminator while sniffing for the prefix
        if ( (integer)"0X1F" == 31 ) return;     // $[E20012]
        if ( (integer)"0" == 0 ) return;         // $[E20012]
        // string -> integer behaves like a 32-bit strtoul(): anything past 0xFFFFFFFF
        // saturates to -1 rather than wrapping, whether or not there's a sign.
        if ( (integer)"4294967295" == -1 ) return;   // $[E20012]
        if ( (integer)"4294967296" == -1 ) return;   // $[E20012]
        if ( (integer)"4294967297" == -1 ) return;   // $[E20012]
        if ( (integer)"0x100000000" == -1 ) return;  // $[E20012]
        if ( (integer)"-4294967296" == -1 ) return;  // $[E20012]
        if ( (integer)"99999999999999999999" == -1 ) return; // $[E20012]
        // but in-range negatives are negated in unsigned space
        if ( (integer)"-4294967295" == 1 ) return;   // $[E20012]
        if ( (integer)"  -5xyz" == -5 ) return;      // $[E20012]
        if ( (integer)"-" == 0 ) return;             // $[E20012]
        // dividing by -1 negates, and INT_MIN / -1 wraps back to INT_MIN
        if ( 5 / -1 == -5 ) return;              // $[E20012]
        if ( 0x80000000 / -1 == 0x80000000 ) return; // $[E20012]
        // both references inline the symbol's constant value; the second must
        // get its own copy rather than stealing the node from the first
        integer dup = 7;
        if ( dup == 7 ) return;                  // $[E20012]
        if ( dup == 7 ) return;                  // $[E20012]

        llFrand(r.z);
        llFrand((float)"inf");
        llFrand(((float)"inf") * 0.0); // will result in NaN, don't fold.

        jump foo;
        integer x = 1;
        @foo;
        // Should not be folded due to the label between the declaration of the var and
        // its use. x will really be equal to 0 here but all the unstructured jumping makes
        // it hard to reason about.
        //
        // TODO: it's ok to fold if all uses of the label occur between the declaration and
        //  use of the var.
        if ( x == 1 ) return;
    }
}
