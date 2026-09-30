// Reported By: Kermitt Quirk
// Date: 2006-02-26
// Version: 0.2.3
// Error: ++/-- not counted as assignment 

default {
    state_entry() {
        list lst = [123, 456];
        
        integer count = 0;
        integer value = llList2Integer(lst, 0);
        
        if (value != 0) {
            count++;
        }
        
        if (count == 0) {
            llOwnerSay((string)value);
        }
    }
} 
