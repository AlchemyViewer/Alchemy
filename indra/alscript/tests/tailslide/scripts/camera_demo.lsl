//Linden Labs
//Dan Linden
integer CHANNEL; // dialog channel
list MENU_MAIN = ["Default", "Overhead Cam", "Spin Cam", "Trap Toggle", "Spaz Cam", "Drop Cam", "Worm Cam", "Side Cam", "Driving Cam", "More...", "Cam ON", "Cam OFF"]; // the main menu
list MENU_2 = ["More...", "...Back"]; // menu 2
 
integer on = FALSE;
integer flying; // $[E20009]
integer falling; // $[E20009]
integer spaz = 0; // $[E20009]
integer trap = 0;

take_camera_control(key agent)
{
    llOwnerSay("take_camera_control"); // say function name for debugging
    llOwnerSay((string)agent); 
    llRequestPermissions(agent, PERMISSION_CONTROL_CAMERA);
    llSetCameraParams([CAMERA_ACTIVE, 1]); // 1 is active, 0 is inactive
    on = TRUE;
}

release_camera_control(key agent)
{
    llOwnerSay("release_camera_control"); // say function name for debugging
    llSetCameraParams([CAMERA_ACTIVE, 0]); // 1 is active, 0 is inactive
    llReleaseCamera(agent);
    on = FALSE;
} 

focus_on_me()
{
    llOwnerSay("focus_on_me"); // say function name for debugging
//    llClearCameraParams(); // reset camera to default
    vector here = llGetPos();
    llSetCameraParams([
        CAMERA_ACTIVE, 1, // 1 is active, 0 is inactive
        CAMERA_BEHINDNESS_ANGLE, 0.0, // (0 to 180) degrees
        CAMERA_BEHINDNESS_LAG, 0.0, // (0 to 3) seconds
        CAMERA_DISTANCE, 0.0, // ( 0.5 to 10) meters
        CAMERA_FOCUS, here, // region relative position
        CAMERA_FOCUS_LAG, 0.0 , // (0 to 3) seconds
        CAMERA_FOCUS_LOCKED, TRUE, // (TRUE or FALSE)
        CAMERA_FOCUS_THRESHOLD, 0.0, // (0 to 4) meters
//        CAMERA_PITCH, 80.0, // (-45 to 80) degrees
        CAMERA_POSITION, here + <4,4,4>, // region relative position
        CAMERA_POSITION_LAG, 0.0, // (0 to 3) seconds
        CAMERA_POSITION_LOCKED, TRUE, // (TRUE or FALSE)
        CAMERA_POSITION_THRESHOLD, 0.0, // (0 to 4) meters
        CAMERA_FOCUS_OFFSET, ZERO_VECTOR // <-10,-10,-10> to <10,10,10> meters
    ]);
}

default_cam()
{
//    llOwnerSay("default_cam"); // say function name for debugging
    llClearCameraParams(); // reset camera to default
    llSetCameraParams([CAMERA_ACTIVE, 1]);
}


driving_cam()
{
    llOwnerSay("driving_cam"); // say function name for debugging
    default_cam();
    llSetCameraParams([
        CAMERA_ACTIVE, 1, // 1 is active, 0 is inactive
        CAMERA_BEHINDNESS_ANGLE, 90.0, // (0 to 180) degrees
        CAMERA_BEHINDNESS_LAG, 0.5, // (0 to 3) seconds
        CAMERA_DISTANCE, 8.0, // ( 0.5 to 10) meters
        //CAMERA_FOCUS, <0,0,5>, // region relative position
        CAMERA_FOCUS_LAG, 0.05 , // (0 to 3) seconds
        CAMERA_FOCUS_LOCKED, FALSE, // (TRUE or FALSE)
        CAMERA_FOCUS_THRESHOLD, 0.0, // (0 to 4) meters
        CAMERA_PITCH, 20.0, // (-45 to 80) degrees
        //CAMERA_POSITION, <0,0,0>, // region relative position
        CAMERA_POSITION_LAG, 0.1, // (0 to 3) seconds
        CAMERA_POSITION_LOCKED, FALSE, // (TRUE or FALSE)
        CAMERA_POSITION_THRESHOLD, 0.0, // (0 to 4) meters
        CAMERA_FOCUS_OFFSET, <3,0,2> // <-10,-10,-10> to <10,10,10> meters
    ]);
}


