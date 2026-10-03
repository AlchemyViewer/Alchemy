// NexiiLSL's scripts/roundabout.lsl (include/NexiiLSL/LICENSE): a whole
// script, with strings over several lines, which includes time.lsl.

/*
The Magic Roundabout

This script is designed to help with the dilemma of assigning one script in the region
as the main controller responsible for loading remote data such as from experience storage
or HTTP requests and then having to distribute any loaded data to other subordinate scripts

Make sure the script is set to no modify so people can't peek at the private key used
-- that is the only piece of info that must truly be kept secret

Because the script sends along a signature in the listener messages, it means you are
limited to 852 bytes max (1024 - 172)
*/


/*
    Compares two timestamps per ISO 8601 format "YYYY-MM-DDThh:mm:ss.ff..fZ"
    Returns -1 if a < b, else 1 if a > b else 0 if same
    
    You can comment/cut out parts of the function if you only care about
    date checks or only want fast time checks
*/
integer CompareTimestamps(string a, string b)
{
    integer aYear = (integer)llGetSubString(a, 0, 3);
    integer bYear = (integer)llGetSubString(b, 0, 3);
    if(aYear < bYear) return -1; else if(aYear > bYear) return 1;
    integer aMonth = (integer)llGetSubString(a, 5, 6);
    integer bMonth = (integer)llGetSubString(b, 5, 6);
    if(aMonth < bMonth) return -1; else if(aMonth > bMonth) return 1;
    integer aDay = (integer)llGetSubString(a, 8, 9);
    integer bDay = (integer)llGetSubString(b, 8, 9);
    if(aDay < bDay) return -1; else if(aDay > bDay) return 1;
    integer aHour = (integer)llGetSubString(a, 11, 12);
    integer bHour = (integer)llGetSubString(b, 11, 12);
    if(aHour < bHour) return -1; else if(aHour > bHour) return 1;
    integer aMinute = (integer)llGetSubString(a, 14, 15);
    integer bMinute = (integer)llGetSubString(b, 14, 15);
    if(aMinute < bMinute) return -1; else if(aMinute > bMinute) return 1;
    float aSecond = (float)llGetSubString(a, 17, -2);
    float bSecond = (float)llGetSubString(b, 17, -2);
    if(aSecond < bSecond) return -1; else if(aSecond > bSecond) return 1;
    return 0;
}

// Pure string version of https://wiki.secondlife.com/wiki/Stamp2UnixInt
integer Timestamp2Unix(string stamp)
{
    integer year = (integer)llGetSubString(stamp, 0, 3);
    integer month = (integer)llGetSubString(stamp, 5, 6);
    integer day = (integer)llGetSubString(stamp, 8, 9);
    integer hours = (integer)llGetSubString(stamp, 11, 12);
    integer minutes = (integer)llGetSubString(stamp, 14, 15);
    integer seconds = (integer)llGetSubString(stamp, 17, -2);
    
    year -= 1902;
    if(year >> 31 | year / 136) return 2145916800 * (1 | year >> 31);
    
    month = ~-month;
    day = ~-day;
    
    integer days = (integer)(year * 365.25 + 0.25) - 24837 +
        month * 30 + (month - (month < 7) >> 1) + (month < 2) -
        (((year + 2) & 3) > 0) * (month > 1) + day;
    
    return (
        days * 86400 +
        hours * 3600 +
        minutes * 60 +
        seconds
    );
}

// Compact Check
integer TimestampIsBefore(string a, string b)
{
    if((integer)llGetSubString(a, 8, 9) < (integer)llGetSubString(b, 8, 9)) return TRUE;
    if((integer)llGetSubString(a, 11, 12) < (integer)llGetSubString(b, 11, 12)) return TRUE;
    if((integer)llGetSubString(a, 14, 15) < (integer)llGetSubString(b, 14, 15)) return TRUE;
    if((float)llGetSubString(a, 17, -2) < (float)llGetSubString(b, 17, -2)) return TRUE;
    return FALSE;
}

