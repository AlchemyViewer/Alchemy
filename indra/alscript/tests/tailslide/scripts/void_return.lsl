foo() {
    if (1)  // $[E20012]
        return llOwnerSay("legal");
    return llOwnerSay("illegal");  // $[E10018]
}

default {
    state_entry() {
        foo();
        if (1)  // $[E20012]
            return llOwnerSay("legal");
        return llOwnerSay("illegal");  // $[E10017]
    }
}
