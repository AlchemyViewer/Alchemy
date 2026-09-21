integer total=0;
default
{
state_entry()
{
total=total- -3;total++;
vector v=<1,2,3>*2.5;
llSay(0,"kept   as   is");
if(total>=2&&total<=9)total<<=1;
}
}
