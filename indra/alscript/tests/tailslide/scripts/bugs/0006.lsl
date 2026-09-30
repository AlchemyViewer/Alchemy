// Reported By: Strife Onizuka
// Date: 2006-02-06
// Version: v0.1.2
// Error: Error on multiple handlers for the same event

default {
    state_entry() { }
    state_entry() { } // $[E10033]
    state_entry() { } // $[E10033]
    state_entry() { } // $[E10033]
}


