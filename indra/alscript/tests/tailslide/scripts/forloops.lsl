default {
    state_entry() {
        integer i;
        integer j;
        for(;1;) llResetTime();
        for(i=1;j=2;) {
            llResetTime();
        }
        for(i=1,j=2,j=2,j=2;j=2;j=1,j=2) { // $[E20014]

        }
        // not a vector, chained comparison expressions!
        for(1<1,1,1>1; 1; 1){} // $[E20014]
    }
}
