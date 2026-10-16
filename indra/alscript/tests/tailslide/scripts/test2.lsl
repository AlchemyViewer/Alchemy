integer CHANNEL = 0;

default {
    state_entry() {
        integer i = 1;
        llSay(CHANNEL, "hello");
        llSetTimerEvent(i);
    }

    timer() {
        llSay(CHANNEL, "timer");
        state other;
    }
}

state other {
    state_entry() {
        llSay(CHANNEL, "other state");
    }
}

