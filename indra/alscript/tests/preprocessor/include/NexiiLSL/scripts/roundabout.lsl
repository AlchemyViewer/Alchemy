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

#include "NexiiLSL/time.lsl"

#define CHANNEL_ROUNDABOUT -100 // replace with secret channel

string PrivateKey = "-----BEGIN RSA PRIVATE KEY-----
replace with private key
-----END RSA PRIVATE KEY-----";
string PublicKey = "-----BEGIN PUBLIC KEY-----
replace with public key
-----END PUBLIC KEY-----";
float LastPingPong = -60.0;
float LastController = -60.0;

key Controller = NULL_KEY;
list Subordinates;

integer Tick;

default
{
    state_entry()
    {
        llMessageLinked(LINK_SET, CHANNEL_ROUNDABOUT, "init", "");
        llLinksetDataWrite("Roundabout", "init");
        state subordinate;
    }
}

state subordinate
{
    state_entry()
    {
        llListen(CHANNEL_ROUNDABOUT, "", "", "");
        llSetTimerEvent(10.0);
        
        LastPingPong = llGetTime();
        string payload = "ping," + (string)llGenerateKey();
        string signature = llSignRSA(PrivateKey, payload, "sha512");
        llRegionSay(CHANNEL_ROUNDABOUT, signature + payload);
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
            llRegionSay(CHANNEL_ROUNDABOUT, signature + text);
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
                llMessageLinked(LINK_SET, CHANNEL_ROUNDABOUT, "subordinate", "");
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
            llMessageLinked(LINK_SET, CHANNEL_ROUNDABOUT, text, identifier);
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
            llRegionSay(CHANNEL_ROUNDABOUT, signature + payload);
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
        if(channel == CHANNEL_ROUNDABOUT + 1 && Controller != NULL_KEY)
        {
            // Pass along messages from subordinate to controller
            string signature = llSignRSA(PrivateKey, text, "sha512");
            llRegionSayTo(Controller, CHANNEL_ROUNDABOUT, signature + text);
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
        llRegionSay(CHANNEL_ROUNDABOUT, signature + text);
        
        llLinksetDataWrite("Roundabout", "controller");
        llMessageLinked(LINK_SET, CHANNEL_ROUNDABOUT, "controller", "");
        llListen(CHANNEL_ROUNDABOUT, "", "", "");
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
            llRegionSay(CHANNEL_ROUNDABOUT, signature + text);
        }
        else if(message == "pong"); // Ignore
        else if(message == "controller")
        {
            // Huh? There was another controller?
            LastController = llGetTime();
            Controller = identifier;
            llLinksetDataWrite("Roundabout", "subordinate");
            llMessageLinked(LINK_SET, CHANNEL_ROUNDABOUT, "subordinate", "");
            state subordinate;
        }
        
        // Pass along any other messages from subordinates into linkset
        else
        {
            llMessageLinked(LINK_SET, CHANNEL_ROUNDABOUT, text, identifier);
        }
    }
    
    link_message(integer sender, integer channel, string text, key identifier)
    {
        if(channel == CHANNEL_ROUNDABOUT + 1)
        {
            // Pass along messages from controller to subordinates
            string signature = llSignRSA(PrivateKey, text, "sha512");
            llRegionSay(CHANNEL_ROUNDABOUT, signature + text);
        }
    }
}