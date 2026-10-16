list HEX_CHARACTERS = ["0","1","2","3","4","5","6","7","8","9","a","b","c","d","e","f"];

string int2hex(integer i) {
    return llList2String(HEX_CHARACTERS, i);
}

string int2hex2(integer i) {
    return llList2String(HEX_CHARACTERS, i/16) + llList2String(HEX_CHARACTERS, i%16);
}

string int2hex4(integer i) {
    return llList2String(HEX_CHARACTERS, i          / 4096 ) +
           llList2String(HEX_CHARACTERS, (i % 4096) / 256  ) +
           llList2String(HEX_CHARACTERS, (i % 256)  / 16   ) +
           llList2String(HEX_CHARACTERS, (i % 16)          );
}

integer hex2int(string s) {
    return (integer) ("0x" + s);
}

integer ac;
assert(integer c) {
    ac++;
    if (!c) llOwnerSay((string) ac + " - FAIL");
//    else llOwnerSay((string) ac + " - PASS" );
}

default {
    state_entry() {
        assert( int2hex(6) == "6" );
        assert( int2hex(15) == "f" );
        assert( int2hex2(15) == "0f" );
        assert( int2hex2(192) == "c0" );
        assert( hex2int("f") == 15 );
        assert( hex2int("0f") == 15 );
        assert( hex2int("c0") == 192 );
        
        assert( int2hex4(256) == "0100" );
        assert( int2hex4(12345) == "3039" );        
    }
}

