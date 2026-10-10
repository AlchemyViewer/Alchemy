// A negated number in a global initializer is a single constant to LL's compiler,
// not a negation, while in an expression it's a negation of the positive literal.
integer gi = -1; // $[E20009]
integer gbig = -2147483648; // $[E20009]
float gf = -1.0; // $[E20009]
integer gs = -ALL_SIDES; // $[E20009]

default {
    state_entry() {
        integer li = -1; // $[E20009]
        float lf = -1.0; // $[E20009]
    }
}
