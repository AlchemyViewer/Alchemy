default {
    state_entry() {
        state foo;
    }
}

state foo {
    state_entry() {
        state bar;
    }
}

state bar {
    state_entry() {
        state default;
    }
}
