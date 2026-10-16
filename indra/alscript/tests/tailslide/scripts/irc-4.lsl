/////////////////////////////////////////////////
// DeepTeal Channels

integer     CHANNEL_MUX     = 956458772;  // Main message channel
integer     CHANNEL_CONTROL = 5018485;    // Control message channel    (UNUSED: $[E20009])

/////////////////////////////////////////////////
// Other Constants

string      COMMAND_CHAR    = "/";        // Control message prefix     (UNUSED: $[E20009])
integer     ID_SET          = 0xF;        // Special name/loc id
integer     NUM_SLOTS       = 0xE;        // Number of name/loc slots.
integer     POS_FRONT       = 0;          // Front of list
integer     POS_BACK        = 0x7FFFFFFF; // Back of list
integer     MAX_STR_LEN     = 255;        // Max string length
integer     TIMER_INTERVAL  = 1;
integer     TIMEOUT         = 5;          // XML-RPC Response timeout
list        HEX_CHARACTERS  = ["0","1","2","3","4","5","6","7","8","9","a","b","c","d","e","f"];
// convenience
integer     ON              = TRUE;
integer     OFF             = FALSE;

/////////////////////////////////////////////////
// Globals

key         rpc_channel;                  // XML-RPC Channel
key         rpc_msg_key;                  // current XML-RPC message key
integer     is_channel_available;         // whether current msg is still awaiting reply
list        local_names;                  // local name list
list        local_locations;              // local location list
list        remote_names;                 // remote name list
list        remote_locations;             // remote location list
integer     global_message_id;            // current message id  
list        message_queue;                // messages waiting to send
list        messages_sent;                // last messages sent
integer     next_name_pos = -1;           // pos to insert next new local name
integer     next_location_pos = -1;       // pos to insert next new local location

////////////////////////////////////////////////
// hex functions
string int2hex(integer i) {
    return llList2String(HEX_CHARACTERS, i);
}

string int2hex2(integer i) {
    return llList2String(HEX_CHARACTERS, i/16) + llList2String(HEX_CHARACTERS, i%16);
}

integer hex2int(string s) {
    return (integer) ("0x" + s);
}

////////////////////////////////////////////////
// flash(prim, on) - finds a linked prim named in the format "name:offcolor:oncolor" and
//                   sets it to the color specified
flash(string prim, integer on) {
    integer i;
    integer prims = llGetNumberOfPrims() + 1;
    list l;
    if (on) on = 2; else on = 1; 
    for ( i = 2; i < prims; ++i ) {
        l = llParseString2List( llGetLinkName(i), [":"], [] );
        if ( llList2String(l, 0) == prim ) {
            llSetLinkColor( i, (vector)llList2String(l, on), ALL_SIDES );
            return;
        }
    }
}


////////////////////////////////////////////////
// reset() - clear remote lists, queue local lists
reset() {
    integer i;
    global_message_id = 0;
    remote_names = [];
    remote_locations = [];
    for ( i = 0; i < NUM_SLOTS; i++ ) {
        remote_names += "?";
        remote_locations += "?";
        // unshift local_*[i] if they exist
        if ( llGetListLength(local_names) > i )
            queue_message( POS_FRONT, i, ID_SET, llList2String(local_names, i) );
        if ( llGetListLength(local_locations) > i )
            queue_message( POS_FRONT, ID_SET, i, llList2String(local_locations, i) );
    }
}

////////////////////////////////////////////////
// queue_message(pos, name, loc, data) - queue a message
//   pos  - where to insert in queue
//   name - name id
//   loc  - location id
//   data - data
queue_message( integer pos, integer name, integer loc, string data ) {
    string message = int2hex(name) + int2hex(loc) + int2hex2(llStringLength(data));
    // translate entities (xml-rpc should do this for us !!)
    // since they are automatically translated on the other side, we do them AFTER
    // the length is added
    data = llDumpList2String( llParseStringKeepNulls(data,["&"],[]), "&amp;" );
    data = llDumpList2String( llParseStringKeepNulls(data,["<"],[]), "&lt;" );
    data = llDumpList2String( llParseStringKeepNulls(data,[">"],[]), "&gt;" );
    message += data;    
    
    // 1000 invocations, straight llListInsertList vs if() chain, run concurrently
    // 1/2 = front insert, 3/4/5/6 = back insertion
    // llListInsertList - 1: 17.27, 2: 18.48 || 3: 15.47, 4: 16.16, 5: 15.91, 6: 15.78
    // if() chain       - 1: 11.90, 2: 12.72 || 3: 16.27, 4: 14.38, 5: 14.44, 6: 17.07
    if ( pos == POS_FRONT )
        message_queue = message + message_queue;
    else
        message_queue = llListInsertList( message_queue, [message], pos );
}

