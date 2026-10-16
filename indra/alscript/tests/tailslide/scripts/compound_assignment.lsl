default {
    state_entry() {
        vector v = <1,1,1>;
        float f = 1.0;
        integer i = 1;
        v %= <2,2,2>;
        v *= <2,2,2>; // $[E10002]
        f += 1;
        i *= 2.0; // technically allowed, but oh so broken. $[E20015]
        llSetPos(v);
    }
}