side_cam()
{
    llOwnerSay("side_cam"); // say function name for debugging
    llClearCameraParams(); // reset camera to default
    llSetCameraParams([
        CAMERA_ACTIVE, 1, // 1 is active, 0 is inactive
        CAMERA_BEHINDNESS_ANGLE, 0.0, // (0 to 180) degrees
        CAMERA_BEHINDNESS_LAG, 0.0, // (0 to 3) seconds
        CAMERA_DISTANCE, 0.0, // ( 0.5 to 10) meters
        //CAMERA_FOCUS, <0,0,5>, // region relative position
        CAMERA_FOCUS_LAG, 0.0 , // (0 to 3) seconds
//        CAMERA_FOCUS_LOCKED, FALSE, // (TRUE or FALSE)
        CAMERA_FOCUS_THRESHOLD, 0.0, // (0 to 4) meters
//        CAMERA_PITCH, 80.0, // (-45 to 80) degrees
        //CAMERA_POSITION, <0,0,0>, // region relative position
        CAMERA_POSITION_LAG, 0.0, // (0 to 3) seconds
//        CAMERA_POSITION_LOCKED, FALSE, // (TRUE or FALSE)
        CAMERA_POSITION_THRESHOLD, 0.0, // (0 to 4) meters
        CAMERA_FOCUS_OFFSET, <0,3,0> // <-10,-10,-10> to <10,10,10> meters
    ]);
}

rearview_cam() // $[E20009]
{
    llOwnerSay("rearview_cam"); // say function name for debugging
    llSetCameraParams([
        CAMERA_ACTIVE, 1, // 1 is active, 0 is inactive
        CAMERA_BEHINDNESS_ANGLE, 180.0, // (0 to 180) degrees
        CAMERA_BEHINDNESS_LAG, 0.0, // (0 to 3) seconds
//        CAMERA_DISTANCE, 10.0, // ( 0.5 to 10) meters
        //CAMERA_FOCUS, <0,0,5>, // region relative position
        CAMERA_FOCUS_LAG, 3.0 , // (0 to 3) seconds
//        CAMERA_FOCUS_LOCKED, FALSE, // (TRUE or FALSE)
        CAMERA_FOCUS_THRESHOLD, 0.0, // (0 to 4) meters
//        CAMERA_PITCH, 80.0, // (-45 to 80) degrees
        //CAMERA_POSITION, <0,0,0>, // region relative position
//        CAMERA_POSITION_LAG, 0.0, // (0 to 3) seconds
//        CAMERA_POSITION_LOCKED, FALSE, // (TRUE or FALSE)
        CAMERA_POSITION_THRESHOLD, 0.0, // (0 to 4) meters
        CAMERA_FOCUS_OFFSET, <5,2,-2> // <-10,-10,-10> to <10,10,10> meters
    ]);
}

