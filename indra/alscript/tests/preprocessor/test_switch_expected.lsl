// preprocessor: switches
integer state_of(integer n)
{
    {if((n) == (0))jump _sw2_1;
if((n) == (1))jump _sw2_2;
if((n) == (2))jump _sw2_3;
jump _sw2_default;

        @_sw2_1;
            return 10;
        @_sw2_2;
        @_sw2_3;
        {
            integer m = n * 2;
            {if((m) == (4))jump _sw1_1;
jump _sw1_default;

                @_sw1_1; return 40;
                @_sw1_default; jump _sw1_end;
            
@_sw1_end;
}
            return m;
        }
        @_sw2_default;
            llOwnerSay("other");
            jump _sw2_end;
    
@_sw2_end;
}
    return -1;
}

default
{
    touch_start(integer total)
    {
        integer owner = llDetectedKey(0) == llGetOwner();
        {if((owner) == (TRUE))jump _sw3_1;
if((owner) == (FALSE))jump _sw3_2;
jump _sw3_end;

            @_sw3_1; llSay(0, "owner"); jump _sw3_end;
            @_sw3_2; llSay(0, "visitor");
        
@_sw3_end;
}
    }
}
