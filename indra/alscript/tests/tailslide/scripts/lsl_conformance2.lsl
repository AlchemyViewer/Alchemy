// https://wiki.secondlife.com/wiki/LSL_Language_Test_2
// This work uses content from the Second Life® Wiki.
// Copyright © 2007-2012 Linden Research, Inc. Licensed under the Creative Commons Attribution-Share Alike 3.0 License

integer gTestsPassed = 0;
integer gTestsFailed = 0;

key gNullKey = NULL_KEY;
key gStringInAKey = "foo";
string gKeyInAString = gNullKey;  // $[E20009]

integer gVisitedStateTest = FALSE;
integer chat = TRUE; // Chat is not a reserved word.

testPassed(string description, string actual, string expected)  // $[E20009] $[E20009] $[E20009]
{
    ++gTestsPassed;
    //llSay(0, description);
}

testFailed(string description, string actual, string expected)
{
    ++gTestsFailed;
    llSay(0, "FAILED!: " + description + " (" + actual + " expected " + expected + ")");
}

ensureTrue(string description, integer actual)
{
    if(actual)
    {
        testPassed(description, (string) actual, (string) TRUE);
    }
    else
    {
        testFailed(description, (string) actual, (string) TRUE);
    }
}

ensureFalse(string description, integer actual)
{
    if(actual)
    {
        testFailed(description, (string) actual, (string) FALSE);
    }
    else
    {
        testPassed(description, (string) actual, (string) FALSE);
    }
}

ensureIntegerEqual(string description, integer actual, integer expected)
{
    if(actual == expected)
    {
        testPassed(description, (string) actual, (string) expected);
    }
    else
    {
        testFailed(description, (string) actual, (string) expected);
    }
}

integer floatEqual(float actual, float expected)
{
    float error = llFabs(expected - actual);
    float epsilon = 0.001;
    if(error > epsilon)
    {
        llSay(0,"Float equality delta " + (string)error);
        return FALSE;
    }
    return TRUE;
}

ensureFloatEqual(string description, float actual, float expected)
{
    if(floatEqual(actual, expected))
    {
        testPassed(description, (string) actual, (string) expected);
    }
    else
    {
        testFailed(description, (string) actual, (string) expected);
    }
}

ensureFloatExactEqual(string description, float actual, float expected)
{
    if(actual == expected)
    {
        testPassed(description, (string) actual, (string) expected);
    }
    else
    {
        testFailed(description, (string) actual, (string) expected);
    }
}

ensureStringEqual(string description, string actual, string expected)
{
    if(actual == expected)
    {
        testPassed(description, (string) actual, (string) expected);
    }
    else
    {
        testFailed(description, (string) actual, (string) expected);
    }
}

ensureKeyEqual(string description, key actual, key expected)
{
    if(actual == expected)
    {
        testPassed(description, (string) actual, (string) expected);
    }
    else
    {
        testFailed(description, (string) actual, (string) expected);
    }
}

ensureVectorEqual(string description, vector actual, vector expected)
{
    if(floatEqual(actual.x, expected.x) &&
        floatEqual(actual.y, expected.y) &&
        floatEqual(actual.z, expected.z))
    {
        testPassed(description, (string) actual, (string) expected);
    }
    else
    {
        testFailed(description, (string) actual, (string) expected);
    }
}

ensureRotationEqual(string description, rotation actual, rotation expected)
{
    if(floatEqual(actual.x, expected.x) &&
        floatEqual(actual.y, expected.y) &&
        floatEqual(actual.z, expected.z) &&
        floatEqual(actual.s, expected.s))
    {
        testPassed(description, (string) actual, (string) expected);
    }
    else
    {
        testFailed(description, (string) actual, (string) expected);
    }
}

ensureListEqual(string description, list actual, list expected)
{
    if(actual == expected)
    {
        testPassed(description, (string) actual, (string) expected);
    }
    else
    {
        testFailed(description, (string) actual, (string) expected);
    }
}

vector gVector;

float testReturnFloat()
{
    return 1.0;
}


key testReturnKey()
{
    return NULL_KEY;
}

testArgumentAccessor(vector v)
{
    v = <0,0,0>;
    v.x = testReturnFloat();
    v.y = testReturnFloat();
    v.z = testReturnFloat();
    ensureVectorEqual("testArgumentAccessor", v, <1,1,1>);
}

