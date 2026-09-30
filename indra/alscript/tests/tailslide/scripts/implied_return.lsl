default {
    state_entry() {
        if (1)  // $[E20012]
            return;
        else {
            return;
        }
        // no return should be injected here!
    }

    moving_start() {
        while (1) return;
        // return should be injected here
    }

    moving_end() {
        if (1) {} else { return; }  // $[E20012]
        // return should be injected here, no ret in true branch.
    }

    no_sensor() {
        return;
        {}
        // return should be injected here since the empty compound statement
        // counts as code after the RET, even though it didn't do anything.
    }
}
