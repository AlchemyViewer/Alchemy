// TLTP Server
// version 0.32c
// Author: Jesrad Seraph
// Modify and redistribute freely as long as you allow free modification and redistribution

// This example server supports both chat and email transport methods
// The pages are cached in lists at startup, for better performance

// Currently, the default page is the first page cached.

// TODO: support crossing the transport methods (listen->email, email->shout)

integer cur_emailer = 0;
integer max_emailer;

list tlmlPages;
list pages;            // contains the TLML code for each page, simply concatenated line
                // after line and page after page
list indexes;            // contains each page index, following the same order as pages above

list offsets;            // contains the starting position of each page in the pages list
list sizes;            // contains the number of entries of each page in the pages list

integer page;            // inventory number of page being read
string name;            // name of page being read
integer line;            // line being asked
key query;            // dataserver request

integer channel = 777;        // private server channel for accepting requests
integer handle;            // listener handle for getting requests
float advert_rate = 5.0;    // timerate for announcing the server
string tltp_str = "TLTP";    // saving memory
string eof = EOF;

// courtesy of Strife Onizuka
list TightListParse(string a)
{
    string b = llGetSubString(a,0,0);//save memory
    return llParseStringKeepNulls(llDeleteSubString(a,0,0), [b],[]);
}
string TightListDump(list a, string b)
{

    string c = (string)a;
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

sendEmail(string a, string s, string m)
{
    if (max_emailer > 0)
    {
        llMessageLinked(LINK_THIS, cur_emailer, TightListDump([a + "@lsl.secondlife.com", s, m], "!"), "");
        cur_emailer = (cur_emailer + 1) % max_emailer;
    } else llEmail(a + "@lsl.secondlife.com", s, m);
}

default
{
    state_entry()
    {
        llOwnerSay("Caching pages...");
        pages = [];
        indexes = [];
        offsets = [];
        sizes = [];
        page = 0;
        line = 0;
        if (llGetInventoryNumber(INVENTORY_NOTECARD) > 0)
        {
            name = llGetInventoryName(INVENTORY_NOTECARD, 0);
            llOwnerSay("Caching page " + name);
            query = llGetNotecardLine(name, line);
        } else llOwnerSay("No page found.");
    }

    touch_start(integer c)
    {
        // Owner can reset the script during the caching
        if (llDetectedKey(0) != llGetOwner()) return;
        llResetScript();
    }

    dataserver(key id, string data)
    {
        if (query == id)
        {
            if (data != eof)
            {
                if(line == 0 && llGetSubString(data,-6,-1) == "TLML-L")
                {
                    data = eof + eof;//hack
                } else {
                    pages += [data];
                    line = line + 1;
                    query = llGetNotecardLine(name, line);
                }
            }
            if (llSubStringIndex(data, eof) + 1)
            {
                if(data == eof + eof)
                    tlmlPages += [name];
                else
                {
                    indexes += [name];
                    sizes += [line];
                    offsets += [llGetListLength(pages) - line];
                }
                page = page + 1;
                if (page < llGetInventoryNumber(INVENTORY_NOTECARD))
                {
                    // Caching the next page
                    line = 0;
                    name = llGetInventoryName(INVENTORY_NOTECARD, page);
                    llOwnerSay("Caching page " + name);
                    query = llGetNotecardLine(name, line);
                } else {
                    // Done caching
                    llOwnerSay("Caching ended.");
                    state ready;
                }
            }
        }
    }
    changed(integer c)
    {
        if (c & CHANGED_INVENTORY) llResetScript();
    }
}

// the global variable 'name' is reused in this state
state ready
{
    state_entry()
    {
        max_emailer = llGetInventoryNumber(INVENTORY_SCRIPT) - 1;
        handle = llListen(channel, "", "", "");
        llOwnerSay("Server ready on channel " + (string)channel);
        llSetTimerEvent(advert_rate);
        llOwnerSay((string)llGetKey() + "@lsl.secondlife.com");
        if (llGetObjectDesc() == "") llSetObjectDesc("Server");
    }

    changed(integer c)
    {
        if (c & CHANGED_INVENTORY) llResetScript();
    }

    timer()
    {
        llSay(-9, "U" + TightListDump([llList2String(indexes + tlmlPages, 0), 0, channel, llGetObjectDesc()], "|") );
        llGetNextEmail("", "");
    }

    listen(integer ch, string n, key id, string msg)
    {
//        llOwnerSay(llList2CSV([ch,n,id,msg]));
        string a = llGetSubString(msg, 0, 0);
        if (a != "U") return;
        list info = TightListParse(llDeleteSubString(msg, 0, 0));
        if (llGetListLength(info) < 3) return;    // simple sanity check

        string req = llList2String(info, 0);
        integer i = llListFindList(indexes, [req]);
        string m = llList2String(info, 1);
        integer c = (integer)llList2String(info, 2);

        if (m != "0") return;    // not supporting cross transport methods right now

        if (i < 0)
        {
            i = llListFindList(tlmlPages, [req]);
            if (i < 0)
            {
                // page is not a notecard
                integer type = llGetInventoryType(req);
                if (type == INVENTORY_NONE) { llShout(c, "T" + TightListDump(["-4", llList2String(indexes, 0)], "!")); } else
                if (type == INVENTORY_NOTECARD) { llShout(c, "T!-2"); llResetScript(); } else
                if (type == INVENTORY_ANIMATION) { llShout(c, "A" + (string)llGetInventoryKey(req)); } else
                if (type == INVENTORY_SOUND) { llShout(c, "S" + TightListDump([(string)llGetInventoryKey(req), "1.0"], "!")); } else
                if (type == INVENTORY_SCRIPT) { llShout(c, "T" + TightListDump(["-4", llList2String(indexes, 0)], "!")); } else
                {
                    llShout(c, "cDownloading " + req);
                    if((n = llGetOwnerKey(id)) != id)
                        llGiveInventory(n, req);
                    else
                    {
                        llShout(c, "cCannot resolve your key ! Please come to sim secondlife://" + llGetRegionName());
                    }
                }
            }
            else
            {
                llShout(c, "U"+TightListDump([0,2,llGetInventoryKey(req),(string)llGetKey()+"|"+req],"/") );
            }
        } else {
            integer n;  // shadow decl: $[E20001]
            integer max = llList2Integer(sizes, i);
            integer o = llList2Integer(offsets, i);
            for(n=0; n<max; n = n + 1)
            {
                llShout(c, llList2String(pages, n + o) );
            }
        }
    }

    email(string time, string address, string subj, string message, integer left)
    {
        if (subj == tltp_str)
        {
            integer start = llSubStringIndex(message, "\n\n") + 2;
            string a = llGetSubString(message, start, start);
            if (a != "U") return;

            list info = TightListParse(llDeleteSubString(message, 0, start));
            name = llList2String(info, 0);

            // add a check for cross transport method here

            integer i = llListFindList(indexes, [name]);
            if (i < 0)
            {
                i = llListFindList(tlmlPages, [name]);
                if (i < 0)
                {
                    // page is not a notecard
                    integer type = llGetInventoryType(name);
                    if (type == INVENTORY_NONE) { sendEmail(llList2String(info, 2), tltp_str, "T" + TightListDump(["-4", llList2String(indexes, 0)], "!")); } else
                    if (type == INVENTORY_NOTECARD) { sendEmail(llList2String(info, 2), tltp_str, "T!-2"); llResetScript(); } else
                    if (type == INVENTORY_ANIMATION) { sendEmail(llList2String(info, 2), tltp_str, "A" + TightListDump([llGetInventoryKey(name)], "!")); } else
                    if (type == INVENTORY_SOUND) { sendEmail(llList2String(info, 2), tltp_str, "S" + TightListDump([llGetInventoryKey(name), "1.0"], "!")); } else
                    if (type == INVENTORY_SCRIPT) { sendEmail(llList2String(info, 2), tltp_str, "T" + TightListDump(["-4", llList2String(indexes, 0)], "!")); } else
                    {
                        sendEmail(llList2String(info, 2), tltp_str, "cDownloading " + name);
                        string id = llGetOwnerKey(llGetSubString(message, 0, 35));
                        if (id != llGetSubString(message, 0, 35)) {
                            llGiveInventory(id, name);
                        } else sendEmail(llList2String(info, 2), tltp_str, "cCannot resolve your key ! Please come to sim secondlife://" + llGetRegionName());
                    }
                }
                else
                {
                    sendEmail(llList2String(info, 2), tltp_str, "U"+TightListDump([0,2,llGetInventoryKey(name),(string)llGetKey()+"|"+name],"/"));
                }
            } else {
                // sending the page
                string data;
                integer n;
                integer l = llList2Integer(sizes, i);
                integer o = llList2Integer(offsets, i);
                string m = llList2String(pages, o);
                for (n=1; n < l; ++n)
                {
                    data = llList2String(pages, o + n);
                    if (llStringLength(m) + llStringLength(data) > 800)    // arbitrary cutout
                    {
                        sendEmail(llList2String(info, 2), tltp_str, m);
                        m = data;
                    } else m += "\n" + data;                // multiple data lines
                }
                sendEmail(llList2String(info, 2), tltp_str, m);
            }
        } else llOwnerSay("Unknown email received: " + subj + ": " + message);
    }
}

