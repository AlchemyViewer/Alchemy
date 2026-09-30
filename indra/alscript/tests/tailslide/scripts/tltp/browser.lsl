// TLTP Browser
// Version 0.37f

// Supports: TLTP v0.3

// J & j commands are still being perfected
// they are not supported yet by TLML (but will be shortly)

integer debug = 0;

// constants
key nullk = NULL_KEY;
vector zerov = ZERO_VECTOR;
integer tlml_code = 1000;
integer url_code = 2000;
integer email_code = 1100;
integer xtm_code = 3000;
integer xtmp_code = 3100;
integer clear_code = 8999;
integer rpc_code = 4400;
integer tn_go = 5200;
integer joint_code = 6000;

string tltp_str = "TLTP";        // for email comm
float max_sleep = 10.0;            // for the -97 code

// info about current pages
list current_url;            // maintained into full URL format
list history;
integer history_size = 12;        // max URLs in history
key source;                // if set to null key, listen anyone, else only listen this key

// info about browser
integer is_on;                // browser's display is visible, and listening to adverts
integer browsing_channel;        // channel for receiving answers if chat
integer browsing_handle;        // handle for receiving answers if chat
integer advert_channel = -9;
integer advert_handle;
integer channel;            // channel for menu
integer handle;                // handle for menu
integer action;                // selected menu option
float discovery_timeout = 10.0;        // time before forgetting a server

// info about available servers
list advertised_names;
list advertised_urls;
list bookmarked_names;
list bookmarked_urls;
integer bkm_part;            // slice of the bookmarks to display
integer adv_part;            // slice of the adverts to display

// colors
vector color_flash = <1,0,0>;
vector color_on = <1,1,0>;
vector color_off = <0.2, 0.2, 0.2>;

// info about inventory reading
key query;
integer line;
string anim;
key page_key;                // remote notecard key
key page_query;                // for reading remote notecards
integer page_line;
key joint_page_key;            // remote joint notecard key
key joint_page_query;            // for reading remote joint notecards
integer joint_page_line;

// info about display
integer link_offset = 2;        // link number of first display prim
integer max_prim_used;            // highest index of display prim in use
integer emailer;            // index of unused display prim for email sending
integer status;             // UNUSED: $[E20009]

// courtesy of Strife Onizuka
list TightListParse(string a)
{
    string b = llGetSubString(a,0,0);//save memory
    return llParseStringKeepNulls(llDeleteSubString(a,0,0), [b],[]);
}
string TightListDump(list a, string b)
{     string c = (string)a;
    if(llStringLength(b)==1)
        if(llSubStringIndex(c,b) == -1)
            jump end;
    integer d = -llStringLength(b += "|\\/?!@#$%^&*()_=:;~{}[],\n\" qQxXzZ");
    while(1+llSubStringIndex(c,llGetSubString(b,d,d)) && d)
        ++d;
    b = llGetSubString(b,d,d);
    @end;
    c = "";//save memory
    return b + llDumpList2String(a, b);
}

go(string url)
{
    if(debug)  // ALWAYS FALSE: $[E20013]
        llOwnerSay(url);
    list t = TightListParse(url) + current_url;
    integer argc = llGetListLength(t);
    if (argc < 5) { llOwnerSay("Missing arguments in URL: " + url); return;    } // sanity check

    // Managing the history
    if (url == llList2String(history, 0))
    {
        // We're getting back in the history
        if (llGetListLength(history) > 1)
        {
            history = llList2List(history, 1, -1);
        } else history = [];
    } else
        history = TightListDump(current_url, "!") + history;

    // Limiting the size of history
    if (llGetListLength(history) > history_size) history = llList2List(history, 0, history_size - 1);

    // extracting info from the URL
    current_url = llDeleteSubList(t, -4, argc - 9);
    request(current_url);
}

