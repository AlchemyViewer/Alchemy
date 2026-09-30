string str_1 = "";
string str_2 = str_1;
string str_3 = str_2; // $[E20009]

string str_4;
string str_5 = str_4;
string str_6 = str_5; // $[E20009]

integer i = 1;
// this should be added to the list as an int under LSO, not a float!
float f = i;
float actually_f = 1.0;
vector v = <f, 2, 3>;
// this should be written as a key global with a string heap entry
key k = NULL_KEY;
rotation r = ZERO_ROTATION;
rotation r2;
rotation r3 = r2; // should be == <0, 0, 0, 0> and not <0, 0, 0, 1> as above. $[E20009]
rotation r4 = r;  // $[E20009]

list l = [i, f, actually_f, v, str_1, k, r, v];  // $[E20009]

default {
    state_entry() {
    }
}
