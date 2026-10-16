default
{
    collision_start(integer i)
    {
        list a = llGetObjectDetails(llDetectedKey(0), ([OBJECT_NAME, 
                    OBJECT_DESC, OBJECT_POS, OBJECT_ROT, OBJECT_VELOCITY,
                    OBJECT_OWNER, OBJECT_GROUP, OBJECT_CREATOR]));
        llWhisper(0,"UUID: " + (string)llDetectedKey(0) +
                "\nName: \"" + llList2String(a,0)+ "\"" +
                "\nDecription: \"" + llList2String(a,1) + "\"" +
                "\nPosition: " + llList2String(a,2) +
                "\nRotation: " + llList2String(a,3) +
                "\nVelocity: " + llList2String(a,4) +
                "\nOwner: " + llList2String(a,5) +
                "\nGroup: " + llList2String(a,6) +
                "\nCreator: " + llList2String(a,7));
    }
}