request(list u)
{
    if (llGetListLength(u) != 4) { llOwnerSay("Bad URL format"); return; }

    llSetText("Browsing " + llList2String(u, 3), <0,1,1>, 0.8);

    if (llList2String(u, 1) == "0")
    {
        // requesting a page to a chat server

        if (llList2String(u, 2) == "0")
        {
            llOwnerSay("Bad server channel.");
            return;
        }

        source = "";

        llListenRemove(browsing_handle);
        do;while(!(browsing_channel = (integer)llFrand(65536.0) | ((integer)llFrand(65536.0) << 16))); // $[E20014]
        browsing_handle = llListen(browsing_channel, "", "", "");

        // sending out an URL command to the server pointing back to us
        // in the future the comm method included here might depend on things like distance to the server
        // object name is optional and only included here because of the specifications
        // All Hail The Holy Specifications !
        llShout((integer)llList2String(u, 2), "U" + TightListDump([llList2String(u, 0), "0", (string)browsing_channel, llGetObjectName()], "&"));

    } else if (llList2String(u, 1) == "1")
    {
        // requesting a page to an email server

        source = "";

        // using the last unused display prim as remailer
        if (emailer <= max_prim_used + link_offset) emailer = llGetNumberOfPrims();

        // same as above, name is included for following the specifications
        // All Hail The Holy Specifications !
        llMessageLinked(emailer, email_code, TightListDump([emailer, llList2String(u, 2), tltp_str], ";"), (key)("U" + TightListDump([llList2String(u, 0), "1", llGetKey(), llGetObjectName()], "&") ) );
        emailer = emailer - 1;
    } else if (llList2String(u, 1) == "2")
    {
        // read a joint notecard page
        // why is this fed though the url parser you might ask? it makes things too complicated otherwise.
        joint_page_key = llList2String(u, 2);
        joint_page_line = (integer)llList2String(u, 0);
        joint_page_query = llGetNotecardLine(joint_page_key, joint_page_line);
    } else {
        llOwnerSay("Unknown transport method for URL: " + llList2CSV(u));
        return;
    }
}

parse(string command, string hook)
{
    string a = llGetSubString(command, 0, 0);
    command = llDeleteSubString(command, 0, 0);

    if (a == "U")
    {
        // received URL
        llOwnerSay("Redirected.");
        go(command);
    } else if (a == "T") {
        // received TLML
        tlml(command);
    } else if (a == "t") {
        // received TLML key
        page_key = command;
        page_line = 0;
        page_query = llGetNotecardLine(page_key, page_line);
    } else if (a == "J") {
        llMessageLinked(LINK_SET, joint_code, command, hook);
    } else if (a == "x" || a == "X") {
        // received XTM or XTMP
        // the parameters should change soon
        integer c = (a == "x") * xtm_code + (a == "X") * xtmp_code;
        llMessageLinked(LINK_SET, c, command, (key)TightListDump([llList2String(current_url, 2), llList2String(current_url, 0)], "*") );
    } else if (a == "A") {
        // received animation key
        anim = command;
        llRequestPermissions(llGetOwner(), PERMISSION_TRIGGER_ANIMATION);
    } else if (a == "S") {
        // received sound key
        list l = TightListParse(command);
        if (llGetListLength(l) < 2) { llPlaySound(llList2String(l, 0), 0.5); } else
            llPlaySound(llList2String(l, 0), (float)llList2String(l, 1));
    } else if (a == "R") {
        // received RPC command
        list rpc = TightListParse(command);
        llMessageLinked(LINK_THIS, rpc_code, llList2String(rpc, 0), (key)TightListDump(llDeleteSubList(rpc, 0, 0), "|"));
    } else if (a == "C") {
        // received chat request
        list chat = TightListParse(command);
        if (llGetListLength(chat) > 1)
        {
            llShout((integer)llList2String(chat, 0), llList2String(chat, 1));
        } else llSay(0, llList2String(chat, 0));
    } else if (a == "c") {
        // received owner chat request
        llOwnerSay(command);
    } else if (a == "W") {
        // received sleep request
        if ((float)command > max_sleep) { llSleep(max_sleep); } else
            llSleep((float)command);
    } else if (a == "N") {
        // texture notecard go
        list com = TightListParse(command);
        llMessageLinked(LINK_SET, tn_go, llList2String(com,0), TightListDump(llDeleteSubList(com, 0, 0), "*"));
    }
}

