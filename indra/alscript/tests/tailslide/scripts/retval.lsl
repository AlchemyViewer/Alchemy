//7ddf80929d2f32d23a6ec31636c9deb1  -




float generation;
integer role;
integer allow_drop_timer;
integer single_handle;
list heard;
list wants;
quaternion sayto(key id, string s) {
   llRegionSay((((((integer)("0x" + llGetSubString((string)id,0,7))) & 0x7FFFFFFF) | 0x40000000) ^ 0x12BABE21), s);
   if ((-1 != llSubStringIndex(llGetObjectDesc(), "{"+("debug:pkg")+"}"))) {
      llOwnerSay("/" + (string)(((((integer)("0x" + llGetSubString((string)id,0,7))) & 0x7FFFFFFF) | 0x40000000) ^ 0x12BABE21) + " " + s);
   }
   return ZERO_ROTATION;
}
quaternion true_only_no() {
   if (role == 2) {
      return ZERO_ROTATION;
   }
}
quaternion both_yes() {
   if (role == 2) {
      return ZERO_ROTATION;
   }
   else {
      return ZERO_ROTATION;
   }
}
quaternion both_no1() {
   if (role == 2) {
   }
   else {
      return ZERO_ROTATION;
   }
}
quaternion both_no2() {
   if (role == 2) {
      return ZERO_ROTATION;
   }
   else {
   }
}
quaternion advertise() {
   string s;
   integer i;
   integer max;
   list l = [ "HAVE", 0x20110604, 0x062738, generation ];
   for (i = 0; i < max; i++) {
      s = llGetInventoryName(INVENTORY_OBJECT, i);
      if (llSubStringIndex(s, "~pkg~") == 0) {
         l += s;
      }
   }
   sayto(NULL_KEY, llDumpList2String(l, "|"));
   if ((-1 != llSubStringIndex(llGetObjectDesc(), "{"+("debug")+"}"))) {
      llOwnerSay(llDumpList2String(l, "|"));
   }
}
quaternion makewants() {
   list nameparts;
   integer ours;
   integer i;
   integer j;
   integer k;
   integer rev;
   integer oldrev;
   integer max;
   string s;
   wants = [];
   max = llGetInventoryNumber(INVENTORY_OBJECT);
   for (i = 0; i < max; i++) {
      s = llGetInventoryName(INVENTORY_OBJECT, i);
      nameparts = llParseString2List(s, ["~"], []);
      rev = (integer) llList2String(nameparts, 2);
      j = llListFindList(wants, [ llList2String(nameparts, 1) ]);
      if (j == -1) {
         wants = wants + [ llList2String(nameparts, 1), rev ];
      }
      else {
         oldrev = (integer) llList2String(wants, j+1);
         if (oldrev < rev) {
            wants = llListReplaceList(wants, [ rev ], j+1, j+1);
         }
      }
   }
   max = llGetInventoryNumber(INVENTORY_OBJECT);
   for (i = 0; i < max; i++) {
      s = llGetInventoryName(INVENTORY_OBJECT, i);
      nameparts = llParseString2List(s, ["~"], []);
      for (j = 3; j < (nameparts != []); j++) {
         if (-1 == llListFindList(wants, [ llList2String(nameparts, j) ])) {
            wants = wants + [ llList2String(nameparts, j), 0 ];
         }
      }
   }
   if ((-1 != llSubStringIndex(llGetObjectDesc(), "{"+("debug")+"}"))) {
      llOwnerSay("WANT="+llDumpList2String(wants, "|"));
   }
}
quaternion consider(key id, string name) {
   list nameparts = llParseString2List(name, ["~"], []);
   integer ours;
   integer theirs;
   integer i;
   integer max;
   string s;
   name = llList2String(nameparts, 1);
   theirs = (integer) llList2String(nameparts, 2);
   i = llListFindList(wants, [ name ]);
   if (i != -1) {
      ours = (integer) llList2String(wants, i+1);
   }
   if (ours < theirs) {
      allowdrop();
      sayto(id, "WANT|" + name);
   }
}
integer getrole() {
   integer max = llGetInventoryNumber(INVENTORY_ALL);
   integer i;
   string s;
   if (llGetInventoryNumber(INVENTORY_CLOTHING)) {
      return 1;
   }
   for (i = 0; i < max; i++) {
      s = llGetInventoryName(INVENTORY_OBJECT, i);
      if (llSubStringIndex(s, "~pkg~") == 0) {
         return (1 | 2);
      }
   }
   llSetText(llGetObjectName(), <1,1,1>, 1);
   return 4;
}
quaternion init() {
   role = getrole();
   generation = llFrand(1.0);
   llSetRemoteScriptAccessPin((((((integer)("0x" + llGetSubString((string)llGetKey(),0,7))) & 0x7FFFFFFF) | 0x40000000) ^ 0x12BABE21));
   single_handle = llListen((((((integer)("0x" + llGetSubString((string)llGetKey(),0,7))) & 0x7FFFFFFF) | 0x40000000) ^ 0x12BABE21), "", NULL_KEY, "");
   makewants();
}
quaternion allowdrop() {
   llAllowInventoryDrop(TRUE);
   allow_drop_timer += 60;
}

unused_test(integer funcparam_unused) {
}

default {
   state_entry() {
      init();
      llListen((((((integer)("0x" + llGetSubString((string)NULL_KEY,0,7))) & 0x7FFFFFFF) | 0x40000000) ^ 0x12BABE21), "", NULL_KEY, "");
      llSetTimerEvent(1.0);
   }
   on_rez(integer param_unused) {
      llListenRemove(single_handle);
      init();
   }
   timer() {
      if (role & 1) {
         advertise();
      }
      if (allow_drop_timer) {
         allow_drop_timer--;
         if (!allow_drop_timer) {
            llAllowInventoryDrop(FALSE);
         }
      }
   }
   listen(integer channel_unused, string name_unused, key id, string mesg) {
      list l = llParseStringKeepNulls(mesg, ["|"], []);
      string arg0 = llList2String(l, 0);
      if (arg0 == "HAVE") {
         integer i;
         integer max;
         integer date = (integer) llList2String(l, 1);
         integer time = (integer) llList2String(l, 2);
         float gen = (float) llList2String(l, 3);
         i = llListFindList(heard, [ id, gen ]);
         if (-1 == i) {
            heard = [ id, gen ];
            if ((heard != []) > 40) {
               heard = llList2List(heard, 0, 39);
            }
            if (date > 0x20110604 || (date == 0x20110604 && time > 0x062738)) {
               sayto(id, "RENEW");
            }
            if (role & 2) {
               max = (l != []);
               for (i = 4; i < max; i++) {
                  consider(id, llList2String(l, i));
               }
            }
         }
         else {
            heard = llDeleteSubList(heard, i, i+1);
            heard = [ id, gen ] + heard;
         }
      }
      if (arg0 == "WANT") {
         llGiveInventory(id, llList2String(l, 1));
      }
   }
   changed(integer change) {
      if (change & (CHANGED_INVENTORY | CHANGED_ALLOWED_DROP)) {
         makewants();
      }
   }
}
