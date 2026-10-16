default {
    state_entry() {
        vector v = <1,2,3>;
        v.x = 2;
        // make sure this doesn't get output with "<<" or ">>", which are shift operators...
        v = < <v.x, 1, 2> * <1,2,3>, 2, <v.x, 1, 2> * <1,2,3> >;
        llSetPos(v);
    }
}
