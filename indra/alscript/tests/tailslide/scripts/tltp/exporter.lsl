// TLML Exporter v0.12, drop in a page's prim to get the corresponding TLML code

// ===How to get unicode characters in your llSetText_text===
// "\uxxxx"     where xxxx are 4 hex characters, unicode character 0000xxxx
// "\Uxxxxxxxx" where xxxxxxxx are 8 hex characters, unicode character xxxxxxxx
// "\rXxxx"     where X is a hex character giving the number of xxxxx hex character pairs
//                if X equals 2 then 4 hex characters follows. This is useful when multiple
//                unicode characters are needed. Be sure to break up your UTF-8 string on a
//                character boundry or the character will not be included.

string llSetText_text     = "";
vector llSetText_color    = <1.0,1.0,1.0>;
float  llSetText_alpha    = 1.0;

vector llTargetOmega_axis       = ZERO_VECTOR;
float  llTargetOmega_spinrate   = 0.0;
float  llTargetOmega_gain       = 0.0;

//ANIM_ON	==	0x01
//LOOP  	==	0x02
//REVERSE 	==	0x04
//PING_PONG ==	0x08
//SMOOTH 	==	0x10
//ROTATE  	==	0x20
//SCALE  	==	0x40

integer llSetTextureAnim_mode = 0;
integer llSetTextureAnim_face = ALL_SIDES;
integer llSetTextureAnim_x_frames = 2;
integer llSetTextureAnim_y_frames = 2;
float 	llSetTextureAnim_start_frame = 0;
float 	llSetTextureAnim_end_frame = 3;
float   llSetTextureAnim_rate = 0.1;

list llParticleSystem_list      = [];

string TLML_URL                 = "url";

//////////////////////////////////////////////////////////////
//               DO NOT MODIFY ANYTHING BELOW               //
//////////////////////////////////////////////////////////////
//This script is a nightmare, only the brave should venture deeper
//adding support for new features is pretty easy
//but don't touch the structure of the script
//because of the complexities of TLML, the data layout is complex
//the *right* was to do this would be with 3 passes
//1) determin the mask
//2) parse the masks and filling in the data
//Instead we do it with basicly a single pass
//Then as the masks become aparent we fill insert them into the stream.
//This saves alot of time with minimal expense
//Unfortunately it's horribly complex

//{
string byte2hex(integer x)
{//Helper function for use with unicode characters.
    integer x0  = (x & 0xF);
    return llGetSubString(hexc, x0 = ((x >> 4) & 0xF), x0) + llGetSubString(hexc, x0, x0);
}

string Unescape(string a)
{
    string  b = a;
    integer c = -1;
    integer d;
    integer e;
    integer f = 0; 
    string g;
    while(d = llSubStringIndex(b, "\\") + 1)
    {
        g = llGetSubString(b,d,d);
        c += d;

        if((g == "\"") || (g == "\\"))
            a = llDeleteSubString(a,c,c);
        else if(g == "n")
            a = llInsertString(llDeleteSubString(a,c,c+1), c, "\n");
        else if(g == "t")
            a = llInsertString(llDeleteSubString(a,c,c+1), c, "\t");
        else if(g == "r")//rx[11,22,33,44,55,66,77,88,99,AA,BB,CC,DD,EE,FF]
        {
            g = "";
            if(d+(e = (integer)("0x"+llGetSubString(b,d+1,d+1)) * 2)+1 >= (f = llStringLength(b)))
                e = (f - d - 2) & -2;
            if(f = e)//this may look like a mistake, it's not $[E20002]
            {
                do
                    g = "%"+llGetSubString(b,d + e,d + e + 1) + g;
                while((e-=2) > 0);
            }
            a = llInsertString(llDeleteSubString(a,c, c + 2 + f),c, g = llUnescapeURL(g));
            c += llStringLength(g);//add to c so we don't accidentily unescape result
        }
        else if(g == "u" || (e = (g == "U")))// \uXXXX or  \UXXXXXXXX
        {
            a = llDeleteSubString(a, c, c + 5 + e *= 4);
            if(0 < e = (integer)("0x"+llGetSubString(b,d +1, d +4 + e)))
            {
                if (e >= 0x4000000)
                    f = 5;
                else if (e >= 0x200000)
                    f = 4;
                else if (e >= 0x10000)
                    f = 3;
                else if (e >= 0x800)
                    f = 2;
                else if (e >= 0x80)
                    f = 1;
                g = "%" + byte2hex((e >> (6 * f)) | ((0x3F80 >> f) * (0 != f)));
                while(f)
                    g += "%" + byte2hex((((e >> (6 * --f)) | 0x80) & 0xBF));
                a = llInsertString(a, c++, llUnescapeURL(g));
            }
        }
        b = llDeleteSubString(a,0,c);
    }
    return a;
}

