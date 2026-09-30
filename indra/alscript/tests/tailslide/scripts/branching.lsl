default {
    state_entry() {
        if (0) {  // $[E20013]
            if (0) {  // $[E20013]
                1.0;
            }
        } else {
            1.0;
        }

        for(2, 2; 2; 2) {
            2.0;
        }

        while(3) {
            3.0;
        }

        do {
            4.0;
        } while (4);
    }
}