integer TimestampCompare(string a, string b)
{
    integer aDate = (integer)(llGetSubString(a, 0, 3) + llGetSubString(a, 5, 6) + llGetSubString(a, 8, 9));
    integer bDate = (integer)(llGetSubString(b, 0, 3) + llGetSubString(b, 5, 6) + llGetSubString(b, 8, 9));
    if(aDate < bDate) return -1;
    else if(aDate > bDate) return 1;
    float aTime = (float)(llGetSubString(a, 11, 12) + llGetSubString(a, 14, 15) + llGetSubString(a, 17, -1));
    float bTime = (float)(llGetSubString(b, 11, 12) + llGetSubString(b, 14, 15) + llGetSubString(b, 17, -1));
    llOwnerSay("aTime: " + (string)aTime + "; bTime: " + (string)bTime);
    if(aTime < bTime) return -1;
    else if(aTime > bTime) return 1;
    return 0;
}

// Milliseconds since beginning of month for timestamp
integer Timestamp2Millisec(string stamp)
{
    return (integer)llGetSubString(stamp, 8, 9) * 86400000 + // Days
        (integer)llGetSubString(stamp, 11, 12) * 3600000 + // Hours
        (integer)llGetSubString(stamp, 14, 15) * 60000 + // Minutes
        llRound(((float)llGetSubString(stamp, 17, -2) * 1000.0)) // Seconds.Milliseconds
        - 617316353; // Offset to fit between [-617316353,2147483547]
}

// Seconds since timestamp epoch, per https://wiki.secondlife.com/wiki/Stamp2UnixInt
integer Timestamp2Seconds(string stamp)
{
    list parts = llParseString2List(stamp, ["-", "T", ":", "."], []);
    integer year = llList2Integer(parts, 0) - 1902;
    if(year >> 31 | year / 136) return 2145916800 * (1 | year >> 31);
    integer month = ~-llList2Integer(parts, 1);
    return 86400 * (((integer)(year * 365.25 + 0.25)) - 24837 +
          month * 30 + (month - (month < 7) >> 1) + (month < 2) -
          (((year + 2) & 3) > 0) * (month > 1) +
          (~-llList2Integer(parts, 2))) +
          llList2Integer(parts, 3) * 3600 +
          llList2Integer(parts, 4) * 60 +
          llList2Integer(parts, 5);
}



string PrivateKey = "-----BEGIN RSA PRIVATE KEY-----\nreplace with private key\n-----END RSA PRIVATE KEY-----"

;
string PublicKey = "-----BEGIN PUBLIC KEY-----\nreplace with public key\n-----END PUBLIC KEY-----"

;
float LastPingPong = -60.0;
float LastController = -60.0;

key Controller = NULL_KEY;
list Subordinates;

integer Tick;

default
{
    state_entry()
    {
        llMessageLinked(LINK_SET, -100, "init", "");
        llLinksetDataWrite("Roundabout", "init");
        state subordinate;
    }
}

