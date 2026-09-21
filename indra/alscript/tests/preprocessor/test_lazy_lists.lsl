// preprocessor: lazylists
list settings;
list names = ["a", "b"];

default
{
    state_entry()
    {
        settings[0] = 5;
        settings[1] = "text";
        settings[2] = (integer)settings[0] + 1;
        integer i = (integer)settings[0];
        string s = (string)settings[1];
        key k = (key)(settings[3]);
        vector v = (vector)settings[(integer)settings[0]];
        list rest = (list)settings[2];
        llSay(0, (string)llGetListLength(names) + s + (string)i + (string)k + (string)v + llList2CSV(rest));
        if (llGetListLength(settings) > 2) settings[4] = llGetKey();
    }
}
