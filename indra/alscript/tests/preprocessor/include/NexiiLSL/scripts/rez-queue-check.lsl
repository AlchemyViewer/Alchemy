#include "NexiiLSL/app-uri.lsl"

float start;
float end;
integer hasQueue;
integer hasLargeQueue;
list tracked;

integer lastCount;
integer tick;

checkAttachments()
{
    list agents = llJson2List(llLinksetDataRead("agents"));
    integer iterator = llGetListLength(agents);
    while(iterator --> 0)
    {
        string agent = llList2String(agents, iterator);
        string animation = llGetAnimation(agent);
        string legacyName = llKey2Name(agent); // Ghosted avatars have an empty string
        string displayName = llGetDisplayName(agent); // May not always be non-empty string?
        string userName = llGetUsername(agent); // May not always be non-empty string?
        list attachments = llGetAttachedList(agent);
        
        if(animation == ""
        || animation == "Init"
        || legacyName == ""
        || displayName == ""
        || userName == ""
        || llGetListLength(attachments) == 0)
            jump continue; // Do not consider agent if entering/exiting region, logging, teleporting or ghosted
        
        list prevAttachments = llJson2List(llLinksetDataRead(agent + "_attachments"));
        list attached;
        list detached;
        
        // Attached
        integer index = llGetListLength(attachments);
        while(index --> 0)
        {
            string attachment = llList2String(attachments, index);
            integer pointer = llListFindList(prevAttachments, [attachment]);
            if(pointer == -1)
            {
                llLinksetDataWrite(attachment + "_name", llKey2Name(attachment));
                attached += attachment;
            }
        }
        
        // Detached
        index = llGetListLength(prevAttachments);
        while(index --> 0)
        {
            string attachment = llList2String(prevAttachments, index);
            integer pointer = llListFindList(attachments, [(key)attachment]);
            if(pointer == -1)
            {
                // llLinksetDataDelete(agent + "_" + attachment);
                detached += attachment;
            }
        }
        
        // Cache
        llLinksetDataWrite(agent + "_attachments", llList2Json(JSON_ARRAY, attachments));
        
        
        //if(hasQueue || hasLargeQueue)
        //{
            //string message;
            integer a = llGetListLength(attached);
            integer d = llGetListLength(detached);
            /*if(a && d) message = agentAboutURI(agent) + " attached " + string(a) + " and detached " + string(d) + " items";
            else if(a) message = agentAboutURI(agent) + " attached " + string(a) + " items";
            else if(d) message = agentAboutURI(agent) + " detached " + string(d) + " items";*/
            
            if(a) tracked += agentAboutURI(agent);
            
            /*while(a --> 0)
            {
                string attachment = llList2String(attached, a);
                message += "\n+ " + objectURI(attachment);
            }*/
            
            while(d --> 0)
            {
                string attachment = llList2String(detached, d);
                // message += "\n- " + llLinksetDataRead(attachment + "_name");
                llLinksetDataDelete(attachment + "_name");
                // llLinksetDataDelete(agent + "_" + attachment);
            }
            
            //if(message) llOwnerSay(message);
        //}
        
        @continue;
    }
}

announce(string message) {
    string name = llGetObjectName();
    llSetObjectName(llGetRegionName());
    
    //llSay(PUBLIC_CHANNEL, message);
    
    //*
    list agents = llJson2List(llLinksetDataRead("agents"));
    integer iterator = llGetListLength(agents);
    while(iterator --> 0)
    {
        key agent = llList2Key(agents, iterator);
        llRegionSayTo(agent, PUBLIC_CHANNEL, message);
    }
    //*/
    
    llSetObjectName(name);
}