overhead_cam()
{
    llOwnerSay("overhead_cam"); // say function name for debugging
    default_cam();
    llSetCameraParams([
        CAMERA_ACTIVE, 1, // 1 is active, 0 is inactive
        CAMERA_BEHINDNESS_ANGLE, 180.0, // (0 to 180) degrees
        CAMERA_BEHINDNESS_LAG, 0.5, // (0 to 3) seconds
        CAMERA_DISTANCE, 10.0, // ( 0.5 to 10) meters
        //CAMERA_FOCUS, <0,0,5>, // region relative position
        CAMERA_FOCUS_LAG, 0.05 , // (0 to 3) seconds
        CAMERA_FOCUS_LOCKED, FALSE, // (TRUE or FALSE)
        CAMERA_FOCUS_THRESHOLD, 0.0, // (0 to 4) meters
        CAMERA_PITCH, 80.0, // (-45 to 80) degrees
        //CAMERA_POSITION, <0,0,0>, // region relative position
        CAMERA_POSITION_LAG, 0.0, // (0 to 3) seconds
        CAMERA_POSITION_LOCKED, FALSE, // (TRUE or FALSE)
        CAMERA_POSITION_THRESHOLD, 0.0, // (0 to 4) meters
        CAMERA_FOCUS_OFFSET, <0,0,0> // <-10,-10,-10> to <10,10,10> meters
    ]);
}

drop_camera_5_seconds()
{
    llOwnerSay("drop_camera_5_seconds"); // say function name for debugging
    llSetCameraParams([
        CAMERA_ACTIVE, 1, // 1 is active, 0 is inactive
        CAMERA_BEHINDNESS_ANGLE, 0.0, // (0 to 180) degrees
        CAMERA_BEHINDNESS_LAG, 0.5, // (0 to 3) seconds
        CAMERA_DISTANCE, 3.0, // ( 0.5 to 10) meters
        //CAMERA_FOCUS, <0,0,5>, // region relative position
        CAMERA_FOCUS_LAG, 2.0, // (0 to 3) seconds
        CAMERA_FOCUS_LOCKED, FALSE, // (TRUE or FALSE)
        CAMERA_FOCUS_THRESHOLD, 0.0, // (0 to 4) meters
        CAMERA_PITCH, 0.0, // (-45 to 80) degrees
        //CAMERA_POSITION, <0,0,0>, // region relative position
        CAMERA_POSITION_LAG, 0.05, // (0 to 3) seconds
        CAMERA_POSITION_LOCKED, TRUE, // (TRUE or FALSE)
        CAMERA_POSITION_THRESHOLD, 0.0, // (0 to 4) meters
        CAMERA_FOCUS_OFFSET, <0,0,0> // <-10,-10,-10> to <10,10,10> meters
    ]);
    llSleep(5);
    default_cam();
}

worm_cam()
{
    llOwnerSay("worm_cam"); // say function name for debugging
    llSetCameraParams([
        CAMERA_ACTIVE, 1, // 1 is active, 0 is inactive
        CAMERA_BEHINDNESS_ANGLE, 180.0, // (0 to 180) degrees
        CAMERA_BEHINDNESS_LAG, 0.0, // (0 to 3) seconds
        CAMERA_DISTANCE, 8.0, // ( 0.5 to 10) meters
        //CAMERA_FOCUS, <0,0,5>, // region relative position
        CAMERA_FOCUS_LAG, 0.0 , // (0 to 3) seconds
        CAMERA_FOCUS_LOCKED, FALSE, // (TRUE or FALSE)
        CAMERA_FOCUS_THRESHOLD, 4.0, // (0 to 4) meters
        CAMERA_PITCH, -45.0, // (-45 to 80) degrees
        //CAMERA_POSITION, <0,0,0>, // region relative position
        CAMERA_POSITION_LAG, 1.0, // (0 to 3) seconds
        CAMERA_POSITION_LOCKED, FALSE, // (TRUE or FALSE)
        CAMERA_POSITION_THRESHOLD, 1.0, // (0 to 4) meters
        CAMERA_FOCUS_OFFSET, <0,0,0> // <-10,-10,-10> to <10,10,10> meters
    ]);
}