testLocalAccessor()
{
    vector v = <0,0,0>;
    v.x = testReturnFloat();
    v.y = testReturnFloat();
    v.z = testReturnFloat();
    ensureVectorEqual("testLocalAccessor", v, <1,1,1>);
}

testGlobalAccessor()
{
    gVector = <0,0,0>;
    gVector.x = testReturnFloat();
    gVector.y = testReturnFloat();
    gVector.z = testReturnFloat();
    ensureVectorEqual("testGlobalAccessor", gVector, <1,1,1>);
}

tests()
{
    // Ensure that multiple states containing the same handler work (SVC-1372 Regression)
    ensureTrue("gVisitedStateTest ", gVisitedStateTest);

    ensureFloatExactEqual("1.4e-45 == (float)\"1.4e-45\"", 1.4e-45, (float)"1.4e-45");

    // Managed pointer save/restore tests.
    testArgumentAccessor(<0,0,0>);
    testLocalAccessor();
    testGlobalAccessor();

    // Multiple list element saving (SVC-1712 Regression)
    ensureStringEqual("(string)[1, testReturnFloat(), testReturnFloat()] == \"11.01.0\"", (string)[1, testReturnFloat(), testReturnFloat()], "11.0000001.000000");

    // Key equality (SVC-1720 Regression)
    key k = "foo";
    key k2 = "foo";
    ensureKeyEqual("k = \"foo\";k2 = \"foo\";k == k2", k, k2);

    // List comparison. Not actual comparison, just length.
    ensureIntegerEqual("[] == []", [] == [], 1);
    ensureIntegerEqual("[1] == [2]", [1] == [2], 1);  // $[E20011]
    ensureIntegerEqual("[1,2] == [1,2]", [1,2] == [1,2], 1);  // $[E20011]
    ensureIntegerEqual("[1,2] == [2]", [1,2] == [2], 0);  // $[E20011]
    ensureIntegerEqual("[1] == [2,3]", [1] == [2,3], 0);  // $[E20011]

    ensureIntegerEqual("[] != []", [] != [], 0);
    ensureIntegerEqual("[1] != [2]", [1] != [2], 0);  // $[E20011]
    ensureIntegerEqual("[1,2] != [1,2]", [1,2] != [1,2], 0);  // $[E20011]
    ensureIntegerEqual("[1,2] != [2]", [1,2] != [2], 1);  // $[E20011]
    ensureIntegerEqual("[1] != [2,3]", [1] != [2,3], -1);  // $[E20011]

    // Parsing numbers with junk after
    ensureFloatEqual("(float)\"5.0a\" == 5.0", (float) "5a", 5.0);
    ensureFloatEqual("(integer)\"12foo\" == 12", (integer) "12foo", 12);

    // List value semantics across addition (SVC-1448 Regression)
    list l;
    l = [42];
    l + 1;
    ensureListEqual("list l = [42]; l + 1; l == [42]", l, [42]);
    l = [42];
    l + 1.5;
    ensureListEqual("list l = [42]; l + 1.5; l == [42]", l, [42]);
    l = [42];
    l + NULL_KEY;
    ensureListEqual("list l = [42]; l + NULL_KEY; l == [42]", l, [42]);
    l = [42];
    l + <1,2,3>;
    ensureListEqual("list l = [42]; l + <1,2,3>; l == [42]", l, [42]);
    l = [42];
    l + <1,2,3,4>;
    ensureListEqual("list l = [42]; l + <1,2,3>; l == [42]", l, [42]);


    // Conditional test of key (SVC-1776/SVC-1811 Regression)
    if(k)
    {
        testFailed("if(k)", "TRUE", "FALSE");
    }
    else
    {
        testPassed("if(k)", "FALSE", "FALSE");
    }

    // String/Key equality (SVC-1770 Regression)
    ensureIntegerEqual("k == \"foo\"",k == "foo", TRUE);

    // String/Key equality (SVC-1772 Regression)
    ensureIntegerEqual("gStringInAKey == \"foo\"", gStringInAKey == "foo", TRUE);
    ensureIntegerEqual("gNullKey == NULL_KEY", gNullKey == NULL_KEY, TRUE);

    // Key to string cast on assignment (SVC-1707 Regression)
    k = NULL_KEY;
    string s;
    s = k;
    ensureStringEqual("k = NULL_KEY; string s; s = k;", s, NULL_KEY);

    // Loop semantics for vectors
    vector v = <1,1,1>;
    while (v) { v = ZERO_VECTOR; }
    ensureVectorEqual( "while (v) { v = ZERO_VECTOR; }", v, ZERO_VECTOR);

    v = <2,2,2>;
    do {  v = ZERO_VECTOR; } while (v);
    ensureVectorEqual( "v = <2,2,2>; do { v = ZERO_VECTOR } while (v);", v, ZERO_VECTOR);

    for (v = <3,3,3>;v;v=ZERO_VECTOR) {}  // $[E20014]
    ensureVectorEqual( "for (v = <3,3,3>;v;v=ZERO_VECTOR) {}", v, ZERO_VECTOR);

    // Loops with keys
    k = "7c42811e-229f-4500-b6d7-2c37324ff816";
    while (k) { k = NULL_KEY; }
    ensureKeyEqual( "while (k) { k = NULL_KEY; }", k, NULL_KEY);

    k = "7c42811e-229f-4500-b6d7-2c37324ff816";
    do {  k = NULL_KEY; } while (k);
    ensureKeyEqual( "k = \"7c42811e-229f-4500-b6d7-2c37324ff816\"; do { k = NULL_KEY } while (k);", k, NULL_KEY);

    for (k = "7c42811e-229f-4500-b6d7-2c37324ff816"; k; k=NULL_KEY) {}  // $[E20014]
    ensureKeyEqual( "for (k = \"7c42811e-229f-4500-b6d7-2c37324ff816\";k;k=NULL_KEY) {}", k, NULL_KEY);

    // Loops with rotations
    rotation q = <1,1,1,1>;
    while (q) { q = ZERO_ROTATION; }
    ensureRotationEqual( "while (q) { q = ZERO_ROTATION; }", q, ZERO_ROTATION);

    q = <2,2,2,2>;
    do {  q = ZERO_ROTATION; } while (q);
    ensureRotationEqual( "q = <2,2,2>; do { v = ZERO_ROTATION } while (q);", q, ZERO_ROTATION);

    for (q = <3,3,3,3>; q; q=ZERO_ROTATION) {}  // $[E20014]
    ensureRotationEqual( "for (q = <3,3,3,3>;q;q=ZERO_ROTATION) {}", q, ZERO_ROTATION);

    // Loops with lists
    l = [1];
    //while (l) { l = []; }
    //ensureListEqual( "while (l) { l = []; }", l, []); // Fails on LSL, SVC-689 incompatibility

    l = [2];
    do { l = []; } while (l);
    ensureListEqual( "l = [2]; do { v = [] } while (l);", l, []);

    //for (l = [3]; l; l=[]) {}
    //ensureListEqual( "for (l = [3];l;l=[]) {}", l, []);  // Fails on LSL, SVC-689 incompatibility

    // Loop for strings
    s = "1!";
    while (s) { s = ""; }
    ensureStringEqual( "while (s) { s = \"\"; }", s, "");

    s = "2!";
    do {  s = ""; } while (s);
    ensureStringEqual( "s = \"2!\"; do { s = \"\" } while (s);", s, "");

    for (s = "3!";s;s="") {}  // $[E20014]
    ensureStringEqual( "for (s = \"3!\";s;s=\"\") {}", s, "");

    // Loop for integers
    integer i = 1;
    while (i) { i = 0; }
    ensureIntegerEqual( "while (i) { i = 0; }", i, 0);

    i = 2;
    do {  i = 0; } while (i);
    ensureIntegerEqual( "i = 2; do { i = 0 } while (i);", i, 0);

    for (i = 3;i;i=0) {}  // $[E20014]
    ensureIntegerEqual( "for (i = 3;i;i=0) {}", i, 0);

    // Loop for floats
    float f = 1;
    while (f) { f = 0; }
    ensureFloatEqual( "while (f) { f = 0; }", f, 0);

    f = 2;
    do {  f = 0; } while (f);
    ensureFloatEqual( "f = 2; do { f = 0 } while (f);", f, 0);

    for (f = 3;f;f=0) {}  // $[E20014]
    ensureFloatEqual( "for (f = 3;f;f=0) {}", f, 0);


    // Unary vector multiplication
    v = <1,2,3>;
    v *= 2;
    ensureVectorEqual("v = <1,2,3>; v *= 2;", v, <2,4,6>);

    v = <1,2,3>;
    v *= 0.5;
    ensureVectorEqual("v = <1,2,3>; v *= 0.5;", v, <0.5,1,1.5>);

    // Indentify rotation
    v = <1,0,0>;
    v *= ZERO_ROTATION;
    ensureVectorEqual("vector v = <1,0,0>; v *= ZERO_ROTATION;", v, <1,0,0>);

    // Unary vector cross product (note no unary dot product exists)
    v = <0,1,0>;
    v %= <0,0,1>;
    ensureVectorEqual("vector v = <0,1,0>; v %= <0,0,1>;", v, <1,0,0>);

    // Unary rotation
    rotation r = ZERO_ROTATION;
    r *= ZERO_ROTATION;
    ensureRotationEqual("rotation r = ZERO_ROTATION; r *= ZERO_ROTATION;",r,ZERO_ROTATION);


    // SVC-1399 Floating point negative zero
    ensureStringEqual("(string)-0.0", (string)-0.0, "-0.000000");
    ensureStringEqual("(string)<-0.0,0.0,-0.0>", (string)<-0.0,0.0,-0.0>, "<-0.00000, 0.00000, -0.00000>");
    ensureStringEqual("(string)<-0.0,0.0,-0.0,0.0>",
        (string)<-0.0,0.0,-0.0,0.0>, "<-0.00000, 0.00000, -0.00000, 0.00000>");

    ensureStringEqual("llList2CSV([-0.0, <-0.0,0.0,-0.0>, <-0.0,0.0,-0.0,0.0>])",
        llList2CSV([-0.0, <-0.0,0.0,-0.0>, <-0.0,0.0,-0.0,0.0>]),
        "-0.000000, <-0.000000, 0.000000, -0.000000>, <-0.000000, 0.000000, -0.000000, 0.000000>");

    ensureStringEqual("llDumpList2String([-0.0, <-0.0,0.0,-0.0>, <-0.0,0.0,-0.0,0.0>], \" ~ \")",
        llDumpList2String([-0.0, <-0.0,0.0,-0.0>, <-0.0,0.0,-0.0,0.0>], " ~ "),
        "-0.000000 ~ <-0.000000, 0.000000, -0.000000> ~ <-0.000000, 0.000000, -0.000000, 0.000000>");

    ensureStringEqual("(string)[-0.0, <-0.0,0.0,-0.0>, <-0.0,0.0,-0.0,0.0>]",
        (string)[-0.0, <-0.0,0.0,-0.0>, <-0.0,0.0,-0.0,0.0>],
        "-0.000000<-0.000000, 0.000000, -0.000000><-0.000000, 0.000000, -0.000000, 0.000000>");

    ensureStringEqual("llList2String([-0.0], 0)", llList2String([-0.0], 0), "-0.000000");

    ensureStringEqual("(string)(float)\"-0.0\"", (string)((float)"-0.0"), "-0.000000");
    ensureStringEqual("(string)(vector)\"<-0.0,0.0,-0.0>\"",
        (string)((vector)"<-0.0,0.0,-0.0>"), "<-0.00000, 0.00000, -0.00000>");
    ensureStringEqual("(string)(rotation)\"<-0.0,0.0,-0.0,0.0>\"",
        (string)((rotation)"<-0.0,0.0,-0.0,0.0>"), "<-0.00000, 0.00000, -0.00000, 0.00000>");

    ensureKeyEqual("testReturnKey() == NULL_KEY", testReturnKey(), NULL_KEY);


    key defaultKey;
    ensureKeyEqual("key defaultKey;", defaultKey, "");

    integer defaultInteger;
    ensureIntegerEqual("key defaultInteger;", defaultInteger, 0);

    float defaultFloat;
    ensureFloatEqual("key defaultInteger;", defaultFloat, 0);

    string defaultString;
    ensureStringEqual("string defaultString;", defaultString, "");

    ensureIntegerEqual("chat == TRUE", chat, TRUE);

}

runTests()
{
    llResetTime();
    tests();
    llSay(0, "Ran " + (string)(gTestsPassed + gTestsFailed) + " tests in " + (string) llGetTime() + " seconds with " + (string)gTestsFailed + " failures");

    // Reset globals.
    gVisitedStateTest = FALSE;
    gTestsPassed = 0;
    gTestsFailed = 0;
}

default
{
    state_entry()
    {
        state StateTest;
    }

    touch_start(integer total_number)
    {
        state StateTest;
    }
}

state StateTest
{
    state_entry()
    {
        ensureFalse("gVisitedStateTest", gVisitedStateTest);
        gVisitedStateTest = TRUE;
        runTests();
    }

    touch_start(integer total_number)
    {
        state default;
    }
}
