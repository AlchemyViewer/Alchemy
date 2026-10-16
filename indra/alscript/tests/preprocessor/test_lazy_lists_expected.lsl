list lazy_list_set(list L, integer i, list v)
{
    while (llGetListLength(L) < i)
        L = L + 0;
    return llListReplaceList(L, v, i, i);
}

// preprocessor: lazylists
list settings;
list names = ["a", "b"];

default
{
    state_entry()
    {
        settings = lazy_list_set(settings,0,[5]);
        settings = lazy_list_set(settings,1,["text"]);
        settings = lazy_list_set(settings,2,[llList2Integer(settings, 0) + 1]);
        integer i = llList2Integer(settings, 0);
        string s = llList2String(settings, 1);
        key k = llList2Key(settings, 3);
        vector v = llList2Vector(settings, llList2Integer(settings, 0));
        list rest = llList2List(settings, 2);
        llSay(0, (string)llGetListLength(names) + s + (string)i + (string)k + (string)v + llList2CSV(rest));
        if (llGetListLength(settings) > 2) settings = lazy_list_set(settings,4,[llGetKey()]);
    }
}