default
{
    state_entry()
    {
        llSetText("", <1,1,1>, 1);
        llLinksetDataDeleteFound("_(attachments|name)$", "");
        
        list agents = llGetAgentList(AGENT_LIST_REGION, []);
        llLinksetDataWrite("agents", llList2Json(JSON_ARRAY, agents));
        integer iterator = lastCount = llGetListLength(agents);
        while(iterator --> 0)
        {
            string agent = llList2String(agents, iterator);
            list attachments = llGetAttachedList(agent);
            llLinksetDataWrite(agent + "_attachments", llList2Json(JSON_ARRAY, attachments));
            integer index = llGetListLength(attachments);
            while(index --> 0)
            {
                string attachment = llList2String(attachments, index);
                llLinksetDataWrite(attachment + "_name", llKey2Name(attachment));
            }
        }
        
        llOwnerSay("Initialise Rez Queue Checker (Mem " + llGetSubString((string)(llGetFreeMemory() / 1024.0), 0, -6) + "KB / LSD " + llGetSubString((string)(llLinksetDataAvailable() / 1024.0), 0, -6) + "KB)");
        // llOwnerSay((string));
        
        llSetTimerEvent(20/45.);
        checkAttachments();
    }
    
    timer()
    {
        if(llGetTime() > 60*60*12) llResetScript();
        
        ++tick;
        
        integer count = llGetRegionAgentCount();
        if(lastCount != count)
        {
            lastCount = count;
            list lastAgents = llJson2List(llLinksetDataRead("agents"));
            list agents = llGetAgentList(AGENT_LIST_REGION, []);
            list added;
            list removed;
            while(count --> 0)
            {
                string agent = llList2String(agents, count);
                integer pointer = llListFindList(lastAgents, [agent]);
                if(pointer == -1)
                {
                    added += agentAboutURI(agent);
                    list attachments = llGetAttachedList(agent);
                    llLinksetDataWrite(agent + "_attachments", llList2Json(JSON_ARRAY, attachments));
                }
            }
            
            count = llGetListLength(lastAgents);
            while(count --> 0)
            {
                string agent = llList2String(lastAgents, count);
                integer pointer = llListFindList(agents, [(key)agent]);
                if(pointer == -1)
                {
                    removed += agentAboutURI(agent);
                    llLinksetDataDeleteFound("^" + agent, "");
                }
            }
            
            //if(removed) llOwnerSay("Removed: " + llList2CSV(removed));
            //if(added) llOwnerSay("Added: " + llList2CSV(added));
            
            llLinksetDataWrite("agents", llList2Json(JSON_ARRAY, agents));
        }
        
        float now = llGetTime();
        float downloads = llGetSimStats(SIM_STAT_ASSET_DOWNLOADS);
        float uploads = llGetSimStats(SIM_STAT_ASSET_UPLOADS);
        
        // if(downloads > 0.5 || uploads > 0.5)
        if(downloads > 0.01 || uploads > 0.01)
        {
            if(!hasQueue)
            {
                //llOwnerSay("Downloads pending!");
                hasQueue = TRUE;
                end = start = now;
            }
            
            else if(hasQueue)
            {
                end = now;
                if(downloads > 0.9 && /*now - end > 1.5 &&*/ !hasLargeQueue)
                {
                    hasLargeQueue = TRUE;
                    tracked = [];
                    announce("Rez queue detected!");
                }
            }
        }
        
        else if(hasLargeQueue)
        {
            float delta = end - start;
            start = end = 0;
            checkAttachments();
            string message = "Rez queue ended after " + llGetSubString((string)delta, 0, -5) + "s";
            if(tracked) message += " due to " + llDumpList2String(tracked, ", ");
            announce(message);
            hasQueue = hasLargeQueue = FALSE;
            tracked = [];
        }
        
        else if(hasQueue)
        {
            float delta = end - start;
            checkAttachments();
            //llOwnerSay("Pending over after " + llGetSubString((string)delta, 0, -5) + "s");
            hasQueue = FALSE;
        }
        
        else if(!(tick % 45)) checkAttachments();
    }
    
    changed(integer change)
    {
        if(change & CHANGED_REGION) llResetScript();
    }
    
    attach(key avatar)
    {
        llResetScript();
    }
}
