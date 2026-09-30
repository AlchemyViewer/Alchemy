// A global whose rvalue couldn't be evaluated because of an error we already
// reported shouldn't make every global that refers to it complain as well.
string foo = undefined_thing;   // $[E10006]
string bar = foo;
string baz = bar;

default {
    state_entry() {
        llOwnerSay(baz);
    }
}
