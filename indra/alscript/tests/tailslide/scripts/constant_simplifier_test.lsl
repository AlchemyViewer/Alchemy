default {
    state_entry() {
        vector v = <1.0, 2.0, 3.0>; // $[E20009]
        rotation r = <1.0, (float)2, 3, (float)((integer)4)>; // $[E20009]
        // Self-cast. Worthless, but sometimes used to prevent runtime evaluation of unary minus
        // on a constant with the old compiler!
        integer i = (integer)42; // $[E20009]
        // explicit cast
        integer j = (integer)42.0; // $[E20009]
        float f = (float)42; // $[E20009]
        // Implicit cast via promotion
        float g = 42; // $[E20009]
    }
}
