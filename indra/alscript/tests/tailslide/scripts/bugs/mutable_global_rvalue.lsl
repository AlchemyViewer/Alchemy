string foo = "foo";
list bar = [foo];
string baz = foo;

integer i = 2;
float f = i;

default {
    state_entry() {
        foo = "f";
        i = 3;
        llOwnerSay(foo + (string)bar + baz + (string)(i + f));
    }
}
