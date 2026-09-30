integer foo() { // $[E10019]
    jump bar;
    return 1;
    // presence of a label after the return should
    // trigger an error about missing return value.
    @bar;
}

integer bar() {
    if (llFrand(1)) {
        if (llFrand(1)) {
            return 1;
        } else {
            return 1;
        }
    } else {
        return 1;
    }
}

integer foo_compound() { // $[E10019]
    return 1;
    // empty compound statement affects return check,
    // even though it's a no-op
    {}
}

integer foo_noop() { // $[E10019]
    return 1;
    // explicit noop affects return checks too
    ;
}

integer bar_jump() { // $[E10019]
    if (llFrand(1)) {
        if (llFrand(1)) {
            return 1;
        } else {
            return 1;
            // location of the jump is irrelevant to return reachability check.
            jump bazzy;
            @bazzy;
        }
    } else {
        return 1;
    }
}

integer quux() { // $[E10019]
    // this shouldn't satisfy the constraint that all paths return
    while(0)
        return 1;
    // this neither.
    while(1)
        return 1;
}



default {
    state_entry() {
        foo();
        foo_compound();
        foo_noop();
        bar();
        bar_jump();
        quux();
    }
}
