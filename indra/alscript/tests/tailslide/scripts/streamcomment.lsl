/* comment A */
a() { } // $[E20009]
/* comment
   A */
b() { } // $[E20009]
/*
 *  comment B
 **/
c() { } // $[E20009]
default {
   /* comment B */
   state_entry() {
      /* comment C */
   }
}
