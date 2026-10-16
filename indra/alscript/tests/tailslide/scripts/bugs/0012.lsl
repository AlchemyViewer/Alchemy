// Reported By: Kayla Stonecutter
// Date: 2006-02-18
// Version: 0.2.2
// Error: PAY_HIDE and PAY_DEFAULT are not defined
//        masa: DATA_SIM_RATING is also not defined

default {
    state_entry() {
        llSetPayPrice(PAY_HIDE, [10, PAY_DEFAULT, PAY_HIDE, PAY_HIDE]);
        llRequestSimulatorData( "Baku", DATA_SIM_RATING );
    }
}

