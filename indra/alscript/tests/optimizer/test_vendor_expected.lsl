list BUTTONS = ["Buy", "Info", "Close"];
integer listener;
string describe()
{
    return "Widget" + " for L$" + "10";
}

default
{
    state_entry()
    {
        integer i;
        llSetText(describe(), <1, 1, 1>, 1);
        while (i < 3)
        {
            ++i;
        }
    }

    touch_start(integer total)
    {
        listener = llListen(((integer)-12345), "", llDetectedKey(0), "");
        llDialog(llDetectedKey(0), describe(), BUTTONS, ((integer)-12345));
        if (total)
            llSetTimerEvent(30);
    }

    listen(integer channel, string name, key id, string message)
    {
        if (message == "Buy")
        {
            llSay(0, "Thanks!");
            return;
        }
        if (message == "Info")
            llSay(0, describe());
        llListenRemove(listener);
        llSetTimerEvent(0);
    }

    timer()
    {
        llListenRemove(listener);
        llSetTimerEvent(0);
    }
}