////////////////////////////////////////////////
// handle_request( data ) - handle an incoming request
handle_request( string data ) {
    integer pos = 0;
    integer len = llStringLength(data);
    integer name;
    integer location;
    integer msgdatalen;
    string  msgdata;
    flash("recv", ON);
    while ( pos < len ) {
        // bit of black magic here for speed, but shouldn't be too hard to understand
        name       = hex2int(llGetSubString( data, pos, pos));
        location   = hex2int(llGetSubString( data, ++pos, pos ));
        msgdatalen = hex2int(llGetSubString( data, ++pos, ++pos ));
        msgdata    = llGetSubString( data, ++pos, pos += (msgdatalen - 1) );
        ++pos;
        //llOwnerSay( llList2CSV([name,location,msgdatalen,msgdata]) );
        
        if ( name == ID_SET )
            remote_locations = llListReplaceList( remote_locations, [msgdata], location, location );
        else if ( location == ID_SET )
            remote_names     = llListReplaceList( remote_names,     [msgdata], name,     name     );
        else
            send_message( name, location, msgdata );
    }
    // process_queue(FALSE);
    
    // if channel is still available
    if ( is_channel_available ) {
        // due to a bug in SL's XML-RPC implementation, probably owing to the
        // mis-/non-use of the msg_id, if a response times out, the next response
        // is discarded (probably sent to the old request). So we must make sure
        // to respond before the timeout, even if we have nothing to send.
        llResetTime();
    }
    
    flash("recv", OFF);
}

////////////////////////////////////////////////
// send_message( name, location, message ) - output message
send_message( integer name, integer location, string message ) {
    string header = "<[" + llList2String( remote_locations, location ) + "] " + llList2String( remote_names, name ) + "> ";
    string b64_msg = "TextBase64::" + header + llStringToBase64(" " + message);
    if ( llStringLength(b64_msg) < MAX_STR_LEN )
        llSay( CHANNEL_MUX, b64_msg );
    else
        llSay( CHANNEL_MUX, "Text::" + header + message );
}

////////////////////////////////////////////////
// process_queue( force ) - process message queue
//   force - if TRUE, send response even if there is no data
process_queue(integer force) {
    string  data    = "";
    integer datalen = 0;
    string  msg;
    integer msglen;
    integer to_pop  = 0;
    messages_sent = [];
    
    while ( llGetListLength(message_queue) > to_pop ) {
        msg = llList2String(message_queue, to_pop);
        msglen = llStringLength(msg);
        if ( (datalen + msglen) > MAX_STR_LEN )
            jump process_queue__break1;
        to_pop += 1;
        messages_sent += [msg];
        datalen += msglen;
        data += msg;
    }
    @process_queue__break1;
    
    if ( to_pop > 0 ) {
        message_queue = llDeleteSubList(message_queue, 0, to_pop - 1);
    }
    
    if ( datalen > 0 || force ) {
        llSetTimerEvent(0); // don't want timer events while we're waiting
        flash("open", OFF);
        flash("send", ON);
        llRemoteDataReply( rpc_channel, rpc_msg_key, data, global_message_id );
        flash("send", OFF);
        llSetTimerEvent(TIMER_INTERVAL);
        is_channel_available = FALSE;
    }
        
}

////////////////////////////////////////////////
// add_to_queue(name, location, message) - add a message to the queue
//   XXX: this is named sort of poorly
add_to_queue(string name, string location, string message) {
    integer iname = get_local_name(name);
    integer iloc  = get_local_location(location);
    queue_message( POS_BACK, iname, iloc, message );
}

