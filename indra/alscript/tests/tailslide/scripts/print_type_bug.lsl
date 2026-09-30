default {
    state_entry() {
        // in LL's compiler print() is seen to return a string in this context,
        // even though there won't be a string on the stack. We don't support that.
        string foo = print("bar");  // $[E10015] $[E20009]
        // We also don't support void expressions as an argument to print()
        print(llOwnerSay("foo"));  // $[E10011]
    }
}
