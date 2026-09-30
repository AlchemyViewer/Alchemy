float foo = -2.0e+9999; // $[E20009]
float bar = 2.0e+9999; // $[E20009]

default {
    state_entry() {
        llSetPos(<-2.0e+9999, 2.0e+9999, 0>);
        llSetRot(<-2.0e+9999, 2.0e+9999, 0, 0>);
    }
}