string flo(float a)
{
    string b = (string)a;
    while(llGetSubString(b,-1,-1) == "0")
        b=llDeleteSubString(b,-1,-1);
    if(llGetSubString(b,-1,-1) == ".")
        return llDeleteSubString(b,-1,-1);
    if(llGetSubString(b,(a<0),(a<0)+1)=="0.")
        return llDeleteSubString(b,(a<0),(a<0));
    return b;
}

string vec(vector a)
{
    if(a == ZERO_VECTOR) return "";
    return "<"+flo(a.x)+","+flo(a.y)+","+flo(a.z)+">";
}

string rot(rotation a)
{
    if(a == ZERO_ROTATION) return "";
    return "<"+flo(a.x)+","+flo(a.y)+","+flo(a.z)+","+flo(a.s)+">";
}

string int(integer a)
{
    if(a == 0) return "";
    return (string)a;
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

list lis(list a)
{
    integer b = -llGetListLength(a) - 1;
    list c;
    integer d;
    while(++b)
    {
        if((d = llGetListEntryType(a,b)) == TYPE_FLOAT)
        {
            float e = llList2Float(a,b);
            if(e != 0.0)
                c += flo(e);
            else
                c += "";
        }
        else if(d == TYPE_VECTOR)
            c += vec(llList2Vector(a,b));
        else if(d == TYPE_ROTATION)
            c += rot(llList2Rot(a,b));
        else if(d == TYPE_INTEGER)
            c += int(llList2Integer(a,b));
        else
            c += llList2String(a,b);
    }
    return c;
}

string hex(integer x) 
{
    integer x0 = x & 0xF;
    string res = llGetSubString(hexc, x0, x0);
    x = (x >> 4) & 0x0FFFFFFF; //otherwise we get infinite loop on negatives.
    while( x != 0 )
    {
        x0 = x & 0xF;
        res = llGetSubString(hexc, x0, x0) + res;
        x = x >> 4;
    } 
    return res;
}
//}
add(list value, integer mask)
{
    if(llStringLength(llDumpList2String(header+params+  value," ")) > 240)
    {
        store();
        mode = mask | (mode & 0x101);
    }
    else
        mode = mode | mask;
	params += value;
}

store()
{
    integer a;
	integer b = 0;
	if(llList2String(params,-1) != sep)//it's possible we might be on a boarder already.
		params += mode;
	else
		params = llDeleteSubList(params,-1,-1);
//	llOwnerSay(TightListDump(params,""));
	@loop;
	if(1 + (a = b))
		if(b = 1 + llListFindList(llList2List(params + [sep],a + 1,200),[sep])) // $[E20002]
	{
		b += a;
//		llOwnerSay(llList2CSV([a,b] + llList2List(params,b - 1, b)));
		params = llListInsertList(llDeleteSubList(params,b - 1, b),[hex(llList2Integer(params, b - 1) | (!a << 5))], a);
		jump loop;
	}
	params = [TightListDump(params,"")];
//    llOwnerSay(llList2CSV(params));
    commands += params;
	params = [sep];
	if(mode & 0x100)
		params += cface;
    recycle_mask = recycle_mask | mode;
}

break()
{
	if(mode & 0xFFFffFB0)
	{
		params += mode;
		recycle_mask = recycle_mask | mode;
		mode = 1;
		params += sep;
	}
}
//{
theFace(integer f)
{
    if (multiplefaces)
        add([f], 0x100);

    if (llList2Integer(llGetPrimitiveParams([PRIM_FULLBRIGHT, f]), 0))
        mode = mode | 0x10;

	if (llGetTexture(f) != "5748decc-f629-461c-9a36-a35a221fe21f")
		add([llGetTexture(f)], 0x200);

    vector t_v = llGetTextureScale(f);
    if (t_v != <1.0, 1.0, 0.0>)
        add([vec(t_v)], 0x400);

    t_v = llGetTextureOffset(f);
    if (t_v != ZERO_VECTOR)
        add([vec(t_v)], 0x800);
    else
        mode = mode | 0x2;

    float t_f = llGetTextureRot(f);
    if (t_f != 0.0)
        add([flo(t_f)], 0x1000);
    else
        mode = mode | 0x2;


    t_v = llGetColor(f);
    if (t_v != <1.0, 1.0, 1.0>)
        add([vec(t_v)], 0x2000);
    else
        mode = mode | 0x4;

    t_f = llGetAlpha(f);
    if  (f != 1.0)
        add([flo(t_f)], 0x4000);
    else
        mode = mode | 0x4;

    list t_l = llGetPrimitiveParams([PRIM_BUMP_SHINY, f]);
    integer t_i = llList2Integer(t_l, 0);
    if (t_i != PRIM_SHINY_NONE)
        add([t_i], 0x8000);
    else
        mode = mode | 0x8;

    t_i = llList2Integer(t_l, 1);
    if (t_i != PRIM_BUMP_NONE)
        add([t_i], 0x10000);
    else
        mode = mode | 0x8;
}

checkFaces()
{
    integer max = llGetNumberOfSides();
    multiplefaces = FALSE;
    string texture = llGetTexture(0);
    vector color = llGetColor(0);
    float alpha = llGetAlpha(0);
    list fullbrights = llGetPrimitiveParams([PRIM_FULLBRIGHT, ALL_SIDES]);
    integer fullbright = llList2Integer(fullbrights, 0);
    list bump_shiny = llGetPrimitiveParams([PRIM_BUMP_SHINY, ALL_SIDES]);
    integer bump = llList2Integer(bump_shiny,1);
    integer shiny = llList2Integer(bump_shiny,0);

    integer i = 1;
    for (; i<max && !multiplefaces; ++i)
    {
        if (llGetTexture(i) != texture)
            multiplefaces = TRUE;

        if (llGetColor(i) != color)
            multiplefaces = TRUE;

        if (llGetAlpha(i) != alpha)
            multiplefaces = TRUE;

        if (llList2Integer(fullbrights, i) != fullbright)
            multiplefaces = TRUE;

        if (llList2Integer(bump_shiny, i* 2 + 1) != bump)
            multiplefaces = TRUE;

        if (llList2Integer(bump_shiny, i* 2) != shiny)
            multiplefaces = TRUE;
    }
    if(!multiplefaces)
    {
        mode = mode | 0x1;
		cface = ALL_SIDES;
    } else {
        cface = 0;
    }

    theFace(0);
}

checkPrim()
{
    list type = llGetPrimitiveParams([PRIM_TYPE]);

    if (llList2Integer(type, 0) == 0)
    {
        if (llList2Integer(type, 1) == 0)
        {
            if (llList2Vector(type, 2) == <0.0, 1.0, 0.0>)
            {
                if (llList2Float(type, 3) == 0.0)
                {
                    if (llList2Vector(type, 4) == ZERO_VECTOR)
                    {
                        if (llList2Vector(type, 5) == <1.0, 1.0, 0.0>)
                        {
                            if (llList2Vector(type, 6) == ZERO_VECTOR)
                            {
                                return;
                            }
                        }
                    }
                }
            }
        }
    }
    add(lis(type), 0x4000000);
}
//}
string sep;

integer mode;

integer cface;
integer multiplefaces;

list params;
list header;

integer recycle_mask;

list commands;

string hexc="0123456789ABCDEF";

default
{
    state_entry()
    {
        llSetText(Unescape(llSetText_text), llSetText_color, llSetText_alpha);
        llTargetOmega(llTargetOmega_axis, llTargetOmega_spinrate, llTargetOmega_gain);
        llParticleSystem(llParticleSystem_list);
        llSetTextureAnim(llSetTextureAnim_mode, llSetTextureAnim_face, llSetTextureAnim_x_frames, llSetTextureAnim_y_frames,
                                llSetTextureAnim_start_frame,llSetTextureAnim_end_frame,llSetTextureAnim_rate);

        llOwnerSay("--------------------------------------");

        sep = llUnescapeURL("%01");
//        sep = "~~~~";

        if((llGetObjectPermMask(MASK_OWNER) & 0x0000E000) != 0x0000E000)
        {//If you remove this check the script will not function any better.
        //Many of the functions used to gather information do the same permissions check internaly
        //Those functions will cause error messages and an invalid TLML stream.
        //YOU HAVE BEEN WARNED.
            llOwnerSay("You cannot clone an object you do not have full permission on");
            return;
        }

        header = [llGetLinkNumber(), TLML_URL];

        params = [sep];

		checkFaces();//   	  0x100 ->  0x10000

		add([vec(llGetScale())], 0x20000);
		if (llGetLinkNumber() >= 2)
			add([vec(llGetLocalPos())], 0x40000);

		rotation local = llGetLocalRot();
		if (local != ZERO_ROTATION)
			add([rot(llGetLocalRot())], 0x100000);


		if(llSetText_text != "")    // $[E20013]
			add([llSetText_text], 0x800000);
		if(llSetText_color != <1.0,1.0,1.0> || llSetText_alpha != 1.0)  // $[E20013]
			add([vec(llSetText_color),flo(llSetText_alpha)], 0x1000000);

		if(llTargetOmega_axis != ZERO_VECTOR || llTargetOmega_spinrate != 0.0 | llTargetOmega_gain != 0.0) // $[E20013]
			add([vec(llTargetOmega_axis),flo(llTargetOmega_spinrate  ),flo(llTargetOmega_gain)], 0x2000000);

		checkPrim();//0x4000000

		if(llParticleSystem_list != []) // $[E20013]
			add([TightListDump(lis(llParticleSystem_list),"*")], 0x10000000);

        integer t;
        if(multiplefaces)
        {
            t = llGetNumberOfSides();
			cface = 1;
            while(cface < t)
            {
				break();
				theFace(cface);
				++cface;
            }
        }
		if(llSetTextureAnim_mode)   // $[E20013]
		{
			break();
			add([llSetTextureAnim_face, llSetTextureAnim_mode, llSetTextureAnim_x_frames, llSetTextureAnim_y_frames,
					llSetTextureAnim_start_frame,llSetTextureAnim_end_frame,llSetTextureAnim_rate], 0x200100);
		}
		store();
        t = llGetListLength(commands);

        integer c;
        string d;
        string e;
        string f;
        if(t > 1)
            d = hex(recycle_mask);

//        llOwnerSay("-----------------------");
        while(c < t)
        {
            e = llDumpList2String(llParseString2List(llList2String  (commands,c),[sep],[]),d);
            f = TightListDump(header,llGetSubString(e,0,0));
            llOwnerSay("T"+f+e);
            if(++c == 1)
				header = [llGetLinkNumber(), ""];
        }

//        llRemoveInventory(llGetScriptName());
    }
}

