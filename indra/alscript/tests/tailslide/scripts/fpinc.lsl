default {
   state_entry() {
      float x;

      x++;
      x--;

      vector v;
      llOwnerSay((string)v.x++);
      print((string)(++v.x)); // ONLY valid with parens around it because of LSL's stupid grammar.
   }
}
