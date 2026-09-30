// Tests for expressions allowed in LL's LSL compiler

float foo = -PI; // fine, this is a builtin
float baz = foo;
float quux = -baz;  // bad, this doesn't reference a builtin $[E10021]
vector barz = <0, 0, baz>; // composition of simpleassignables is allowed
vector fooz = <0, 0, barz.z>; // but using the accessor form is not $[E10021]
integer negbool = -TRUE; // illegal in global contexts because arcane lexer reasons... $[E10021]
vector negvec = -ZERO_VECTOR; // negating is only allowed for _integer_ lexer constants. $[E10021]

default {
    state_entry() {
        llOwnerSay((string)[foo, baz, quux, barz, fooz, negbool, negvec]);
    }
}
