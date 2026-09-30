default {
    state_entry() {
        integer i;
        float f;
        vector v;
        rotation r;
        string s;
        key k;
        list l;

        1 + 1;
        l + 1;
        1 + l;
        2.0 + 2.0;
        v + v;
        r + r;
        s + s;
        l + l;

        2 - 2;
        2 - 2.0;
        v - v;
        r - r;

        i / i;
        f / f;
        v / f;
        v / r;
        r / r;

        i % i;
        v % v;

        i >= i;
        i >= f;
        i <= i;
        i <= i;
        i < i;
        i < f;
        i > i;
        i > f;

        i == i;  // $[E20018]
        f == f;  // $[E20018]
        s == s;  // $[E20018]
        k == k;  // $[E20018]
        s == k;  // $[E20018]
        k == s;  // $[E20018]
        v == v;  // $[E20018]
        r == r;  // $[E20018]
        l == l;  // $[E20018]

        i != i;
        f != f;
        s != s;
        k != k;
        s != k;
        k != s;
        v != v;
        r != r;
        l != l;

        1 && 2;
        1 || 2;
        1 | 2;
        1 & 2;
        1 ^ 2;
        1 >> 2;
        1 << 2;

        i *= f;  // $[E20015]
    }
}
