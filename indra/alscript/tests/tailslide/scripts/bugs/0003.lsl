// Reported By: masa
// Date: 2006-02-08
// Version: 0.1.1
// Error: "Type mismatch: string string" is utterly unhelpful

default {
    state_entry() {
        string a;
        "A" | "B";  // $[E10002]
        a++;        // $[E10002]
    }
}    
