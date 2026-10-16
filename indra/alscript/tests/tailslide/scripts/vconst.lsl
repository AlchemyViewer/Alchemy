float q = 1;
vector v = <1,q,3>;

default {
    state_entry() {
        vector baz = <1,2,<1,1,1>*<1,1>1,1> >;
        list foo =[<1,2,<1,1,1>*<1,1>1,1> >];
        llOwnerSay((string)baz);
        llOwnerSay((string)foo);
        v = <0,0,0>;
    }
}



