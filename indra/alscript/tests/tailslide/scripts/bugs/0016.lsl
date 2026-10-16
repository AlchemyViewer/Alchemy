// Reported By: Greg Hauptmann
// Date: 2006-03-28
// Version: 0.2.4
// Error: integer/float should work (result is float)
default {
    state_entry() {
        1 / 3.0; 
    }
}
