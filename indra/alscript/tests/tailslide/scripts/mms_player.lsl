//
//   TIPS FOR UPLOADING/PLAYING MUSIC (possibly outdated info as of SL 1.4):
//
// - Use GoldWave (http://www.goldwave.com/) to split your MP3 into 9 second clips.
//   You can do this easiest by hitting Tools -> Cue Points, then select Auto Cue.
//   Hit the "Spacing" tab and type "9.00" as the interval, and hit OK. Then hit
//   Split File, choose how to name the files, and hit OK.
//
// - Upload your clips one at a time, using the lowest bitrate. I am not positive,
//   but I'm fairly certain the Bulk Upload feature defaults to 64 or 128kbps. The
//   format sounds are uploaded in (ogg) sounds just as good (bad) at 32kbps as 128kbps,
//   but 32kbps will load much faster.
//
// - DO NOT SET THE SCRIPT TO RAPIDLY PRELOAD THE WHOLE SONG. It will take MUCH longer
//   to hear your music if you force it download every clip first. It will also queue
//   up ALL of the sounds for anybody who just happens to be passing by, even if they
//   will never be around to hear them.
//
// Changes:
// 0.7.7 -- bugfix: stopped spinning before song was over
//       -- bounce->reset, all states call in on_rez
// 0.7.6 -- MSG_DISABLE_TEXT, MSG_PRELOAD, MSG_READING
// 0.7.5 -- dont reset on rez
// 0.7.4 -- fixed wind_down timing bug, add PARAM_DIE_ON_UNLINK for jukeboxes etc.
// 0.7.3 -- link message support
// 0.7.2 -- add SPIN, SPIN_RATE
// 0.7.1 -- display time instead of clip number, better timing
// 0.7.0 -- now reads from list, notecard, or inventory

integer READ_AHEAD          = 2;        // number of clips to preload ahead of playing
float   PRELOAD_WAIT        = 4.5;      // number of seconds per clip to wait for preloading before playback starts
float   CLIP_LENGTH         = 9;        // length in seconds of each clip
float   WAIT_TIME           = 8;        // delay to try to achieve between playing clips
float   DATASERVER_TIMEOUT  = 5.0;      // when to give up on the dataserver
integer OWNER_ONLY          = FALSE;    // only owner can start/stop playback
    
float   TEXT_ALPHA          = 1.0;      // transparency of floating tet

vector  COLOR_RED           = <1,0,0>;  // red
vector  COLOR_GREEN         = <0,1,0>;  // green
vector  COLOR_BLUE          = <0,0,1>;  // blue

integer SPIN                = TRUE;
float   SPIN_RATE           = 30;

key     DOMAIN              = "MASA MUSIC SCRIPT";
integer MSG_PLAY            = 10000;
integer MSG_STOP            = 10100;
integer MSG_PRELOAD         = 10200;
integer MSG_READING         = 10300;
integer MSG_DISABLE_TOUCH   = 11000;
integer MSG_DISABLE_TEXT    = 12000;
integer MSG_START           = 20000;
integer MSG_END             = 20100;
integer MSG_NUM_CLIPS       = 21000;

integer PARAM_DIE_ON_UNLINK = 222646;

list    clips;
string  total_time;
integer num_clips;
integer clip_playing;
integer clip_preloading;
integer preset_clips;
integer notecard_line;
integer disable_touch;
integer disable_text;
integer die_on_unlink;

say(string str) {
    llSay(0, str);
}

string format_float( float num, integer after_dec, integer chop_dec ) {
    string str = "";
    list x = llParseString2List( (string)num, ["."], [] );
    str += llList2String(x, 0);
    
    string decimal = llList2String(x,1);
    if ( (integer)decimal!=0 || !chop_dec ) {
        str += ".";
        str += llGetSubString(decimal, 0, after_dec - 1);
    }
    return str;
}

set_text(string str, vector color) {
    if ( disable_text ) return;
    llSetText( llGetObjectName() + "\n" + str, color, TEXT_ALPHA );
}

integer check_control(integer num) {
    integer i;
    
    if ( disable_touch ) return FALSE;
    
    if ( !OWNER_ONLY ) return TRUE;     // $[E20012]
    
    for (i = 0; i < num; i++)
        if ( llDetectedKey(i) == llGetOwner() )
            return TRUE;
            
    return FALSE;
}

