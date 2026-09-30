// Reported By: Strife Onizuka
// Date: 2006-03-01
// Version: 0.2.3
// Error: integer + float should be valid

default {
    state_entry() {
        1 + 1.0;
    }
}

