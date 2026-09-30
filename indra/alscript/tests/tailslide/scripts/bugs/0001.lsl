// Reported by: Strife Onizuka
// Date: 2006-02-08
// Version: v0.1.1
// Bug: "variable `a' declared but never used"

fun(vector a) {
    llOwnerSay((string)a.x);
}

default {
    state_entry() {
        fun(ZERO_VECTOR);
    }
}

