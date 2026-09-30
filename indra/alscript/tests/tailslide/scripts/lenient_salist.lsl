string foo;
string bar = foo;
string quux = "";
list baz = [bar]; // ok to not have an initializer for SAIdentifiers in SALists under mono semantics
list bazzy = [quux];

default {
    state_entry() {llOwnerSay((string)baz + (string)bazzy);}
}
