// What must not change: strings, comments, labels, vectors; and what must.


string multi = "line one\nline two X\nline three"

;
string plain = "X GREET";
// X GREET
/* X
   GREET */
vector v = <1, 1, 1>;
rotation r = <0, 0, 0, 1>;

default
{
    state_entry()
    {
        integer i = 1;
        @top;
        if (i < 1 + 2) { ++i; jump top; }
        llSay(0, "hello" + multi + plain);
    }
}
