integer gi;
float gf;

default {
    state_entry() {
        integer li;
        float lf;
        li = 1;
        lf = 1.0;
        gi = 1;
        gf = 1.0;

        1 + 1;
        2 - 2.0;
        1 / 2.0;
        1 % 2;
        1 >= 2.0;
        1 <= 2.0;
        1 > 2.0;
        1 < 2.0;
        1 == 2.0;  // $[E20018]
        1 != 2.0;
        1 && 2;
        1 || 2;
        1 | 2;
        1 & 2;
        1 ^ 2;
        1 >> 2;
        1 << 2;

        li *= 2;
        gi *= 2;
        gf *= 2;
        gf /= 1.0;
        gi %= 2;
        gf += 1.0;
        gf -= 1.0;
        // puke.
        gi *= 1.0;  // $[E20015]
        gf = gi *= 1.0;  // $[E20015]

        ++gi;
        --gi;
        !gi;
        ~gi;
        -gi;
        gi--;
        gi++;

        // This is actually 1 with a unary minus operation rather than
        // literal -1. Why.
        -1;
        -1.0;
    }
}
