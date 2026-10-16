float foo() {
    return 1;
}

default {
    state_entry() {
        integer i;
        i += 1;
        ++i;
        i++;
        vector v = <2,2,2>;
        v.x += 2;
        ++v.x;
        v.x++;
        float f = 3;
        f = 3;
        f += 3;

        key k = "foo";
        string s = k;  // $[E20009]
        llGetOwnerKey("bar");
        llOwnerSay(k);
        llFrand(4);
        llFrand(++i);
        foo();
    }
}
