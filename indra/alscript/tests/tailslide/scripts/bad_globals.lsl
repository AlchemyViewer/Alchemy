// different kinds of bad declarations that generate error tokens
// and have unique parser branches
string baz = "";
string quux = ;

default {
    state_entry() {
        llOwnerSay(baz);
        llOwnerSay(quux);
        <;
    }
}
