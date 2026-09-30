integer CONST       = 16;
integer NOT_CONST   = 0;
integer ALSO_NOT_CONST = 1;
integer NOT_SET;                    // and not used         $[E20009]
list    EMPTY_LIST  = [];
list    NOT_EMPTY_LIST = [3,2,1];

default {
    state_entry() {
        list l = [];
        list m = [1,2,3];
        integer j;

        ALSO_NOT_CONST = 1;

        if ( CONST )                // condition is always true                     $[E20012]
            NOT_CONST = 1;          // value assigned to NOT_CONST is never used

        if ( l == EMPTY_LIST )      // condition is always true                     $[E20012]
            NOT_CONST = 2;          // value assigned to NOT_CONST is never used

        if ( m == EMPTY_LIST )      // condition is always false                    $[E20013]
            NOT_CONST = 3;

        if ( m == NOT_EMPTY_LIST )  // condition always true, only comparing lengths $[E20012] $[E20011]
            NOT_CONST = 4;

        j = 4;                      // value assigned to j is never used
        NOT_CONST = 6;              // but don't warn about this, because it is 
                                    //  global and may be used by something else
    }
    
    state_exit() {
        integer i = ALSO_NOT_CONST;     // unused: $[E20009]
        integer const_here = 4;
        string string_const = "hi";
        rotation quat_const = <1,2,3,4>;

        if ( const_here == 4 )          // always true  $[E20012]
            llSay( const_here, "hi" );

        if ( const_here != (2 + 2) )    // always false $[E20013]
            return;

        if ( !(string_const == "hi") )  // always false $[E20013]
            return;

        if ( quat_const == <1,2,3,4> )  // always true  $[E20012]
            return;

    }
}
