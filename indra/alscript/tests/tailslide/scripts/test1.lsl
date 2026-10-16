integer global_test  = 5;
integer global_test2;

default {
    state_entry() {
        string test = "Hello World!";
        vector numbers = <123.1, 345.3, 987.6>;
        numbers = numbers * (numbers * numbers);
        numbers = numbers % numbers;
        llSay( 0, test );
        llSay( 0, (string) numbers );
        llSay( 0, test + (string) numbers );
        numbers.x = numbers.y = numbers.z;
        llSay( 0, (string) numbers );
        numbers.x = global_test;
        state other_state;
    }
}   

state other_state {
    state_entry() {
        list words = ["I", "am", "in", "other_state"];
        llSay( 0, llDumpList2String(words, " ") );
        jump label1;
        llSay( 0, "Shouldn't get here.");
        jump label2;
        @label1;
        llSay( 0, "Hi from label1." );
        @label2;
        llSay( 0, "Hi from label2." );
        global_test2 = 3;
    }
}
