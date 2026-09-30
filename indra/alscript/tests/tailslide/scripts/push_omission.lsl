default {
    state_entry() {
        integer i = 1;
        integer j = 2;
        // the dup / pop for the result of this assignment may be omitted
        i = 3;
        // but not for the = in the subexpression of this one!
        j = i = 4;
        // parenthesised expression statements should also have their pushes omitted
        (i = 5);
        ((i = 6));
        // and so should the init / increment expressions of a for-loop, even when parenthesised
        for ((i = 0); i < 3; (i += 1)) {}  // $[E20014]
        // we can't omit the pop because the pushing of the retval is controlled by the callee
        llFrand(1.0);
        // ...and the same is true when the call is parenthesised
        (llFrand(1.0));
    }
}
