integer foo() {
    llSetText((string)llGetTime(), <1,1,1>, 1);
    return 1;
}

default {
    state_entry() {
        if(0) // $[E20013]
            do
                @bar;
            while(foo());

        llOwnerSay("test");
        jump bar;
    }
}
