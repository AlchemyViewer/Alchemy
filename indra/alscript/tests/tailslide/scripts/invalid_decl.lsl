default {
    state_entry() {
        do
            string baz = "quux"; // $[E10032]
        while (llGetStartParameter());
        llOwnerSay(baz);
    }
}
