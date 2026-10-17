// A vector off the stack that is not finite is a math error, raised in
// the middle of the instruction reading it, which goes on to put what it
// made on the heap. Here the string a cast made, which the heap
// dropped unfreed when the fault was already raised: it frees it now.
//
// expect fault: Math Error

default
{
    state_entry()
    {
        vector v = <3.0e38, 0, 0>;
        llSay(0, (string)(v * 10.0));
        llSay(0, "after");
    }
}