tlml(string t)
{
    // extracting the target display prim number
    integer ind = llSubStringIndex(llDeleteSubString(t, 0, 0), llGetSubString(t, 0, 0));
    integer target = (integer)llGetSubString(t, 1, ind);

    if (target >= 0) {
        // sending the TLML to the display
        if (target > llGetNumberOfPrims() - link_offset)
        {
            llOwnerSay("Not enough prims to display this page.");
            return;
        }
        if (target > max_prim_used) max_prim_used = target;
        llMessageLinked(target + link_offset, tlml_code, llDeleteSubString(t, 0, ind), (key)TightListDump([TightListDump(current_url, "#"), llGetLocalRot(), llList2String(current_url, 2)], "&"));
    } else {
        if (target == -99) { clear(); } else
        if (target == -1) { llOwnerSay("Server is unavailable !"); } else
        if (target == -2) { llOwnerSay("Server error !"); } else
        if (target == -3) { llOwnerSay("Access denied !"); } else
        if (target == -4) { llOwnerSay("Page not found !"); } else
        if (target == -97)
        {
            float delay = (float)llDeleteSubString(t, 0, ind);
            if (delay > max_sleep) delay = max_sleep;
            llSleep(delay);
        } else if (target != -98) {
            llOwnerSay("Server response not recognized: " + t);
        }
    }
}

clear()
{
    integer n;  // UNUSED: $[E20009]
    llMessageLinked(LINK_ALL_OTHERS, clear_code, "nevermind", nullk);
    max_prim_used = 0;
}

on()
{
    is_on = TRUE;
    llSetColor(color_on, ALL_SIDES);
    llListenRemove(advert_handle);
    advert_handle == llListen(advert_channel, "", "", ""); // eq as statement $[E20018]
    integer n;
    for(n = 0; n<max_prim_used; ++n) llSetLinkAlpha(n + link_offset, 1.0, ALL_SIDES);
    llSetTimerEvent(5.0);
}

off()
{
    is_on = FALSE;
    llSetColor(color_off, ALL_SIDES);
    llSetText("", zerov, 0.0);
    llListenRemove(advert_handle);
    llListenRemove(browsing_handle);
    advertised_urls = [];
    advertised_names = [];
    source = "";
    browsing_handle = 0;
    page_query = nullk;
    query = nullk;
    joint_page_query = nullk;    // avoiding a strange infinite loop that shouldn't be possible (no one can predict a key?)
    integer n;
    for(n = 0; n<max_prim_used; ++n) llSetLinkAlpha(n + link_offset, 0.0, ALL_SIDES);
    llSetTimerEvent(0.0);
}

config(string msg)
{
    if (action == 1)
    {
        // from bookmarks menu
        if (msg == "Back")
        {
            action = 0;
            menu();
        } else if (msg == "More")
        {
            bkm_part = (bkm_part + 1) % llGetListLength(bookmarked_names);
            menu();
        } else {
            integer i = llListFindList(bookmarked_names, [msg]);
            if (i < 0) { llOwnerSay("Bookmark not found."); } else {
                go(llList2String(bookmarked_urls, i));
            }
            llListenRemove(handle);
        }
    } else if (action == 2)
    {
        // from adverts menu
        if (msg == "Back")
        {
            action = 0;
            menu();
        } else if (msg == "More")
        {
            adv_part = (adv_part + 1) % llGetListLength(advertised_names);
            menu();
        } else {
            integer i = llListFindList(advertised_names, [msg]);
            if (i < 0) { llOwnerSay("Server not found."); } else {
                go(llList2String(advertised_urls, i));
            }
            llListenRemove(handle);
        }
    } else {
        if (msg == "Bookmarks")
        {
            action = 1;
            menu();
            return;
        }
        if (msg == "Connect")
        {
            action = 2;
            menu();
            return;
        }
        if (msg == "Reload")
        {
            request(current_url);
        }
        if (msg == "< Back")
        {
            if (llGetListLength(history) > 1)
            {
                go(llList2String(history, 0));
            } else llOwnerSay("Can't go further back.");
        } else if (msg == "On")
        {
            on();
        } else if (msg == "Off")
        {
            off();
        } else if (msg == "Close") {
            llListenRemove(browsing_handle);
            browsing_handle = 0;
            source = "";
            llSetText("", zerov, 0.0);
            clear();
        }
        llListenRemove(handle);
    }
}

