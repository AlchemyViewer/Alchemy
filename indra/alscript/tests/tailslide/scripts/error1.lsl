integer number = llGetUnixTime();   // function exprs not treated as constant   $[E10021] $[E20009]
integer number = 3;                 // not allowed even though above fails      $[E10001]

vector j = <1,2,"3">;               // $[E20009] $[E10016]
rotation b = <1,2,3,"4">;           // $[E20009] $[E10016]
rotation c = <1,2,3,<1,2,3> >;      // $[E20009] $[E10016]
list foz = [<1,2,3,"4">];           // $[E20009] $[E10016]

string unused()  {                  // warning: declared but never used         $[E20009] $[E10019]
    state default;                  // error: can't change state in function    $[E10014]
    if (1 == 1) {                   // always true                              $[E20012]
        state default;              // warning: hack that corrupts stack        $[E20004]
        return;                     // error: returning nothing in int function $[E10018]
    }
}

string test(integer a, vector v) {  // param a is never used                    $[E20009]
    string q;                       // warning: declared but never used         $[E20009]
    return v;                       // error: return vector in string function  $[E10018]
}

default {

    state_entry(integer param) {    // state_entry does not take params         $[E10028]
        integer number = "hello";   // type mismatch, warning: shadow decl      $[E10015] $[E20001]
        int q;                      // should point out int->integer typo maybe $[E10020]
        [1] == [2];                 // warning: only compares length            $[E20011] $[E20018]
        number = number;            // warning: statement with no effect?
        str = "hi!";                // undeclared                               $[E10006]
        llSay(0, number.x);         // number is not a vector                   $[E10010]
        LLsay(0, llListToString([])); // typos; suggest llSay, llList2String    $[E10006] $[E10006]
        test(1, "hi");              // arg 2 should be vector not string        $[E10011]
        jump number;                // number is not a label                    $[E10005]
        jump label;                 // When using multiple jumps to
        jump label;                 //  one label, all but last is ignored
        @label;                     //  but that's not a warning.
        return number;              // returning a value in an event            $[E10017]
        state default;              // warning: state current acts like return  $[E20003]
                                    // warning: code is never reached?
        test();                     // too few arguments                        $[E10013]
        test(1,<1,2,3>,3);          // too many arguments                       $[E10012]
        vector z = <1,2,"3">;       // wrong type for member                    $[E10016] $[E20009]
        rotation q = <0,0,0,"3">;   // wrong type for member                    $[E10016] $[E20009]
        list boz = [<1,2,3,"4">];   // $[E20009] $[E10016]
        key k = "foo";
        llOwnerSay(k + "foo");      // key + str is not valid, even             $[E10002]
                                    // with implicit promotion between the two.
        list l = [llOwnerSay("")];  // no putting voids in lists!               $[E10036] $[E20009]
        if (llOwnerSay("")) {}      // void expr in condition                    $[E10038] $[E20007]
        while (llOwnerSay("")) {}   // void expr in condition                    $[E10038] $[E20014]
        do {} while (llOwnerSay("")); // void expr in condition                  $[E10038] $[E20014]
        for (; llOwnerSay("");) {}  // void expr in condition                    $[E10038] $[E20014]
    }

    touch_start() {                 // requires parameters                      $[E10029]
    }

    at_target(integer i, vector v, string s) { // third param should be vector  $[E10027]
    }

}
