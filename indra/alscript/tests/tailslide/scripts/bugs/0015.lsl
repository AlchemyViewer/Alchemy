// Reported By: Strife Onizuka
// Date: 2006-03-07
// Version: 0.2.3
// Error: integer - float should be float

default {
    state_entry() {
        rotation move_a;
        integer mask_b;
        mask_b = mask_b << (5 - move_a.s); // $[E10002]
    }
}
