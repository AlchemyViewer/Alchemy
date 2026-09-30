default {
    state_entry() {
        llOwnerSay((string)(2 + 2 * 2 + 2 - 2));
        llOwnerSay((string)(llGetTime() + llGetTime() + llGetTime() - llGetTime()));
        llOwnerSay("foo" + llGetDisplayName(llGetOwner()) + "baz");
    }
}
