default {
    state_entry() {
        integer foo = 1;  // $[E20009]
        float bar = 1.0;  // $[E20009]
        string quux = "foo";  // $[E20009]
        integer some_i;  // $[E20009]
        float some_f;  // $[E20009]
        string some_s;  // $[E20009]
        // no key with a literal since we want a separate test for autocast.
        key some_k;  // $[E20009]
        // no key with a literal since we want a separate test for list expressions.
        list l;  // $[E20009]
    }
}
