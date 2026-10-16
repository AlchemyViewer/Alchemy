default {
    state_entry() {
        // continue-like jumps
        while (1) {
            if (llFrand(1)) {
                jump break_1;
                @break_1;
            }
        }
        while (1) {
            if (llFrand(1)) {
                1;
            } else {
                jump break_2;
                @break_2;
            }
        }
        while (1) {
            if (llFrand(1)) {
                jump break_3;
            } else {}
            @break_3;
        }

        // not continue-like
        while (1) {
            if (llFrand(1)) {
                jump break_4;
                @break_4;
            } else {}
        }

        // break-like jumps
        while (1) {
            if (llFrand(1)) {
                jump break_5;
            }
        }
        @break_5;

        // not break-like

        while (1) {
            if (llFrand(1)) {
                jump break_6;
            }
        }
        1;
        @break_6;

        while (1) {
            while (1) {
                // can't break out of two loops!
                jump break_7;
            }
        }
        @break_7;
    }
}