////////////////////////////////////////////////
// get_local_name(name) - return local name id, adding it if it doesn't exist
integer get_local_name(string name) {
    integer pos = llListFindList(local_names, [name]);
    if ( pos != -1 ) return pos;
    pos = llGetListLength(local_names);
    if ( pos < NUM_SLOTS ) {
        local_names += name;
        queue_message( POS_BACK, pos, ID_SET, name );
        return pos;
    } 
    next_name_pos = (next_name_pos + 1) % NUM_SLOTS;
    local_names = llListReplaceList(local_names, [name], next_name_pos, next_name_pos);
    queue_message( POS_BACK, next_name_pos, ID_SET, name );
    return next_name_pos;
}

// XXX: would be nice if macros or something could be used here
// s/_name/_location/g
integer get_local_location(string name) {
    integer pos = llListFindList(local_locations, [name]);
    if ( pos != -1 ) return pos;
    pos = llGetListLength(local_locations);
    if ( pos < NUM_SLOTS ) {
        local_locations += name;
        queue_message(POS_BACK, ID_SET, pos, name);
        return pos;
    } 
    next_location_pos = (next_location_pos + 1) % NUM_SLOTS;
    local_locations = llListReplaceList(local_locations, [name], next_location_pos, next_location_pos);
    queue_message(POS_BACK, ID_SET, next_location_pos, name);
    return next_location_pos;
}

        

default {
    state_entry() {
        reset();
        llOpenRemoteDataChannel();
        llListen( CHANNEL_MUX, "", NULL_KEY, "" );
        llSetTimerEvent(TIMER_INTERVAL);
        llResetTime();
    }
    
    remote_data(integer type, key channel, key msg_key, string sender, integer message_id, string data) {
        //llOwnerSay("remote_data");        
        if ( type == REMOTE_DATA_CHANNEL ) {
            // update channel key
            rpc_channel = channel;
            
            // tell owner what it is
            llOwnerSay( llGetScriptName() + ": " + (string)rpc_channel );
            
            // mark channel unavailable
            is_channel_available = FALSE;
            return;
        } else if (type == REMOTE_DATA_REQUEST) {                                                
            if ( global_message_id == 0 && message_id != 0 && message_id != 1 ) { // we need a reset
            
                // send request
                process_queue(TRUE);
                
            } else {                                        

                // store message key needed for response
                rpc_msg_key = msg_key;
    
                // mark channel available
                is_channel_available = TRUE;
                flash( "open", ON );
            
                // messages weren't received
                if ( message_id != (global_message_id + 1) ) {
                    // put sent messages back in front of queue
                    message_queue = messages_sent + message_queue;
                }
                
                // if reset requested
                if ( message_id == 0 ) {
                    reset();
                }
                
                // update message id
                global_message_id = message_id + 1;
                
                // handle request
                handle_request( data );
            }
        }
    }
    
    listen(integer channel, string name, key id, string msg) {
        if ( id == llGetKey() ) return;
        //llOwnerSay(msg);
        //llOwnerSay("listen");
        list   l = llParseStringKeepNulls(msg, [], ["<","[","]",">"]);
        // (Text::|TextBase64::), <, nil, [, Satyr, ],  Masakazu Kojima, >,  a
        //                     0  1    2  3      4  5                 6  7   8
        string type  = llList2String(l, 0);
        string mloc  = llList2String(l, 4);
        string mname = llDeleteSubString(llList2String(l, 6),0,0);
        string msg   = llDeleteSubString(llDumpList2String( llList2List(l, 8, -1), "" ),0,0); // SHADOW: $[E20001]
        if ( type == "TextBase64::" ) msg = llDeleteSubString(llBase64ToString(msg),0,0);
        add_to_queue(mname, mloc, msg);
    }
    
    timer() {
        //llOwnerSay("timer");
        if ( is_channel_available ) {
            if ( llGetTime() > TIMEOUT ) {
                process_queue(TRUE); // force processing of sendq (see comments in handle_request)
            } else {
                process_queue(FALSE);
            }
        }
    }
    
}
