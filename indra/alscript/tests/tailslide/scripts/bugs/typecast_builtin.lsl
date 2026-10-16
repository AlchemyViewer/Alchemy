default{
    state_entry(){
        // fine
        (float)-PI;
        // bad, syntax error and type error.
        (float)-NULL_KEY;  // $[E10002] $[E10020]
        integer i;
        // bad, only works on builtin floats and ints.
        (float)-i;  // $[E10020]
    }
}