preload_next_clip(integer show_text) {    
    if ( clip_preloading < num_clips )
        llPreloadSound( llList2Key(clips, clip_preloading) );      
        
    if ( show_text ) {
        set_text(
            "Preloading " + (string)(READ_AHEAD - clip_preloading) + " clip(s) " +
            "[" + format_float(PRELOAD_WAIT * (READ_AHEAD - clip_preloading), 1, 0) + " sec]\n" + 
            "Click to start play immediately.",
            COLOR_BLUE
        );
    }

    clip_preloading += 1;        
}

play_next_clip() {
    llPlaySound( llList2Key(clips, clip_playing), 1.0 );
    clip_playing += 1;
}

update_text() {
    set_text(
        "Playing: " + format_time((integer)llGetTime()) + "/" + total_time,
        COLOR_GREEN
    );
}    

string format_time(integer secs) {
    return (string)((integer) (secs / 60)) + ":" + llGetSubString( "0" + (string)(secs % 60), -2, -1 );
}

send_message(integer msg, list data) {
    llMessageLinked( LINK_SET, msg, llList2CSV(data), DOMAIN );
}

default {
    state_entry() {
        if (SPIN) llSetTextureAnim( FALSE, ALL_SIDES, 0, 0, 0, 0, 0 );        // $[E20012]
        clip_playing    = 0;
        clip_preloading = 0;
        num_clips       = llGetListLength(clips);
        if (num_clips > 0) {
            preset_clips = TRUE;
            total_time      = format_time( (integer) (num_clips * CLIP_LENGTH) );
        }
        llStopSound();
        set_text("Stopped", COLOR_RED);
        send_message( MSG_END, [] );
        if ( llGetStartParameter() == PARAM_DIE_ON_UNLINK ) die_on_unlink = TRUE;
        else die_on_unlink = FALSE;
    }
    
    on_rez(integer param) { state reset; }

    touch_start(integer num) {
        if ( check_control(num) ) {
            if ( preset_clips )
                state preload;        
            else if ( llGetInventoryKey("sounds") != NULL_KEY )
                state read_notecard;
            else if ( llGetInventoryNumber(INVENTORY_SOUND) > 0 )
                state read_inventory;
            else
                say("nothing to play!");
        }
    }
    
    link_message(integer sender, integer msg, string data, key domain) {
        if ( domain != DOMAIN ) return;
        if ( msg == MSG_PLAY ) {
            if ( preset_clips )
                state preload;        
            else if ( llGetInventoryKey("sounds") != NULL_KEY )
                state read_notecard;
            else if ( llGetInventoryNumber(INVENTORY_SOUND) > 0 )
                state read_inventory;
            else
                say("nothing to play!");
        } else if ( msg == MSG_DISABLE_TOUCH ) {
            disable_touch = (integer)data;
        }  else if ( msg == MSG_DISABLE_TEXT ) {
            disable_text = (integer )data;
            if ( disable_text ) llSetText("", <0,0,0>, 0);            
        }

    }
    
    changed(integer what) { if ( what & CHANGED_LINK && llGetLinkNumber() == 0 && die_on_unlink ) llDie(); }
}
state reset {
    state_entry() {
        disable_touch = 0;
        disable_text = 0;
        state default;
    }
}

state read_notecard {
    state_entry() {
        notecard_line = 0;
        llGetNotecardLine("sounds", notecard_line++);
        send_message( MSG_READING, [] );        
        set_text("reading notecard", COLOR_BLUE);
        llSetTimerEvent(5);
        clips = [];
    }
    
    dataserver(key qid, string data) {
        if ( data == EOF ) {
            num_clips = llGetListLength(clips);
            if ( num_clips <= 0 ) {
                say("no clips");
                state default;
            } else {
                total_time      = format_time( (integer) (num_clips * CLIP_LENGTH) );
                state preload;
            }
        }
        clips += llCSV2List(data);
        llGetNotecardLine("sounds", notecard_line++);
        llResetTime();
    }
    
    timer() {
        if ( llGetTime() > DATASERVER_TIMEOUT ) {
            say("dataserver timeout");
            state default;
        }
    }
        
    state_exit() {
        llSetTimerEvent(0);
    }        
    
    changed(integer what) { if ( what & CHANGED_LINK && llGetLinkNumber() == 0 && die_on_unlink ) llDie(); }
    
    on_rez(integer param) { state reset; }
    
    link_message(integer sender, integer msg, string data, key domain) {
        if ( domain != DOMAIN ) return;
        if ( msg == MSG_STOP )
            state default;
        else if ( msg == MSG_DISABLE_TOUCH )
            disable_touch = (integer)data;
        else if ( msg == MSG_DISABLE_TEXT )
            disable_text = (integer )data;
    }
        
}

