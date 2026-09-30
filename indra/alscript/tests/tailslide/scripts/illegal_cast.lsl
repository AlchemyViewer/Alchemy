default {
    state_entry() {
        llOwnerSay((string)1); // fine
        llOwnerSay((key)1);    // bad! $[E10035]
    }
}