spaz_cam()
{
    llOwnerSay("spaz_cam for 5 seconds"); // say function name for debugging
    float i;
    for (i=0; i< 50; i+=1)
    {
        vector xyz = llGetPos() + <llFrand(80) - 40, llFrand(80) - 40, llFrand(10)>; // $[E20009]
//        llOwnerSay((string)xyz);
        vector xyz2 = llGetPos() + <llFrand(80) - 40, llFrand(80) - 40, llFrand(10)>;
        llSetCameraParams([
            CAMERA_ACTIVE, 1, // 1 is active, 0 is inactive
            CAMERA_BEHINDNESS_ANGLE, 180.0, // (0 to 180) degrees
            CAMERA_BEHINDNESS_LAG, llFrand(3), // (0 to 3) seconds
            CAMERA_DISTANCE, llFrand(10), // ( 0.5 to 10) meters
            //CAMERA_FOCUS, xyz, // region relative position
            CAMERA_FOCUS_LAG, llFrand(3), // (0 to 3) seconds
            CAMERA_FOCUS_LOCKED, TRUE, // (TRUE or FALSE)
            CAMERA_FOCUS_THRESHOLD, llFrand(4), // (0 to 4) meters
            CAMERA_PITCH, llFrand(125) - 45, // (-45 to 80) degrees
            CAMERA_POSITION, xyz2, // region relative position
            CAMERA_POSITION_LAG, llFrand(3), // (0 to 3) seconds
            CAMERA_POSITION_LOCKED, TRUE, // (TRUE or FALSE)
            CAMERA_POSITION_THRESHOLD, llFrand(4), // (0 to 4) meters
            CAMERA_FOCUS_OFFSET, <llFrand(20) - 10, llFrand(20) - 10, llFrand(20) - 10> // <-10,-10,-10> to <10,10,10> meters
            ]);
            llSleep(0.1);
    }
    default_cam();
}

spin_cam()
{
    llSetCameraParams([
        CAMERA_ACTIVE, 1, // 1 is active, 0 is inactive
        CAMERA_BEHINDNESS_ANGLE, 180.0, // (0 to 180) degrees
        CAMERA_BEHINDNESS_LAG, 0.5, // (0 to 3) seconds
        //CAMERA_DISTANCE, 10.0, // ( 0.5 to 10) meters
        //CAMERA_FOCUS, <0,0,5>, // region relative position
        CAMERA_FOCUS_LAG, 0.05 , // (0 to 3) seconds
        CAMERA_FOCUS_LOCKED, FALSE, // (TRUE or FALSE)
        CAMERA_FOCUS_THRESHOLD, 0.0, // (0 to 4) meters
        CAMERA_PITCH, 30.0, // (-45 to 80) degrees
        //CAMERA_POSITION, <0,0,0>, // region relative position
        CAMERA_POSITION_LAG, 0.0, // (0 to 3) seconds
        CAMERA_POSITION_LOCKED, FALSE, // (TRUE or FALSE)
        CAMERA_POSITION_THRESHOLD, 0.0, // (0 to 4) meters
        CAMERA_FOCUS_OFFSET, <0,0,0> // <-10,-10,-10> to <10,10,10> meters
    ]);
    
    float i;
    vector camera_position;
    for (i=0; i< 2*TWO_PI; i+=.05)
    {
        camera_position = llGetPos() + <0, 4, 0> * llEuler2Rot(<0,0,i>);
        llSetCameraParams([CAMERA_POSITION, camera_position]);
    }
    default_cam();
}

setup_listen()
{
    llListenRemove(1);
    CHANNEL = llRound(llFrand(1) * 100000);
    integer x = llListen(CHANNEL, "", "", ""); // listen for dialog answers   $[E20009]
}

