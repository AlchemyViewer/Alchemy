// Test various jump scenarios to verify function-level annotations

// No jumps at all
no_jumps() {
    llOwnerSay("hello");
}

// Break-like jump (structured)
break_like() {
    while (1) {
        if (llFrand(1.0))
            jump done;
    }
    @other; // $[E20009]
    @done;
}

// Continue-like jump (structured)
continue_like() {
    while (1) {
        if (llFrand(1.0))
            jump loop_end;
        llOwnerSay("middle");
        @loop_end;
    }
}

// Unstructured: jump outside loop
unstructured_no_loop() {
    if (llFrand(1.0))
        jump skip;
    llOwnerSay("maybe");
    @skip;
}

// Unstructured: jump forward in middle of loop
unstructured_forward() {
    while (1) {
        if (llFrand(1.0))
            jump middle;
        llOwnerSay("before");
        @middle;
        llOwnerSay("after");
    }
}

// Unstructured: jump across nested loops
unstructured_nested() {
    while (1) {
        while (1) {
            jump outer;
        }
        @bar; // $[E20009]
    }
    @outer;
}

// Duplicate labels with Linden semantics (unstructured)
duplicate_labels() {
    while (1) {
        if (llFrand(1.0))
            jump foo; // $[E20016]
        @foo; // $[E20009]
    }
    @foo; // $[E20017] duplicate
}

default {
    state_entry() {
        no_jumps();
        break_like();
        continue_like();
        unstructured_no_loop();
        unstructured_forward();
        unstructured_nested();
        duplicate_labels();
    }
}
