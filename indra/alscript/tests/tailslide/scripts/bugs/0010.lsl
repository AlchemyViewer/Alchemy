// Reported By: Masakazu Kojima
// Date: 2006-02-13
// Version: 0.2.1
// Error: vector.s doesn't give an error, nor does vector.a, vector.house, ...
//        v.1 gives "expression and constant without operator" and makes a bad (- vector float) node

default {
    state_entry() {
        vector v;
        rotation r;

        // these are invalid
        [v.j, // $[E10008]
         v.s, // $[E10008]
         r.a, // $[E10008]
         r.house, // $[E10008]
         v.xample]; // $[E10008]


        // these are valid
        [v.x, v.y, v.z,
         r.x, r.y, r.z, r.s];
     }
}     
