llOwnerSay(string msg) { // $[E10026] $[E20009]
    llSay(0, msg);
}
test2() { // $[E20009]
    // Should notice that this shadows the global "test"
    // even though it's declared later in the file.
    integer test; // $[E20009] $[E20001]
    {
        integer test; // $[E20001] $[E20009]
    }
}
integer test = 1;                       // $[E20009] unused
test() { }                              // $[E10001] already declared
default { state_entry() { test(); } }   // $[E10005] test is variable, not function