state subordinate
{
    state_entry()
    {
        llListen(-100, "", "", "");
        llSetTimerEvent(10.0);
        
        LastPingPong = llGetTime();
        string payload = "ping," + (string)llGenerateKey();
        string signature = llSignRSA(PrivateKey, payload, "sha512");
        llRegionSay(-100, signature + payload);
    }
    
    listen(integer channel, string name, key identifier, string text)
    {
        string signature = llGetSubString(text, 0, 171);
        text = llDeleteSubString(text, 0, 171);
        if(!llVerifyRSA(PublicKey, text, signature, "sha512")) return;
        
        string message = llGetSubString(text, 0, llSubStringIndex(text, ",") - 1);
        
        if(message == "ping")
        {
            LastPingPong = llGetTime();
            
            // Respond to ping with pong
            text = "pong," + (string)llGenerateKey();
            signature = llSignRSA(PrivateKey, text, "sha512");
            llRegionSay(-100, signature + text);
        }
        
        else if(message == "pong")
        {
            LastPingPong = llGetTime();
            
            // Was controller, but might have been reset
            if(identifier == Controller) Controller = NULL_KEY;
            
            // Add to subordinates list
            integer pointer = llListFindList(Subordinates, [identifier]);
            if(pointer == -1) Subordinates += identifier;
        }
        
        else if(message == "controller")
        {
            LastController = llGetTime();
            
            // We just initialised and there is a controller out there
            // Announce to other scripts we are subordinate and ready to request data
            if(Controller == NULL_KEY)
            {
                llLinksetDataWrite("Roundabout", "subordinate");
                llMessageLinked(LINK_SET, -100, "subordinate", "");
                Controller = identifier;
            }
            
            // New controller
            else if(identifier != Controller) Controller = identifier;
            
            // Same controller
            else if(identifier == Controller);
            
            // Remove from subordinates list
            integer pointer = llListFindList(Subordinates, [identifier]);
            if(pointer != -1) Subordinates = llDeleteSubList(Subordinates, pointer, pointer);
        }
        
        // Pass along any other messages from controller into linkset
        else if(identifier == Controller)
        {
            llMessageLinked(LINK_SET, -100, text, identifier);
        }
    }
    
    timer()
    {
        ++Tick;
        float time = llGetTime();
        
        // We lost our controller
        if(time - LastController > 60.0 || (Controller != NULL_KEY && llKey2Name(Controller) == ""))
        {
            // Promote oldest subordinate to controller
            key subordinate = llGetKey();
            list checks = [subordinate, Timestamp2Unix((string)llGetObjectDetails(subordinate, [OBJECT_REZ_TIME]))];
            integer iterator = llGetListLength(Subordinates);
            while(iterator --> 0)
            {
                key subordinate = llList2Key(Subordinates, iterator);
                if(llKey2Name(subordinate) != "") checks += [
                    subordinate, Timestamp2Unix((string)llGetObjectDetails(subordinate, [OBJECT_REZ_TIME]))
                ];
            }
            checks = llListSortStrided(checks, 2, 1, TRUE);
            subordinate = llList2Key(checks, 0);
            
            // Promote ourselves
            if(subordinate == llGetKey()) state controller;
            
            // Promote subodinate to controller
            else Controller = subordinate;
        }
        
        // Ping everyone
        else if(time - LastPingPong > 30.0)
        {
            LastPingPong = llGetTime();
            string payload = "ping," + (string)llGenerateKey();
            string signature = llSignRSA(PrivateKey, payload, "sha512");
            llRegionSay(-100, signature + payload);
        }
        
        // Garbage collection on list
        if(!(Tick % 16))
        {
            integer iterator = llGetListLength(Subordinates);
            while(iterator --> 0)
            {
                key subordinate = llList2Key(Subordinates, iterator);
                if(llKey2Name(subordinate) == "")
                {
                    llOwnerSay("Lost subordinate " + (string)subordinate);
                    Subordinates = llDeleteSubList(Subordinates, iterator, iterator);
                }
            }
        }
    }
    
    link_message(integer sender, integer channel, string text, key identifier)
    {
        if(channel == -100 + 1 && Controller != NULL_KEY)
        {
            // Pass along messages from subordinate to controller
            string signature = llSignRSA(PrivateKey, text, "sha512");
            llRegionSayTo(Controller, -100, signature + text);
        }
    }
    
    state_exit()
    {
        // We don't track subordinates outside this state
        Subordinates = [];
    }
}


state controller
{
    state_entry()
    {
        string text = "controller," + (string)llGenerateKey();
        string signature = llSignRSA(PrivateKey, text, "sha512");
        llRegionSay(-100, signature + text);
        
        llLinksetDataWrite("Roundabout", "controller");
        llMessageLinked(LINK_SET, -100, "controller", "");
        llListen(-100, "", "", "");
    }
    
    listen(integer channel, string name, key identifier, string text)
    {
        string signature = llGetSubString(text, 0, 171);
        text = llDeleteSubString(text, 0, 171);
        if(!llVerifyRSA(PublicKey, text, signature, "sha512")) return;
        
        string message = llGetSubString(text, 0, llSubStringIndex(text, ",") - 1);
        
        if(message == "ping")
        {
            // Respond to ping with controller
            text = "controller," + (string)llGenerateKey();
            signature = llSignRSA(PrivateKey, text, "sha512");
            llRegionSay(-100, signature + text);
        }
        else if(message == "pong"); // Ignore
        else if(message == "controller")
        {
            // Huh? There was another controller?
            LastController = llGetTime();
            Controller = identifier;
            llLinksetDataWrite("Roundabout", "subordinate");
            llMessageLinked(LINK_SET, -100, "subordinate", "");
            state subordinate;
        }
        
        // Pass along any other messages from subordinates into linkset
        else
        {
            llMessageLinked(LINK_SET, -100, text, identifier);
        }
    }
    
    link_message(integer sender, integer channel, string text, key identifier)
    {
        if(channel == -100 + 1)
        {
            // Pass along messages from controller to subordinates
            string signature = llSignRSA(PrivateKey, text, "sha512");
            llRegionSay(-100, signature + text);
        }
    }
}