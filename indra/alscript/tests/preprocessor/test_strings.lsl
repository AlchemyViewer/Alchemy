// What must not change: strings, comments, labels, vectors; and what must.
#define X 1
#define GREET "hello"
string multi = "line one
line two X
line three";
string plain = "X GREET";
// X GREET
/* X
   GREET */
vector v = <X, X, X>;
rotation r = <0, 0, 0, X>;

default
{
    state_entry()
    {
        integer i = X;
        @top;
        if (i < X + 2) { ++i; jump top; }
        llSay(0, GREET + multi + plain);
    }
}
