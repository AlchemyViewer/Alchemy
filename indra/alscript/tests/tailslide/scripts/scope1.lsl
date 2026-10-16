integer test = 1;                               // unused: $[E20009]
default { state_entry() { state test; } }       // test is variable not state: $[E10005]
state test {                                    // already declared: $[E10001]
    state_entry() {}
}
