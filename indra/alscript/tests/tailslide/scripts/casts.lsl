default {
    state_entry() {
        key k;

        (float)1;
        (string)1;
        (list)1;

        (integer)1.0;
        (string)1.0;
        (list)1.0;

        (integer)"foo";
        (float)"foo";
        (key)"foo";
        (vector)"foo";
        (rotation)"foo";
        (list)"foo";

        (string)k;
        (list)k;

        (string)<1.0, 2.0, 3.0>;
        (list)<1.0, 2.0, 3.0>;

        (string)<1.0, 2.0, 3.0, 4.0>;
        (list)<1.0, 2.0, 3.0, 4.0>;

        (string)([]);

        // self-casts
        (integer)1;
        (float)1.0;
        (string)"foo";
        (key)llGetKey();
        (vector)ZERO_VECTOR;
        (rotation)ZERO_ROTATION;
        (list)[];

        // negation inside cast
        (integer)-1;
    }
}