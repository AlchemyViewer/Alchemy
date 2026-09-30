default {
    state_entry() {
        list l;
        l += llOwnerSay("");  // $[E10002]
        l = l + llOwnerSay("");  // $[E10002]
        l = llOwnerSay("") + l;  // $[E10002]
        print(l);
    }
}
