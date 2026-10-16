integer gi;
float gf;
string gs;
key gk;
vector gv;
rotation gr;
list gl;

default {
    touch_start(integer num_detected) {
        integer li;
        float lf;
        string ls;
        key lk;
        vector lv;
        rotation lr;
        list ll;

        li = gi = 1;
        lf = gf = 1.0;
        ls = gs = "foo";
        lk = gk = llGetKey();
        lv = gv = <1.0, 1.0, 1.0>;
        lr = gr = <1.0, 1.0, 1.0, 1.0>;
        ll = gl = [1];
        lv.x = gv.x = 1.0;
        li = gi = num_detected;
        num_detected = 1;
        lk = "foobar";
        ls = lk;
    }
}
