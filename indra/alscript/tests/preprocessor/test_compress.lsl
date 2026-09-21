// preprocessor: compress
#define GAP   3
/* a block comment
   over lines */
integer   total = 0 ;   // trailing

default
{
    state_entry( )
    {
        total = total - -GAP;   total ++ ;
        vector v = < 1 , 2 , 3 > * 2.5 ;
        llSay( 0 , "kept   as   is" ) ;
        if ( total >= 2 && total <= 9 ) total <<= 1 ;
    }
}