state read_inventory {
    state_entry() {
        integer i;
        send_message( MSG_READING, [] );
        set_text("reading inventory", COLOR_RED);
        num_clips = llGetInventoryNumber(INVENTORY_SOUND);
        total_time  = format_time( (integer) (num_clips * CLIP_LENGTH) );
        clips = [];
        for (i = 0; i < num_clips; i++)
            clips += [llGetInventoryName( INVENTORY_SOUND, i )];
            
        state preload;
    }

    changed(integer what) { if ( what & CHANGED_LINK && llGetLinkNumber() == 0 && die_on_unlink ) llDie(); }
    on_rez(integer param) { state reset; }    
}
        
        

state preload {
    state_entry() {
        send_message( MSG_NUM_CLIPS, [num_clips, CLIP_LENGTH] );
        send_message( MSG_PRELOAD, [] );
        preload_next_clip(TRUE);
        llSetTimerEvent( PRELOAD_WAIT );
    }
    
    touch_start(integer num) {
        if ( check_control(num) )
            state playing;
    }
    
    timer() {
        if ( clip_preloading >= READ_AHEAD || clip_preloading >= num_clips )
            state playing;
        preload_next_clip(TRUE);
    }
    
    link_message(integer sender, integer msg, string data, key domain) {
        if ( domain != DOMAIN ) return;
        if ( msg == MSG_PLAY )
            state playing;
        else if ( msg == MSG_STOP )
            state default;
        else if ( msg == MSG_DISABLE_TOUCH )
            disable_touch = (integer)data;
        else if ( msg == MSG_DISABLE_TEXT )
            disable_text = (integer )data;
    }
    

    state_exit() {
        llSetTimerEvent(0);
    }

    changed(integer what) { if ( what & CHANGED_LINK && llGetLinkNumber() == 0 && die_on_unlink ) llDie(); }
    on_rez(integer param) { state reset; }    
}

state playing {
    state_entry() {
        llSetSoundQueueing(TRUE);
        llSetTimerEvent( 1 ); 
        send_message(MSG_START, []);
        llResetTime();
        if ( SPIN ) // $[E20012]
            llSetTextureAnim( ANIM_ON|ROTATE|LOOP, ALL_SIDES, 0, 0, 0, TWO_PI, SPIN_RATE );
        play_next_clip();        
        preload_next_clip(FALSE);
        update_text();
        if ( clip_playing >= num_clips )
            state wind_down;
    }
    
    timer() {
        if ( (integer)((llGetTime()+(CLIP_LENGTH - WAIT_TIME)) / CLIP_LENGTH) >= clip_playing) {
            play_next_clip();        
            preload_next_clip(FALSE);
            if ( clip_playing >= num_clips )
                state wind_down;
        }
        update_text();
    }
    
    touch_start(integer num) {
        if ( check_control(num) )
            state default;
    }
    
    link_message(integer sender, integer msg, string data, key domain) {
        if ( domain != DOMAIN ) return;
        if ( msg == MSG_STOP )
            state default;
        else if ( msg == MSG_DISABLE_TOUCH )
            disable_touch = (integer)data;
        else if ( msg == MSG_DISABLE_TEXT )
            disable_text = (integer )data;
    }
    
    state_exit() {
        llSetTimerEvent(0);
    }

    changed(integer what) { if ( what & CHANGED_LINK && llGetLinkNumber() == 0 && die_on_unlink ) llDie(); }
    on_rez(integer param) { state reset; }    
}

state wind_down {
    state_entry() {
        llSetTimerEvent( 1);
    } 
    
    timer() {
        if ( llGetTime() >= (num_clips * CLIP_LENGTH) ) 
            state default;
        update_text();
    }
    
    touch_start(integer num) {
        if ( check_control(num) )
            state default;
    }
    
    link_message(integer sender, integer msg, string data, key domain) {
        if ( domain != DOMAIN ) return;
        if ( msg == MSG_STOP )
            state default;
        else if ( msg == MSG_DISABLE_TOUCH )
            disable_touch = (integer)data;
        else if ( msg == MSG_DISABLE_TEXT )
            disable_text = (integer )data;
            
    }
    
    state_exit() {
        llSetTimerEvent(0);
    }
    
    changed(integer what) { if ( what & CHANGED_LINK && llGetLinkNumber() == 0 && die_on_unlink ) llDie(); }
    on_rez(integer param) { state reset; }
}

