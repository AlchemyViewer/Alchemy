// Reported By: Strife Onizuka
// Date: 2006-02-12
// Version: 0.2.0
// Error: segfault

list commands;      // unused because of syntax error $[E20009]
integer tmode;      
integer mode;       // $[E20009]

default
{
    state_entry()
    {
        mode = llList2Integer(commands, tmode + 1)
        llOwnerSay("T");    // syntax error from missing ; $[E10020]
        tmode += 3;
    }

    state_exit() {
    }
} 