menu()
{
    string opt;
    list btns;
    if (action == 0)
    {
        // main menu
        opt = "Detected servers: " + (string)llGetListLength(advertised_urls);
        if (is_on)
        {
            btns = ["Off"];
        } else {
            btns = ["On"];
        }
        btns += ["ExitMenu"];
        if (llGetListLength(history) > 1) btns += ["< Back"];
        if (llGetListLength(bookmarked_names) > 0) btns += ["Bookmarks"];
        if (llGetListLength(advertised_urls) > 0) btns += ["Connect"];
        if (llStringLength(source))
            btns += ["Close", "Reload"];
    } else if (action == 1) {
        // display bookmarks
        opt = "Connect to:";
        if (llGetListLength(bookmarked_names) < 12)
        {
            btns = bookmarked_names + ["Back"];
        } else {
            btns = llList2List(bookmarked_names, bkm_part * 10, bkm_part * 10 + 9) + ["More", "Back"];
        }
    } else {
        // display adverts
        opt = "Connect to:";
        if (llGetListLength(advertised_names) < 12)
        {
            btns = advertised_names + ["Back"];
        } else {
            btns = llList2List(advertised_names, adv_part * 10, adv_part * 10 + 9) + ["More", "Back"];
        }
    }
    llDialog(llGetOwner(), opt, btns, channel);
}

