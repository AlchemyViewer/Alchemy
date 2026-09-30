list foo = [1,2];
list bar = [foo]; // $[E10034]

default {
    state_entry() {
        list baz = [foo]; // $[E10034]
        llOwnerSay((string)(foo + bar + baz));
    }
}
