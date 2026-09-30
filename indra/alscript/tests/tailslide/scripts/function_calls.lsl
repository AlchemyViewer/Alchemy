integer foo(integer i, integer g) {  // $[E20009]
    return i;
}

bar(integer i) {  // $[E20009]
    i;
}

default {
    state_entry() {
        llFrand(1.0);
        foo(1, 2);
    }
}
