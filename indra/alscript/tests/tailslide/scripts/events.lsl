boy(integer girl) {} // $[E20009]


state_entry() { // $[E10031] $[E20009]
}

default {
   state_entry() {
      boy("foo"); // $[E10011]
   }
   on_rez(integer paramie) {
   }
   on_rez(string paramie) { // $[E10033] $[E10027]
   }
   object_rez(float s) { // $[E10027]
   }
   listen(integer channel) { // $[E10029]
   }
   changed(integer changed, integer extra) { // $[E10028] $[E10031]
   }
   foo() { // $[E10030]
   }
   bar(integer baz) { // $[E10030]
   }
}
