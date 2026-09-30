default {
    state_entry() {
        integer a;
        if (a = 1){llOwnerSay("foo");}  // $[E20002] warning: assignment as comparison
        integer a;   // $[E10001] error: duplicate declaration
    }
}
