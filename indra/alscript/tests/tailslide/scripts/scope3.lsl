string baz = "quux";
float plugh = 3;
vector quux = <0, 0, plugh>;
list foo = [baz, quux, plugh];

test() { }
default {
    state_entry() {
        integer test = 1; // works fine
        test();
        test = 2;
    }

    touch_start(integer x) {
        jump test;
        test();
        @test;

        jump llOwnerSay;
        llOwnerSay(llDumpList2String(foo, "|"));
        llOwnerSay(llDumpList2String([baz, quux, plugh], "|"));
        @llOwnerSay;
    }
}
