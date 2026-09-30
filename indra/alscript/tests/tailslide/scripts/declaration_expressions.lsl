string bar = foo; // disallowed, foo declared later. $[E20009] $[E10006]
string foo = "2";
string baz = baz; // disallowed, refers to self $[E10006] $[E20009]

default {
    state_entry() {
        {
            // fine to shadow
            integer foo = 2; // $[E20001] $[E20009]
        }
        // still the same type out here
        string foo = foo; // allowed, refers to string foo from outer scope $[E20009] $[E20001]
        string quux = plugh; // disallowed $[E20009] $[E10006]
        string plugh = plugh; // disallowed $[E10006] $[E20009]
    }
}