default
{
    state_entry()
    {
        on();
        browsing_handle = 0;
        bkm_part = 0;
        adv_part = 0;
        current_url = ["", "", "", ""];
        llSetText("", zerov, 0.0);
    }

    changed(integer c)
    {
        if (c & CHANGED_INVENTORY)
        {
            if (llGetInventoryNumber(INVENTORY_NOTECARD) > 0)
            {
                line = 0;
                llOwnerSay("Adding bookmarks...");
                query = llGetNotecardLine(llGetInventoryName(INVENTORY_NOTECARD, 0), line);
            }
        }
    }

    dataserver(key id, string data)
    {
        if (id == query)
        {
            if (data != EOF)
            {
                // adding a bookmark from notecard
                if (llGetSubString(data, 0, 0) != "U") return;
                list l = TightListParse(llDeleteSubString(data, 0, 0));
                if (llGetListLength(l) != 4)
                {
                    llOwnerSay("Bad bookmark skipped: " + data);
                } else {
                    if (llListFindList(bookmarked_urls, [data]) < 0)
                    {
                        string name_to_bkm = llList2String(l, 3);
                        if (llStringLength(name_to_bkm) > 24)
                            name_to_bkm = llGetSubString(name_to_bkm, 0, 23);
                        bookmarked_names += [name_to_bkm];
                        bookmarked_urls += [llDeleteSubString(data, 0, 0)];
                    } else llOwnerSay("Redundant bookmark skipped: " + data);
                }
                line = line + 1;
                query = llGetNotecardLine(llGetInventoryName(INVENTORY_NOTECARD, 0), line);
            } else {
                llRemoveInventory(llGetInventoryName(INVENTORY_NOTECARD, 0));
                if (llGetInventoryNumber(INVENTORY_NOTECARD) > 0)
                {
                    line = 0;
                    llOwnerSay("Adding more bookmarks...");
                    query = llGetNotecardLine(llGetInventoryName(INVENTORY_NOTECARD, 0), line);
                } else llOwnerSay("Done adding bookmarks.");
            }
        } else if (id == page_query)
        {
            // reading of remote notecards
            if (data != EOF)
            {
                parse(data, "");
                page_line = page_line + 1;
                page_query = llGetNotecardLine(page_key, page_line);
            }
        } else if (id == joint_page_query)
        {
            // reading of remote joint notecards
            llMessageLinked(LINK_ALL_OTHERS, joint_code, data, (key)TightListDump([TightListDump([joint_page_line, 2, joint_page_key, llList2String(current_url,3)], "#"), llGetLocalRot(), joint_page_key], "&"));
        }
    }

    timer()
    {//omg this is so wrong this must be rewriten.
        if (llGetTime() > discovery_timeout)
        {
            advertised_names = [];
            advertised_urls = [];
            llResetTime();
            llSetColor(color_on, ALL_SIDES);
        }
        llGetNextEmail("", "");
    }

    run_time_permissions(integer p)
    {
        if (p & PERMISSION_TRIGGER_ANIMATION) llStartAnimation(anim);
    }

    touch_start(integer c)
    {
        if (llDetectedKey(0) != llGetOwner()) return;
        if ((llGetPermissions() & PERMISSION_TRIGGER_ANIMATION) && (anim != ""))
        {
            llStopAnimation(anim);
            anim = "";
        }
        llStopSound();
        action = 0;
        adv_part = 0;
        bkm_part = 0;
        llListenRemove(handle);
        channel = (integer)llFrand(500000000) - 800000000;
        handle = llListen(channel, "", llGetOwner(), "");
        llResetTime();
        menu();
    }

    email(string time, string address, string subj, string message, integer left)
    {
        integer n = 0;
        if (debug) llOwnerSay("Received email: " + subj + ": " + message);  // always false: $[E20013]
        if (subj == tltp_str)
        {
            n = llSubStringIndex(message, "\n\n");
            message = llDeleteSubString(message, 0, n  + 1);
            source = (key)llGetSubString(address, 0, llStringLength(message) - 19);
            if(llGetSubString(address,-20, -1) == "@lsl.secondlife.com")
            {
                current_url = llListReplaceList(current_url, [(string)source], 2, 2);
            }
            else
                current_url = llListReplaceList(current_url, [address], 2, 2);

            list commands = llParseStringKeepNulls(message, ["\n"], []);

            integer m = llGetListLength(commands);
            for(n = 0; n < m; ++n)
                parse(llList2String(commands, n), "");

            if (left > 0) llGetNextEmail("", "");
        }
    }

    listen(integer chan, string name, key id, string msg)
    {
        if (chan == channel)
        {
            // configuration command received
            llResetTime();
            config(msg);
        } else if (chan == advert_channel)
        {
            // heard a server advert
            llResetTime();
            if (llGetSubString(msg, 0, 0) == "U")    // only URLs
            {
                if (llListFindList(advertised_urls, [llDeleteSubString(msg, 0, 0)]) == -1)// only URLs we don't have
                {
                    list temp = TightListParse(llDeleteSubString(msg, 0, 0));
                    if (llGetListLength(temp) >= 4) // skip incomplete URLs
                    {
                        llSetColor(color_flash, ALL_SIDES);
                        string na = llList2String(temp, 3);
                        if (llStringLength(na) > 24)
                            na = llGetSubString(na, 0, 23);
                        llOwnerSay("Found server: " + na);
                        advertised_names += [na];
                        advertised_urls += [llDeleteSubString(msg, 0, 0)];
                    }
                    else if(debug)  // always false: $[E20013]
                        llOwnerSay("Incomplete broadcasted URL");
                }
            }
        } else {
            llResetTime();
            if (source)
            {
                if (source == id)
                    parse(msg, "");
            } else {
                source = id;
                parse(msg, "");
            }
        }
    }

    link_message(integer part, integer code, string msg, key id)
    {
        if (code == url_code)
        {
            parse(msg, id);
        }
    }
}

