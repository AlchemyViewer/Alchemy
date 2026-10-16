string foo;
string bar = foo;
string quux = "";
list baz = [bar]; // must have an initializer for all SAIdentifiers in SALists under lso mode $[E10021]
list bazzy = [quux];

default {
    state_entry() {llOwnerSay((string)baz + (string)bazzy);}
}
