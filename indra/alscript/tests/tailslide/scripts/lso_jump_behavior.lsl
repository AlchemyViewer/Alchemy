default {
    state_entry() {
        // doesn't jump to bar, only last jump to a given label name is counted.
        // will just jump past its operand (which will be 0) making this effectively a no-op.
        // https://wiki.secondlife.com/wiki/Jump#Deep_Notes doesn't appear to be true anymore.
        //
        // looking at the open-sourced lscript code only a 1->1 relationship between explicit
        // jumps and labels is possible, implicitly, due to label name->offset entries being
        // clobbered by the last addition to the map.
        jump bar;  // $[E20016]
        {
            @bar; // $[E20009]
            // no-op for same reasons above
            jump bar;  // $[E20016]
        }
        // nothing ever jumps to this, even though by normal lexical scoping rules the first jump
        // should end up here.
        @bar;  // $[E20009] $[E20017]
        {
            @bar;  // $[E20001] $[E20017]
            // NOT ignored, bar now refers to the label in the nested scope due to
            // labels violating normal lexical scoping rules. this is an infinite loop.
            jump bar;
        }
    }
}
