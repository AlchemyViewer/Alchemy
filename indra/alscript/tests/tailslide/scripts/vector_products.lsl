default {
    state_entry() {
        // dot product pairs each component with the same component of the other vector
        float d1 = <1.0, 0.0, 0.0> * <1.0, 0.0, 0.0>; // $[E20009]
        float d2 = <1.0, 0.0, 0.0> * <0.0, 0.0, 1.0>; // $[E20009]
        float d3 = <1.0, 2.0, 3.0> * <4.0, 5.0, 6.0>; // $[E20009]
        vector c1 = <1.0, 0.0, 0.0> % <0.0, 1.0, 0.0>; // $[E20009]
        vector c2 = <1.0, 2.0, 3.0> % <4.0, 5.0, 6.0>; // $[E20009]
        // A vector crossed with itself must be exactly zero. If the compiler fuses the
        // multiply and subtract this comes out as the rounding error of the products instead.
        // Basically, check if we de-fused the fused multiply-sub we didn't want to happen :)
        vector c3 = <0.1, 0.2, 0.3> % <0.1, 0.2, 0.3>; // $[E20009]
    }
}
