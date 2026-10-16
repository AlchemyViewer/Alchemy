default {
    state_entry() {
        key k;
        vector v;
        rotation r;

        "" + "";
        v + v;
        r + r;

        1 - 1;
        v - v;
        r - r;

        v * 1.0;
        1.0 * v;
        v * v;
        v * r;
        r * r;

        1 / 1;
        v / 1.0;
        v / r;
        r / r;

        1 % 1;
        v % v;

        k == k;  // $[E20018]
        v == v;  // $[E20018]
        r == r;  // $[E20018]
        [] == [];  // $[E20018]
        [] != [];
    }
}
