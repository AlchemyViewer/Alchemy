// Reported By: Greg Hauptmann
// Date: 2006-03-30
// Version: 0.2.4
// Error: setting string to empty list should fail
string emailStringBuffer = []; // $[E10015]
integer j = "hi";              // $[E10015]
string hi = 14;                // $[E10015]

default {
    state_entry() {
        emailStringBuffer = []; // $[E10002]
        j = "cats";             // $[E10002]
        hi = 16;                // $[E10002]
    }
}