default
{
    state_entry()
    {
        setup_listen();
        llSetTimerEvent(2);
    }
    
    touch_start(integer total_number) 
    {
integer perm = llGetPermissions();
if (perm & PERMISSION_DEBIT)
{ llOwnerSay(llGetScriptName() + " has PERMISSION_DEBIT perms for "+(string)llGetPermissionsKey()); }
if (perm & PERMISSION_TAKE_CONTROLS)
{ llOwnerSay(llGetScriptName() + " has PERMISSION_TAKE_CONTROLS perms for "+(string)llGetPermissionsKey()); }
if (perm & PERMISSION_TRIGGER_ANIMATION)
{ llOwnerSay(llGetScriptName() + " has PERMISSION_TRIGGER_ANIMATION perms for "+(string)llGetPermissionsKey()); }
if (perm & PERMISSION_ATTACH)
{ llOwnerSay(llGetScriptName() + " has PERMISSION_ATTACH perms for "+(string)llGetPermissionsKey()); }
if (perm & PERMISSION_CHANGE_LINKS)
{ llOwnerSay(llGetScriptName() + " has PERMISSION_CHANGE_LINKS perms for "+(string)llGetPermissionsKey()); }
if (perm & PERMISSION_TRACK_CAMERA)
{ llOwnerSay(llGetScriptName() + " has PERMISSION_TRACK_CAMERA perms for "+(string)llGetPermissionsKey()); }
if (perm & PERMISSION_CONTROL_CAMERA)
{ llOwnerSay(llGetScriptName() + " has PERMISSION_CONTROL_CAMERA perms for "+(string)llGetPermissionsKey()); }
if (perm == 0)
{ llOwnerSay(llGetScriptName() + " has NO perms for "+(string)llGetPermissionsKey()); }

        llDialog(llDetectedKey(0), "What do you want to do?", MENU_MAIN, CHANNEL); // present dialog on click
    }
    
    listen(integer channel, string name, key id, string message) 
    {
        if (llListFindList(MENU_MAIN + MENU_2, [message]) != -1)  // verify dialog choice
//        if (llListFindList(MENU_MAIN, [message]) != -1)  // verify dialog choice
        {
//            llOwnerSay(name + " picked the option '" + message + "'."); // output the answer
            if (message == "More...") 
                llDialog(id, "Pick an option!", MENU_2, CHANNEL); // present submenu on request
            else if (message == "...Back") 
                llDialog(id, "What do you want to do?", MENU_MAIN, CHANNEL); // present main menu on request to go back

            
    else if (message == "Cam ON")
    {
        take_camera_control(id);
    }

    else if (message == "Cam OFF") 
    {
        release_camera_control(id);
    }

    else if (message == "Default") 
    {
        default_cam();
    }

    else if (message == "Driving Cam") 
    {
        driving_cam();
    }

    else if (message == "Worm Cam") 
    {
        worm_cam();
    }

    else if (message == "Overhead Cam") 
    {
        overhead_cam();
    }

    else if (message == "Spaz Cam") 
    {
        spaz_cam();
    }

    else if (message == "Side Cam") 
    {
        side_cam();
    }

    else if (message == "Drop Cam") 
    {
        drop_camera_5_seconds();
    }

    else if (message == "Trap Toggle")
    {
        trap = !trap;
        if (trap == 1) {
            llOwnerSay("trap is on");
        }
        else {
            llOwnerSay("trap is off");
        }        
    }

    else if (message == "Spin Cam") 
    {
        spin_cam();
    }

        } else 
            llOwnerSay(name + " picked invalid option '" + llToLower(message) + "'."); // not a valid dialog choice
    }

    run_time_permissions(integer perm) {
        if ((perm & PERMISSION_CONTROL_CAMERA) == PERMISSION_CONTROL_CAMERA) {
            llSetCameraParams([CAMERA_ACTIVE, 1]); // 1 is active, 0 is inactive
            llOwnerSay("Camera permissions have been taken");
        }
    }
    
    changed(integer change)
    {
        if (change & CHANGED_LINK)
        {
            key agent = llAvatarOnSitTarget();
            if (agent)
            {
                setup_listen();
                llRequestPermissions(agent, PERMISSION_CONTROL_CAMERA);
            }
        }
    }
    
    attach(key agent)
    {
        if (agent != NULL_KEY)
        {
            setup_listen();
            llRequestPermissions(agent, PERMISSION_CONTROL_CAMERA);
        }
    }
    
    timer()
    {
        if (trap == 1)
        {
            focus_on_me();
        }        
    }
    
}


