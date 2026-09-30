// Reported By: Ima Mechanique
// Date: 2007-06-26
// Version: 0.2.8
// Error: llRegionSay, HTTP_BODY_TRUNCATED

default {
    state_entry() {
        llRegionSay(HTTP_BODY_TRUNCATED, "hi");
    }
}
