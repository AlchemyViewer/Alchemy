default {
    state_entry() {
        llOwnerSay((string)(-0x80000000));
        llOwnerSay((string)(-(-2147483648)));
        llOwnerSay((string)(-(2147483648)));
        llOwnerSay((string)(-2147483648));
        // literals past 0xFFFFFFFF saturate to -1 rather than wrapping
        llOwnerSay((string)4294967295);
        llOwnerSay((string)4294967296);
        llOwnerSay((string)0xFFFFFFFF);
        llOwnerSay((string)0x100000000);
        llOwnerSay((string)99999999999999999999);
    }
}
