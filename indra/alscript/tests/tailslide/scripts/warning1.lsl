default {
    state_entry() {
        integer a;
        integer b;
        if ( a = 1 );           // $[E20002] $[E20007] assign as comparison no parens, empty if
        if ( (a = 1) ) {}       // don't want about assignment. empty if: $[E20007]

        if ( (b = 1) == 2 );    // don't warn about these
        else if ( b == 2 );     // don't warn about these
        else a = 2;             // don't warn about these
    }
}
