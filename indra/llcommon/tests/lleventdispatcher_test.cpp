/**
 * @file   lleventdispatcher_test.cpp
 * @author Nat Goodspeed
 * @date   2011-01-20
 * @brief  Test for lleventdispatcher.
 *
 * $LicenseInfo:firstyear=2011&license=viewerlgpl$
 * Copyright (c) 2011, Linden Research, Inc.
 * $/LicenseInfo$
 */

// Precompiled header
#include "linden_common.h"
// associated header
#include "lleventdispatcher.h"
// STL headers
// std headers
// external library headers
// other Linden headers
#include <catch2/catch_test_macros.hpp>
#include "altestmatchers.h"
#include "lleventfilter.h"
#include "llsd.h"
#include "llsdutil.h"
#include "llevents.h"
#include "stringize.h"
#include "StringVec.h"
#include "tests/wrapllerrs.h"
#include "../test/catch_and_store_what_in.h"
#include "../test/debug.h"

#include <map>
#include <string>
#include <stdexcept>

#include <boost/bind.hpp>
#include <boost/range.hpp>

#include <boost/lambda/lambda.hpp>

#include <iostream>
#include <iomanip>

using boost::lambda::constant;
using boost::lambda::constant_ref;
using boost::lambda::var;

using namespace llsd;

/*****************************************************************************
*   Example data, functions, classes
*****************************************************************************/
// We don't need a whole lot of different arbitrary-params methods, just (no |
// (const LLSD&) | arbitrary) args (function | static method | non-static
// method), where 'arbitrary' is (every LLSD datatype + (const char*)).
// But we need to register each one under different names for the different
// registration styles. Don't forget LLEventDispatcher subclass methods(const
// LLSD&).

// However, the number of target parameter conversions we want to try exceeds
// boost::fusion::invoke()'s supported parameter-list size. Break out two
// different lists.
#define NPARAMSa bool b, int i, float f, double d, const char* cp
#define NPARAMSb const std::string& s, const LLUUID& uuid, const LLDate& date, \
                 const LLURI& uri, const std::vector<U8>& bin
#define NARGSa   b, i, f, d, cp
#define NARGSb   s, uuid, date, uri, bin

namespace
{

// For some registration methods we need methods on a subclass of
// LLEventDispatcher. To simplify things, we'll use this Dispatcher subclass
// for all our testing, including testing its own methods.
class Dispatcher: public LLEventDispatcher
{
public:
    Dispatcher(const std::string& name, const std::string& key):
        LLEventDispatcher(name, key)
    {}

    // sensing member, mutable because we want to know when we've reached our
    // const method too
    mutable LLSD llsd;

    void method1(const LLSD& obj) { llsd = obj; }
    void cmethod1(const LLSD& obj) const { llsd = obj; }
};

// sensing vars, captured in a struct to make it convenient to clear them
struct Vars
{
    LLSD llsd;
    bool b;
    int i;
    float f;
    double d;
    // Capture param passed as char*. But merely storing a char* received from
    // our caller, possibly the .c_str() from a concatenation expression,
    // would be Bad: the pointer will be invalidated long before we can query
    // it. We could allocate a new chunk of memory, copy the string data and
    // point to that instead -- but hey, guess what, we already have a class
    // that does that!
    std::string cp;
    std::string s;
    LLUUID uuid;
    LLDate date;
    LLURI uri;
    std::vector<U8> bin;

    Vars():
        // Only need to initialize the POD types, the rest should take care of
        // default-constructing themselves.
        b(false),
        i(0),
        f(0),
        d(0)
    {}

    // Detect any non-default values for convenient testing
    LLSD inspect() const
    {
        LLSD result;

        if (llsd.isDefined())
            result["llsd"] = llsd;
        if (b)
            result["b"] = b;
        if (i)
            result["i"] = i;
        if (f)
            result["f"] = f;
        if (d)
            result["d"] = d;
        if (! cp.empty())
            result["cp"] = cp;
        if (! s.empty())
            result["s"] = s;
        if (uuid != LLUUID())
            result["uuid"] = uuid;
        if (date != LLDate())
            result["date"] = date;
        if (uri != LLURI())
            result["uri"] = uri;
        if (! bin.empty())
            result["bin"] = bin;

        return result;
    }

    /*------------- no-args (non-const, const, static) methods -------------*/
    void method0()
    {
        debug()("method0()");
        i = 17;
    }

    void cmethod0() const
    {
        debug()('c', NONL);
        const_cast<Vars*>(this)->method0();
    }

    static void smethod0();

    /*------------ Callable (non-const, const, static) methods -------------*/
    void method1(const LLSD& obj)
    {
        debug()("method1(", obj, ")");
        llsd = obj;
    }

    void cmethod1(const LLSD& obj) const
    {
        debug()('c', NONL);
        const_cast<Vars*>(this)->method1(obj);
    }

    // Nothing registers it, but it keeps the set of three whole; internal
    // now, and so unused as far as GCC's -Wunused-function is concerned.
    [[maybe_unused]] static void smethod1(const LLSD& obj);

    /*-------- Arbitrary-params (non-const, const, static) methods ---------*/
    void methodna(NPARAMSa)
    {
        DEBUG;
        // Because our const char* param cp might be NULL, and because we
        // intend to capture the value in a std::string, have to distinguish
        // between the NULL value and any non-NULL value. Use a convention
        // easy for a human reader: enclose any non-NULL value in single
        // quotes, reserving the unquoted string "NULL" to represent a NULL ptr.
        std::string vcp;
        if (cp == NULL)
            vcp = "NULL";
        else
            vcp = std::string("'") + cp + "'";

        this->debug()("methodna(", b,
              ", ", i,
              ", ", f,
              ", ", d,
              ", ", vcp,
              ")");

        this->b = b;
        this->i = i;
        this->f = f;
        this->d = d;
        this->cp = vcp;
    }

    void methodnb(NPARAMSb)
    {
        std::ostringstream vbin;
        for (U8 byte: bin)
        {
            vbin << std::hex << std::setfill('0') << std::setw(2) << unsigned(byte);
        }

        debug()("methodnb(", "'", s, "'",
              ", ", uuid,
              ", ", date,
              ", '", uri, "'",
              ", ", vbin.str(),
              ")");

        this->s = s;
        this->uuid = uuid;
        this->date = date;
        this->uri = uri;
        this->bin = bin;
    }

    void cmethodna(NPARAMSa) const
    {
        DEBUG;
        this->debug()('c', NONL);
        const_cast<Vars*>(this)->methodna(NARGSa);
    }

    void cmethodnb(NPARAMSb) const
    {
        debug()('c', NONL);
        const_cast<Vars*>(this)->methodnb(NARGSb);
    }

    static void smethodna(NPARAMSa);
    static void smethodnb(NPARAMSb);

    static Debug& debug()
    {
        // Lazily initialize this Debug instance so it can notice if main()
        // has forcibly set LOGTEST. If it were simply a static member, it
        // would already have examined the environment variable by the time
        // main() gets around to checking command-line switches. Since we have
        // a global static Vars instance, the same would be true of a plain
        // non-static member.
        static Debug sDebug("Vars");
        return sDebug;
    }
};
/*------- Global Vars instance for free functions and static methods -------*/
static Vars g;

/*------------ Static Vars method implementations reference 'g' ------------*/
void Vars::smethod0()
{
    debug()("smethod0() -> ", NONL);
    g.method0();
}

void Vars::smethod1(const LLSD& obj)
{
    debug()("smethod1(", obj, ") -> ", NONL);
    g.method1(obj);
}

void Vars::smethodna(NPARAMSa)
{
    debug()("smethodna(...) -> ", NONL);
    g.methodna(NARGSa);
}

void Vars::smethodnb(NPARAMSb)
{
    debug()("smethodnb(...) -> ", NONL);
    g.methodnb(NARGSb);
}

/*--------------------------- Reset global Vars ----------------------------*/
void clear()
{
    g = Vars();
}

/*------------------- Free functions also reference 'g' --------------------*/
void free0()
{
    g.debug()("free0() -> ", NONL);
    g.method0();
}

void free1(const LLSD& obj)
{
    g.debug()("free1(", obj, ") -> ", NONL);
    g.method1(obj);
}

void freena(NPARAMSa)
{
    g.debug()("freena(...) -> ", NONL);
    g.methodna(NARGSa);
}

void freenb(NPARAMSb)
{
    g.debug()("freenb(...) -> ", NONL);
    g.methodnb(NARGSb);
}

} // anonymous namespace

/*****************************************************************************
*   Tests
*****************************************************************************/
namespace
{
    static void require_contains(const std::string& outer, const std::string& inner)
    {
        CAPTURE(outer, inner);
        REQUIRE(outer.find(inner) != std::string::npos);
    }

    template <typename CALLABLE>
    static std::string call_exc(CALLABLE&& func, const std::string& exc_frag)
    {
        std::string what =
            catch_what<LLEventDispatcher::DispatchError>(std::forward<CALLABLE>(func));
        require_contains(what, exc_frag);
        return what;
    }

    template <typename CALLABLE>
    static void call_logerr(CALLABLE&& func, const std::string& frag)
    {
        CaptureLog capture;
        // the error should be logged; we just need to stop the exception
        // propagating
        catch_what<LLEventDispatcher::DispatchError>(std::forward<CALLABLE>(func));
        capture.messageWith(frag);
    }

    struct lleventdispatcher_data
    {
        Debug debug{"test"};
        WrapLLErrs redirect;
        Dispatcher work;
        Vars v;
        std::string name, desc;
        // Capture our own copy of all registered functions' descriptions
        typedef std::map<std::string, std::string> DescMap;
        DescMap descs;
        // Capture the Vars instance on which we expect each function to operate
        typedef std::map<std::string, Vars*> VarsMap;
        VarsMap funcvars;
        // Required structure for Callables with requirements
        LLSD required;
        // Parameter names for freena(), freenb()
        LLSD params;
        // Full, partial defaults arrays for params for freena(), freenb()
        LLSD dft_array_full, dft_array_partial;
        // Start index of partial defaults arrays
        const size_t partial_offset;
        // Full, partial defaults maps for params for freena(), freenb()
        LLSD dft_map_full, dft_map_partial;
        // Most of the above are indexed by "a" or "b". Useful to have an
        // array containing those strings for iterating.
        std::vector<LLSD::String> ab;

        lleventdispatcher_data():
            work("test dispatcher", "op"),
            // map {d=double, array=[3 elements]}
            required(LLSDMap("d", LLSD::Real(0))("array", llsd::array(LLSD(), LLSD(), LLSD()))),
            // first several params are required, last couple optional
            partial_offset(3)
        {
            // This object is reconstructed for every test case. But clear
            // global variables every time too.
            ::clear();

            const char* abs[] = { "a", "b" };
            ab.assign(boost::begin(abs), boost::end(abs));

            // Registration cases:
            // - (Callable | subclass const method | subclass non-const method |
            //   non-subclass method) (with | without) required
            // - (Free function | static method | non-static method), (no | arbitrary) params,
            //   array style
            // - (Free function | static method | non-static method), (no | arbitrary) params,
            //   map style, (empty | partial | full) (array | map) defaults
            // - Map-style errors:
            //   - (scalar | map) param names
            //   - defaults scalar
            //   - defaults array longer than params array
            //   - defaults map with plural unknown param names

            // I hate to have to write things twice, because of having to keep
            // them consistent. If we had variadic functions, addf() would be
            // a variadic method, capturing the name and desc and passing them
            // plus "everything else" to work.add(). If I could return a pair
            // and use that pair as the first two args to work.add(), I'd do
            // that. But the best I can do with present C++ is to set two
            // instance variables as a side effect of addf(), and pass those
            // variables to each work.add() call. :-P

            /*------------------------- Callables --------------------------*/

            // Arbitrary Callable with/out required params
            addf("free1", "free1", &g);
            work.add(name, desc, free1);
            addf("free1_req", "free1", &g);
            work.add(name, desc, free1, required);
            // Subclass non-const method with/out required params
            addf("Dmethod1", "method1", NULL);
            work.add(name, desc, &Dispatcher::method1);
            addf("Dmethod1_req", "method1", NULL);
            work.add(name, desc, &Dispatcher::method1, required);
            // Subclass const method with/out required params
            addf("Dcmethod1", "cmethod1", NULL);
            work.add(name, desc, &Dispatcher::cmethod1);
            addf("Dcmethod1_req", "cmethod1", NULL);
            work.add(name, desc, &Dispatcher::cmethod1, required);
            // Non-subclass method with/out required params
            addf("method1", "method1", &v);
            work.add(name, desc, [this](const LLSD& args){ return v.method1(args); });
            addf("method1_req", "method1", &v);
            work.add(name, desc, [this](const LLSD& args){ return v.method1(args); }, required);

            /*--------------- Arbitrary params, array style ----------------*/

            // (Free function | static method) with (no | arbitrary) params, array style
            addf("free0_array", "free0", &g);
            work.add(name, desc, free0);
            addf("freena_array", "freena", &g);
            work.add(name, desc, freena);
            addf("freenb_array", "freenb", &g);
            work.add(name, desc, freenb);
            addf("smethod0_array", "smethod0", &g);
            work.add(name, desc, &Vars::smethod0);
            addf("smethodna_array", "smethodna", &g);
            work.add(name, desc, &Vars::smethodna);
            addf("smethodnb_array", "smethodnb", &g);
            work.add(name, desc, &Vars::smethodnb);
            // Non-static method with (no | arbitrary) params, array style
            addf("method0_array", "method0", &v);
            work.add(name, desc, &Vars::method0, boost::lambda::var(v));
            addf("methodna_array", "methodna", &v);
            work.add(name, desc, &Vars::methodna, boost::lambda::var(v));
            addf("methodnb_array", "methodnb", &v);
            work.add(name, desc, &Vars::methodnb, boost::lambda::var(v));

            /*---------------- Arbitrary params, map style -----------------*/

            // We lay out each params list as an array, also each array of
            // default values we'll register. We'll zip these into
            // (param=value) maps. Why not define them as maps and just
            // extract the keys and values to arrays? Because that wouldn't
            // give us the right params-list order.

            // freena(), methodna(), cmethodna(), smethodna() all take same param list.
            // Same for freenb() et al.
            params = LLSDMap("a", llsd::array("b", "i", "f", "d", "cp"))
                            ("b", llsd::array("s", "uuid", "date", "uri", "bin"));
            debug("params:\n",
                  params, "\n"
                  "params[\"a\"]:\n",
                  params["a"], "\n"
                  "params[\"b\"]:\n",
                  params["b"]);
            // default LLSD::Binary value
            std::vector<U8> binary;
            for (size_t ix = 0, h = 0xaa; ix < 6; ++ix, h += 0x11)
            {
                binary.push_back((U8)h);
            }
            // Full defaults arrays. We actually don't care what the LLUUID or
            // LLDate values are, as long as they're different from the
            // LLUUID() and LLDate() default values so inspect() will report
            // them.
            dft_array_full = LLSDMap("a", llsd::array(true, 17, 3.14, 123456.78, "classic"))
                                    ("b", llsd::array("string",
                                                      LLUUID::generateNewID(),
                                                      LLDate::now(),
                                                      LLURI("http://www.ietf.org/rfc/rfc3986.txt"),
                                                      binary));
            debug("dft_array_full:\n",
                  dft_array_full);
            // Partial defaults arrays.
            for (LLSD::String a: ab)
            {
                LLSD::Integer partition(static_cast<LLSD::Integer>(std::min(partial_offset, dft_array_full[a].size())));
                dft_array_partial[a] =
                    llsd_copy_array(dft_array_full[a].beginArray() + partition,
                                    dft_array_full[a].endArray());
            }
            debug("dft_array_partial:\n",
                  dft_array_partial);

            for(LLSD::String a: ab)
            {
                // Generate full defaults maps by zipping (params, dft_array_full).
                dft_map_full[a] = zipmap(params[a], dft_array_full[a]);

                // Generate partial defaults map by zipping alternate entries from
                // (params, dft_array_full). Part of the point of using map-style
                // defaults is to allow any subset of the target function's
                // parameters to be optional, not just the rightmost.
                for (LLSD::Integer ix = 0, ixend = static_cast<LLSD::Integer>(params[a].size()); ix < ixend; ix += 2)
                {
                    dft_map_partial[a][params[a][ix].asString()] = dft_array_full[a][ix];
                }
            }
            debug("dft_map_full:\n",
                  dft_map_full, "\n"
                  "dft_map_partial:\n",
                  dft_map_partial);

            // (Free function | static method) with (no | arbitrary) params,
            // map style, no (empty array) defaults
            addf("free0_map", "free0", &g);
            work.add(name, desc, free0, LLSD::emptyArray());
            addf("smethod0_map", "smethod0", &g);
            work.add(name, desc, &Vars::smethod0, LLSD::emptyArray());
            addf("freena_map_allreq", "freena", &g);
            work.add(name, desc, freena, params["a"]);
            addf("freenb_map_allreq", "freenb", &g);
            work.add(name, desc, freenb, params["b"]);
            addf("smethodna_map_allreq", "smethodna", &g);
            work.add(name, desc, &Vars::smethodna, params["a"]);
            addf("smethodnb_map_allreq", "smethodnb", &g);
            work.add(name, desc, &Vars::smethodnb, params["b"]);
            // Non-static method with (no | arbitrary) params, map style, no
            // (empty array) defaults
            addf("method0_map", "method0", &v);
            work.add(name, desc, &Vars::method0, var(v), LLSD::emptyArray());
            addf("methodna_map_allreq", "methodna", &v);
            work.add(name, desc, &Vars::methodna, var(v), params["a"]);
            addf("methodnb_map_allreq", "methodnb", &v);
            work.add(name, desc, &Vars::methodnb, var(v), params["b"]);

            // Except for the "more (array | map) defaults than params" error
            // cases, tested separately below, the (partial | full)(array |
            // map) defaults cases don't apply to no-params functions/methods.
            // So eliminate free0, smethod0, method0 from the cases below.

            // (Free function | static method) with arbitrary params, map
            // style, partial (array | map) defaults
            addf("freena_map_leftreq", "freena", &g);
            work.add(name, desc, freena, params["a"], dft_array_partial["a"]);
            addf("freenb_map_leftreq", "freenb", &g);
            work.add(name, desc, freenb, params["b"], dft_array_partial["b"]);
            addf("smethodna_map_leftreq", "smethodna", &g);
            work.add(name, desc, &Vars::smethodna, params["a"], dft_array_partial["a"]);
            addf("smethodnb_map_leftreq", "smethodnb", &g);
            work.add(name, desc, &Vars::smethodnb, params["b"], dft_array_partial["b"]);
            addf("freena_map_skipreq", "freena", &g);
            work.add(name, desc, freena, params["a"], dft_map_partial["a"]);
            addf("freenb_map_skipreq", "freenb", &g);
            work.add(name, desc, freenb, params["b"], dft_map_partial["b"]);
            addf("smethodna_map_skipreq", "smethodna", &g);
            work.add(name, desc, &Vars::smethodna, params["a"], dft_map_partial["a"]);
            addf("smethodnb_map_skipreq", "smethodnb", &g);
            work.add(name, desc, &Vars::smethodnb, params["b"], dft_map_partial["b"]);
            // Non-static method with arbitrary params, map style, partial
            // (array | map) defaults
            addf("methodna_map_leftreq", "methodna", &v);
            work.add(name, desc, &Vars::methodna, var(v), params["a"], dft_array_partial["a"]);
            addf("methodnb_map_leftreq", "methodnb", &v);
            work.add(name, desc, &Vars::methodnb, var(v), params["b"], dft_array_partial["b"]);
            addf("methodna_map_skipreq", "methodna", &v);
            work.add(name, desc, &Vars::methodna, var(v), params["a"], dft_map_partial["a"]);
            addf("methodnb_map_skipreq", "methodnb", &v);
            work.add(name, desc, &Vars::methodnb, var(v), params["b"], dft_map_partial["b"]);

            // (Free function | static method) with arbitrary params, map
            // style, full (array | map) defaults
            addf("freena_map_adft", "freena", &g);
            work.add(name, desc, freena, params["a"], dft_array_full["a"]);
            addf("freenb_map_adft", "freenb", &g);
            work.add(name, desc, freenb, params["b"], dft_array_full["b"]);
            addf("smethodna_map_adft", "smethodna", &g);
            work.add(name, desc, &Vars::smethodna, params["a"], dft_array_full["a"]);
            addf("smethodnb_map_adft", "smethodnb", &g);
            work.add(name, desc, &Vars::smethodnb, params["b"], dft_array_full["b"]);
            addf("freena_map_mdft", "freena", &g);
            work.add(name, desc, freena, params["a"], dft_map_full["a"]);
            addf("freenb_map_mdft", "freenb", &g);
            work.add(name, desc, freenb, params["b"], dft_map_full["b"]);
            addf("smethodna_map_mdft", "smethodna", &g);
            work.add(name, desc, &Vars::smethodna, params["a"], dft_map_full["a"]);
            addf("smethodnb_map_mdft", "smethodnb", &g);
            work.add(name, desc, &Vars::smethodnb, params["b"], dft_map_full["b"]);
            // Non-static method with arbitrary params, map style, full
            // (array | map) defaults
            addf("methodna_map_adft", "methodna", &v);
            work.add(name, desc, &Vars::methodna, var(v), params["a"], dft_array_full["a"]);
            addf("methodnb_map_adft", "methodnb", &v);
            work.add(name, desc, &Vars::methodnb, var(v), params["b"], dft_array_full["b"]);
            addf("methodna_map_mdft", "methodna", &v);
            work.add(name, desc, &Vars::methodna, var(v), params["a"], dft_map_full["a"]);
            addf("methodnb_map_mdft", "methodnb", &v);
            work.add(name, desc, &Vars::methodnb, var(v), params["b"], dft_map_full["b"]);

            // All the above are expected to succeed, and are setup for the
            // tests to follow. Registration error cases are exercised as
            // tests rather than as test setup.
        }

        void addf(const std::string& n, const std::string& d, Vars* v)
        {
            debug("addf('", n, "', '", d, "')");
            // This method is to capture in our own DescMap the name and
            // description of every registered function, for metadata query
            // testing.
            descs[n] = d;
            // Also capture the Vars instance on which each function should operate.
            funcvars[n] = v;
            // See constructor for rationale for setting these instance vars.
            this->name = n;
            this->desc = d;
        }

        void verify_descs()
        {
            // Copy descs to a temp map of same type.
            DescMap forgotten(descs.begin(), descs.end());
            for (LLEventDispatcher::NameDesc nd: work)
            {
                CAPTURE(nd.first);
                DescMap::iterator found = forgotten.find(nd.first);
                UNSCOPED_INFO("LLEventDispatcher records a function we didn't enter");
                REQUIRE(found != forgotten.end());
                REQUIRE(nd.second == found->second);
                // found in our map the name from LLEventDispatcher, good, erase
                // our map entry
                forgotten.erase(found);
            }
            if (! forgotten.empty())
            {
                std::ostringstream out;
                out << "LLEventDispatcher failed to report";
                const char* delim = ": ";
                for (const DescMap::value_type& fme: forgotten)
                {
                    out << delim << fme.first;
                    delim = ", ";
                }
                FAIL(out.str());
            }
        }

        Vars* varsfor(const std::string& name)
        {
            CAPTURE(name);
            VarsMap::const_iterator found = funcvars.find(name);
            REQUIRE(found != funcvars.end());
            REQUIRE(found->second);
            return found->second;
        }

        std::string call_exc(const std::string& func, const LLSD& args, const std::string& exc_frag)
        {
            return ::call_exc(
                [this, func, args]()
                {
                    if (func.empty())
                    {
                        work(args);
                    }
                    else
                    {
                        work(func, args);
                    }
                },
                exc_frag);
        }

        void call_logerr(const std::string& func, const LLSD& args, const std::string& frag)
        {
            ::call_logerr([this, func, args](){ work(func, args); }, frag);
        }

        LLSD getMetadata(const std::string& name)
        {
            CAPTURE(name);
            LLSD meta(work.getMetadata(name));
            REQUIRE(meta.isDefined());
            return meta;
        }

        // From two related LLSD arrays, e.g. a param-names array and a values
        // array, zip them together into an LLSD map.
        LLSD zipmap(const LLSD& keys, const LLSD& values)
        {
            LLSD map;
            for (LLSD::Integer i = 0, iend = static_cast<LLSD::Integer>(keys.size()); i < iend; ++i)
            {
                // Have to select asString() since you can index an LLSD
                // object with either String or Integer.
                map[keys[i].asString()] = values[i];
            }
            return map;
        }

        void require_llsd_equals(const LLSD& actual, const LLSD& expected, U32 bits)
        {
            CAPTURE(expected, actual);
            REQUIRE(llsd_equals(actual, expected, bits));
        }
    };

    // Call cases:
    // - (try_call | call) (explicit name | event key) (real | bogus) name
    // - Callable with args that (do | do not) match required
    // - (Free function | non-static method), no args, (array | map) style
    // - (Free function | non-static method), arbitrary args,
    //   (array style with (scalar | map) | map style with scalar)
    // - (Free function | non-static method), arbitrary args, array style with
    //   array (too short | too long | just right)
    //   [trap LL_WARNS for too-long case?]
    // - (Free function | non-static method), arbitrary args, map style with
    //   (array | map) (all | too many | holes (with | without) defaults)
    // - const char* param gets ("" | NULL)

    // Query cases:
    // - Iterate over all (with | without) remove()
    // - getDispatchKey()
    // - Callable style (with | without) required
    // - (Free function | non-static method), array style, (no | arbitrary) params
    // - (Free function | non-static method), map style, (no | arbitrary) params,
    //   (empty | full | partial (array | map)) defaults

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: map-style registration with non-array params", "[lleventdispatcher]")
    {
        // Pass "param names" as scalar or as map
        LLSD attempts(llsd::array(17, LLSDMap("pi", 3.14)("two", 2)));
        for (LLSD ae: inArray(attempts))
        {
            std::string threw = catch_what<std::exception>([this, &ae](){
                    work.add("freena_err", "freena", freena, ae);
                });
            require_contains(threw, "must be an array");
        }
    }

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: map-style registration with badly-formed defaults", "[lleventdispatcher]")
    {
        std::string threw = catch_what<std::exception>([this](){
                work.add("freena_err", "freena", freena, llsd::array("a", "b"), 17);
            });
        require_contains(threw, "must be a map or an array");
    }

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: map-style registration with too many array defaults", "[lleventdispatcher]")
    {
        std::string threw = catch_what<std::exception>([this](){
                work.add("freena_err", "freena", freena,
                         llsd::array("a", "b"),
                         llsd::array(17, 0.9, "gack"));
            });
        require_contains(threw, "shorter than");
    }

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: map-style registration with too many map defaults", "[lleventdispatcher]")
    {
        std::string threw = catch_what<std::exception>([this](){
                work.add("freena_err", "freena", freena,
                         llsd::array("a", "b"),
                         LLSDMap("b", 17)("foo", 3.14)("bar", "sinister"));
            });
        require_contains(threw, "nonexistent params");
        require_contains(threw, "foo");
        require_contains(threw, "bar");
    }

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: query all lists every registered name and desc", "[lleventdispatcher]")
    {
        verify_descs();
    }

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: query all after remove() omits only the removed name", "[lleventdispatcher]")
    {
        CHECK_FALSE(work.remove("bogus"));
        CHECK(work.remove("free1"));
        // Of course, remove that from 'descs' too...
        descs.erase("free1");
        verify_descs();
    }

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: getDispatchKey() returns the key it was built with", "[lleventdispatcher]")
    {
        CHECK(work.getDispatchKey() == "op");
    }

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: query Callables with/out required params", "[lleventdispatcher]")
    {
        LLSD names(llsd::array("free1", "Dmethod1", "Dcmethod1", "method1"));
        for (LLSD nm: inArray(names))
        {
            LLSD metadata(getMetadata(nm));
            REQUIRE_THAT(metadata["name"], ll_test::LLSDEquals(nm));
            REQUIRE(metadata["desc"].asString() == descs[nm]);
            REQUIRE(metadata["required"].isUndefined());
            REQUIRE(metadata["optional"].isUndefined());

            std::string name_req(nm.asString() + "_req");
            metadata = getMetadata(name_req);
            REQUIRE(metadata["name"].asString() == name_req);
            REQUIRE(metadata["desc"].asString() == descs[name_req]);
            REQUIRE_THAT(metadata["required"], ll_test::LLSDEquals(required));
            REQUIRE(metadata["optional"].isUndefined());
        }
    }

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: query array-style functions/methods", "[lleventdispatcher]")
    {
        // Associate each registered name with expected arity.
        LLSD expected(llsd::array
                      (llsd::array
                       (0, llsd::array("free0_array", "smethod0_array", "method0_array")),
                       llsd::array
                       (5, llsd::array("freena_array", "smethodna_array", "methodna_array")),
                       llsd::array
                       (5, llsd::array("freenb_array", "smethodnb_array", "methodnb_array"))));
        for (LLSD ae: inArray(expected))
        {
            LLSD::Integer arity(ae[0].asInteger());
            LLSD names(ae[1]);
            LLSD req(LLSD::emptyArray());
            req.resize(arity);
            for (LLSD nm: inArray(names))
            {
                CAPTURE(nm);
                LLSD metadata(getMetadata(nm));
                REQUIRE_THAT(metadata["name"], ll_test::LLSDEquals(nm));
                REQUIRE(metadata["desc"].asString() == descs[nm]);
                REQUIRE_THAT(metadata["required"], ll_test::LLSDEquals(req));
                REQUIRE(metadata["optional"].isUndefined());
            }
        }
    }

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: query map-style no-params functions/methods", "[lleventdispatcher]")
    {
        // - (Free function | non-static method), map style, no params (ergo
        //   no defaults)
        LLSD names(llsd::array("free0_map", "smethod0_map", "method0_map"));
        for (LLSD nm: inArray(names))
        {
            LLSD metadata(getMetadata(nm));
            REQUIRE_THAT(metadata["name"], ll_test::LLSDEquals(nm));
            REQUIRE(metadata["desc"].asString() == descs[nm]);
            REQUIRE((metadata["required"].isUndefined() || metadata["required"].size() == 0));
            REQUIRE(metadata["optional"].isUndefined());
        }
    }

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: full array defaults and full map defaults give the same metadata", "[lleventdispatcher]")
    {
        // With functions registered with no defaults ("_allreq" suffixes),
        // there is of course no difference between array defaults and map
        // defaults. (We don't even bother registering with LLSD::emptyArray()
        // vs. LLSD::emptyMap().) With functions registered with all defaults,
        // there should (!) be no difference beween array defaults and map
        // defaults. Verify, so we can ignore the distinction for all other
        // tests.
        LLSD equivalences(llsd::array
                          (llsd::array("freena_map_adft", "freena_map_mdft"),
                           llsd::array("freenb_map_adft", "freenb_map_mdft"),
                           llsd::array("smethodna_map_adft", "smethodna_map_mdft"),
                           llsd::array("smethodnb_map_adft", "smethodnb_map_mdft"),
                           llsd::array("methodna_map_adft", "methodna_map_mdft"),
                           llsd::array("methodnb_map_adft", "methodnb_map_mdft")));
        for (LLSD eq: inArray(equivalences))
        {
            LLSD adft(eq[0]);
            LLSD mdft(eq[1]);
            CAPTURE(adft, mdft);
            // We can't just compare the results of the two getMetadata()
            // calls, because they contain ["name"], which are different. So
            // capture them, verify that each ["name"] is as expected, then
            // remove for comparing the rest.
            LLSD ameta(getMetadata(adft));
            LLSD mmeta(getMetadata(mdft));
            REQUIRE_THAT(ameta["name"], ll_test::LLSDEquals(adft));
            REQUIRE_THAT(mmeta["name"], ll_test::LLSDEquals(mdft));
            ameta.erase("name");
            mmeta.erase("name");
            REQUIRE_THAT(ameta, ll_test::LLSDEquals(mmeta));
        }
    }

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: query map-style arbitrary-params functions/methods", "[lleventdispatcher]")
    {
        // - (Free function | non-static method), map style, arbitrary params,
        //   (empty | full | partial (array | map)) defaults

        // Generate maps containing all parameter names for cases in which all
        // params are required. Also maps containing left requirements for
        // partial defaults arrays. Also defaults maps from defaults arrays.
        LLSD allreq, leftreq, rightdft;
        for (LLSD::String a: ab)
        {
            // The map in which all params are required uses params[a] as
            // keys, with all isUndefined() as values. We can accomplish that
            // by passing zipmap() an empty values array.
            allreq[a] = zipmap(params[a], LLSD::emptyArray());
            // Same for leftreq, save that we use the subset of the params not
            // supplied by dft_array_partial[a].
            LLSD::Integer partition(static_cast<LLSD::Integer>(params[a].size() - dft_array_partial[a].size()));
            leftreq[a] = zipmap(llsd_copy_array(params[a].beginArray(),
                                                params[a].beginArray() + partition),
                                LLSD::emptyArray());
            // Generate map pairing dft_array_partial[a] values with their
            // param names.
            rightdft[a] = zipmap(llsd_copy_array(params[a].beginArray() + partition,
                                                 params[a].endArray()),
                                 dft_array_partial[a]);
        }
        debug("allreq:\n",
              allreq, "\n"
              "leftreq:\n",
              leftreq, "\n"
              "rightdft:\n",
              rightdft);

        // Generate maps containing parameter names not provided by the
        // dft_map_partial maps.
        LLSD skipreq(allreq);
        for (LLSD::String a: ab)
        {
            for (const MapEntry& me: inMap(dft_map_partial[a]))
            {
                skipreq[a].erase(me.first);
            }
        }
        debug("skipreq:\n",
              skipreq);

        LLSD groups(llsd::array       // array of groups

                    (llsd::array      // group
                     (llsd::array("freena_map_allreq", "smethodna_map_allreq", "methodna_map_allreq"),
                      llsd::array(allreq["a"], LLSD())),  // required, optional

                     llsd::array        // group
                     (llsd::array("freenb_map_allreq", "smethodnb_map_allreq", "methodnb_map_allreq"),
                      llsd::array(allreq["b"], LLSD())),  // required, optional

                     llsd::array        // group
                     (llsd::array("freena_map_leftreq", "smethodna_map_leftreq", "methodna_map_leftreq"),
                      llsd::array(leftreq["a"], rightdft["a"])),  // required, optional

                     llsd::array        // group
                     (llsd::array("freenb_map_leftreq", "smethodnb_map_leftreq", "methodnb_map_leftreq"),
                      llsd::array(leftreq["b"], rightdft["b"])),  // required, optional

                     llsd::array        // group
                     (llsd::array("freena_map_skipreq", "smethodna_map_skipreq", "methodna_map_skipreq"),
                      llsd::array(skipreq["a"], dft_map_partial["a"])),  // required, optional

                     llsd::array        // group
                     (llsd::array("freenb_map_skipreq", "smethodnb_map_skipreq", "methodnb_map_skipreq"),
                      llsd::array(skipreq["b"], dft_map_partial["b"])),  // required, optional

                     // We only need mention the full-map-defaults ("_mdft" suffix)
                     // registrations, having established their equivalence with the
                     // full-array-defaults ("_adft" suffix) registrations in another test.
                     llsd::array        // group
                     (llsd::array("freena_map_mdft", "smethodna_map_mdft", "methodna_map_mdft"),
                      llsd::array(LLSD::emptyMap(), dft_map_full["a"])),  // required, optional

                     llsd::array        // group
                     (llsd::array("freenb_map_mdft", "smethodnb_map_mdft", "methodnb_map_mdft"),
                      llsd::array(LLSD::emptyMap(), dft_map_full["b"])))); // required, optional

        for (LLSD grp: inArray(groups))
        {
            // Internal structure of each group in 'groups':
            LLSD names(grp[0]);
            LLSD required(grp[1][0]);
            LLSD optional(grp[1][1]);
            debug("For ", names, ",\n",
                  "required:\n",
                  required, "\n"
                  "optional:\n",
                  optional);

            // Loop through 'names'
            for (LLSD nm: inArray(names))
            {
                CAPTURE(nm);
                LLSD metadata(getMetadata(nm));
                REQUIRE_THAT(metadata["name"], ll_test::LLSDEquals(nm));
                REQUIRE(metadata["desc"].asString() == descs[nm]);
                REQUIRE_THAT(metadata["required"], ll_test::LLSDEquals(required));
                REQUIRE_THAT(metadata["optional"], ll_test::LLSDEquals(optional));
            }
        }
    }

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: try_call() returns whether it found the name", "[lleventdispatcher]")
    {
        CHECK_FALSE(work.try_call("freek", LLSD()));
        CHECK_FALSE(work.try_call(LLSDMap("op", "freek")));
        CHECK(work.try_call("free0_array", LLSD()));
        CHECK(work.try_call(LLSDMap("op", "free0_map")));
    }

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: call with bad name", "[lleventdispatcher]")
    {
        call_exc("freek", LLSD(), "not found");
        std::string threw = call_exc("", LLSDMap("op", "freek"), "bad");
        require_contains(threw, "op");
        require_contains(threw, "freek");
    }

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: call with event key", "[lleventdispatcher]")
    {
        // We don't need a separate test for operator()(string, LLSD) with
        // valid name, because all the rest of the tests exercise that case.
        // The one we don't exercise elsewhere is operator()(LLSD) with valid
        // name, so here it is.
        work(LLSDMap("op", "free0_map"));
        CHECK(g.i == 17);
    }

    // Cannot be defined inside function body... remind me again why we use C++...  :-P
    namespace
    {
        struct CallablesTriple
        {
            std::string name, name_req;
            LLSD& llsd;
        };
    } // anonymous namespace

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: call Callables", "[lleventdispatcher]")
    {
        CallablesTriple tests[] =
        {
            { "free1",     "free1_req",     g.llsd },
            { "Dmethod1",  "Dmethod1_req",  work.llsd },
            { "Dcmethod1", "Dcmethod1_req", work.llsd },
            { "method1",   "method1_req",   v.llsd }
        };
        // Arbitrary LLSD value that we should be able to pass to Callables
        // without 'required', but should not be able to pass to Callables
        // with 'required'.
        LLSD answer(42);
        // LLSD value matching 'required' according to llsd_matches() rules.
        LLSD matching(LLSDMap("d", 3.14)("array", llsd::array("answer", true, answer)));
        // Okay, walk through 'tests'.
        for (const CallablesTriple& tr: tests)
        {
            // Should be able to pass 'answer' to Callables registered
            // without 'required'.
            work(tr.name, answer);
            REQUIRE_THAT(tr.llsd, ll_test::LLSDEquals(answer));
            // Should NOT be able to pass 'answer' to Callables registered
            // with 'required'.
            call_logerr(tr.name_req, answer, "bad request");
            // But SHOULD be able to pass 'matching' to Callables registered
            // with 'required'.
            work(tr.name_req, matching);
            REQUIRE_THAT(tr.llsd, ll_test::LLSDEquals(matching));
        }
    }

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: passing wrong args to (map | array)-style registrations", "[lleventdispatcher]")
    {
        // Pass scalar/map to array-style functions, scalar/array to map-style
        // functions. It seems pointless to repeat this with every variation:
        // (free function | non-static method), (no | arbitrary) args. We
        // should only need to engage it for one map-style registration and
        // one array-style registration.
        // Now that LLEventDispatcher has been extended to treat an LLSD
        // scalar as a single-entry array, the error we expect in this case is
        // that apply() is trying to pass that non-empty array to a nullary
        // function.
        call_logerr("free0_array", 17, "LL::apply");
        // similarly, apply() doesn't accept an LLSD Map
        call_logerr("free0_array", LLSDMap("pi", 3.14), "unsupported");

        std::string map_exc("needs a map");
        call_logerr("free0_map", 17, map_exc);
        // Passing an array to a map-style function works now! No longer an
        // error case!
//      call_exc("free0_map", llsd::array("a", "b"), map_exc);
    }

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: call no-args functions", "[lleventdispatcher]")
    {
        LLSD names(llsd::array
                   ("free0_array", "free0_map",
                    "smethod0_array", "smethod0_map",
                    "method0_array", "method0_map"));
        for (LLSD name: inArray(names))
        {
            // Look up the Vars instance for this function.
            Vars* vars(varsfor(name));
            // Both the global and stack Vars instances are automatically
            // cleared at the start of each test case. But since we're
            // calling these things several different times in the same
            // test case, manually reset the Vars between each.
            *vars = Vars();
            REQUIRE(vars->i == 0);
            // call function with empty array (or LLSD(), should be equivalent)
            work(name, LLSD());
            REQUIRE(vars->i == 17);
        }
    }

    // Break out this data because we use it in a couple different tests.
    static LLSD array_funcs(llsd::array
                            (LLSDMap("a", "freena_array")   ("b", "freenb_array"),
                             LLSDMap("a", "smethodna_array")("b", "smethodnb_array"),
                             LLSDMap("a", "methodna_array") ("b", "methodnb_array")));

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: call array-style functions with wrong-length arrays", "[lleventdispatcher]")
    {
        // Could have different wrong-length arrays for *na and for *nb, but
        // since they both take 5 params...
        LLSD tooshort(llsd::array("this", "array", "too", "short"));
        LLSD toolong (llsd::array("this", "array", "is",  "one", "too", "long"));
        LLSD badargs (llsd::array(tooshort, toolong));
        for (const LLSD& toosomething: inArray(badargs))
        {
            for (const LLSD& funcsab: inArray(array_funcs))
            {
                for (const llsd::MapEntry& e: inMap(funcsab))
                {
                    // apply() complains about wrong number of array entries
                    call_logerr(e.second, toosomething, "LL::apply");
                }
            }
        }
    }

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: call array-style functions with right-size arrays", "[lleventdispatcher]")
    {
        std::vector<U8> binary;
        for (size_t h(0x01), i(0); i < 5; h+= 0x22, ++i)
        {
            binary.push_back((U8)h);
        }
        LLSD args(LLSDMap("a", llsd::array(true, 17, 3.14, 123.456, "char*"))
                         ("b", llsd::array("string",
                                           LLUUID("01234567-89ab-cdef-0123-456789abcdef"),
                                           LLDate("2011-02-03T15:07:00Z"),
                                           LLURI("http://secondlife.com"),
                                           binary)));
        LLSD expect;
        for (LLSD::String a: ab)
        {
            expect[a] = zipmap(params[a], args[a]);
        }
        // Adjust expect["a"]["cp"] for special Vars::cp treatment.
        expect["a"]["cp"] = stringize("'", expect["a"]["cp"].asString(), "'");
        debug("expect: ", expect);

        for (const LLSD& funcsab: inArray(array_funcs))
        {
            for (LLSD::String a: ab)
            {
                CAPTURE(funcsab[a], a);
                // Reset the Vars instance before each call
                Vars* vars(varsfor(funcsab[a]));
                *vars = Vars();
                work(funcsab[a], args[a]);
                require_llsd_equals(vars->inspect(), expect[a], 7); // 7 bits ~= 2 decimal digits
            }
        }
    }

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: passing LLSD() to a const char* param sends NULL", "[lleventdispatcher]")
    {
        UNSCOPED_INFO("Vars::cp init");
        CHECK(v.cp == "");
        work("methodna_map_mdft", LLSDMap("cp", LLSD()));
        UNSCOPED_INFO("passing LLSD()");
        CHECK(v.cp == "NULL");
        work("methodna_map_mdft", LLSDMap("cp", ""));
        UNSCOPED_INFO("passing \"\"");
        CHECK(v.cp == "''");
        work("methodna_map_mdft", LLSDMap("cp", "non-NULL"));
        UNSCOPED_INFO("passing \"non-NULL\"");
        CHECK(v.cp == "'non-NULL'");
    }

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: call map-style functions with (full | oversized) (arrays | maps)", "[lleventdispatcher]")
    {
        const char binary[] = "\x99\x88\x77\x66\x55";
        LLSD array_full(LLSDMap
                        ("a", llsd::array(false, 255, 98.6, 1024.5, "pointer"))
                        ("b", llsd::array("object", LLUUID::generateNewID(), LLDate::now(), LLURI("http://wiki.lindenlab.com/wiki"), LLSD::Binary(boost::begin(binary), boost::end(binary)))));
        LLSD array_overfull(array_full);
        for (LLSD::String a: ab)
        {
            array_overfull[a].append("bogus");
        }
        debug("array_full: ", array_full, "\n"
              "array_overfull: ", array_overfull);
        // We rather hope that LLDate::now() will generate a timestamp
        // distinct from the one it generated in the constructor, moments ago.
        UNSCOPED_INFO("Timestamps too close");
        CHECK(array_full["b"][2].asDate() != dft_array_full["b"][2].asDate());
        // We /insist/ that LLUUID::generateNewID() do so.
        UNSCOPED_INFO("UUID collision");
        CHECK(array_full["b"][1].asUUID() != dft_array_full["b"][1].asUUID());
        LLSD map_full, map_overfull;
        for (LLSD::String a: ab)
        {
            map_full[a] = zipmap(params[a], array_full[a]);
            map_overfull[a] = map_full[a];
            map_overfull[a]["extra"] = "ignore";
        }
        debug("map_full: ", map_full, "\n"
              "map_overfull: ", map_overfull);
        LLSD expect(map_full);
        // Twiddle the const char* param.
        expect["a"]["cp"] = std::string("'") + expect["a"]["cp"].asString() + "'";
        // Another adjustment. For each data type, we're trying to distinguish
        // three values: the Vars member's initial value (member wasn't
        // stored; control never reached the set function), the registered
        // default param value from dft_array_full, and the array_full value
        // in this test. But bool can only distinguish two values. In this
        // case, we want to differentiate the local array_full value from the
        // dft_array_full value, so we use 'false'. However, that means
        // Vars::inspect() doesn't differentiate it from the initial value,
        // so won't bother returning it. Predict that behavior to match the
        // LLSD values.
        expect["a"].erase("b");
        debug("expect: ", expect);
        // For this test, calling functions registered with different sets of
        // parameter defaults should make NO DIFFERENCE WHATSOEVER. Every call
        // should pass all params.
        LLSD names(LLSDMap
                   ("a", llsd::array
                    ("freena_map_allreq",  "smethodna_map_allreq",  "methodna_map_allreq",
                     "freena_map_leftreq", "smethodna_map_leftreq", "methodna_map_leftreq",
                     "freena_map_skipreq", "smethodna_map_skipreq", "methodna_map_skipreq",
                     "freena_map_adft",    "smethodna_map_adft",    "methodna_map_adft",
                     "freena_map_mdft",    "smethodna_map_mdft",    "methodna_map_mdft"))
                   ("b", llsd::array
                    ("freenb_map_allreq",  "smethodnb_map_allreq",  "methodnb_map_allreq",
                     "freenb_map_leftreq", "smethodnb_map_leftreq", "methodnb_map_leftreq",
                     "freenb_map_skipreq", "smethodnb_map_skipreq", "methodnb_map_skipreq",
                     "freenb_map_adft",    "smethodnb_map_adft",    "methodnb_map_adft",
                     "freenb_map_mdft",    "smethodnb_map_mdft",    "methodnb_map_mdft")));
        // Treat (full | overfull) (array | map) the same.
        LLSD argssets(llsd::array(array_full, array_overfull, map_full, map_overfull));
        for (const LLSD& args: inArray(argssets))
        {
            for (LLSD::String a: ab)
            {
                for (LLSD::String name: inArray(names[a]))
                {
                    CAPTURE(name, a);
                    // Reset the Vars instance
                    Vars* vars(varsfor(name));
                    *vars = Vars();
                    work(name, args[a]);
                    require_llsd_equals(vars->inspect(), expect[a], 7); // 7 bits, 2 decimal digits
                    // intercept LL_WARNS for the two overfull cases?
                }
            }
        }
    }

    namespace
    {
        struct DispatchResult: public LLDispatchListener
        {
            using DR = DispatchResult;

            DispatchResult(): LLDispatchListener("results", "op")
            {
                add("strfunc",   "return string",       &DR::strfunc);
                add("voidfunc",  "void function",       &DR::voidfunc);
                add("emptyfunc", "return empty LLSD",   &DR::emptyfunc);
                add("intfunc",   "return Integer LLSD", &DR::intfunc);
                add("llsdfunc",  "return passed LLSD",  &DR::llsdfunc);
                add("mapfunc",   "return map LLSD",     &DR::mapfunc);
                add("arrayfunc", "return array LLSD",   &DR::arrayfunc);
            }

            std::string strfunc(const std::string& str) const { return "got " + str; }
            void voidfunc()                  const {}
            LLSD emptyfunc()                 const { return {}; }
            int  intfunc(int i)              const { return -i; }
            LLSD llsdfunc(const LLSD& event) const
            {
                LLSD result{ event };
                result["with"] = "string";
                return result;
            }
            LLSD mapfunc(int i, const std::string& str) const
            {
                return llsd::map("i", intfunc(i), "str", strfunc(str));
            }
            LLSD arrayfunc(int i, const std::string& str) const
            {
                return llsd::array(intfunc(i), strfunc(str));
            }
        };
    } // anonymous namespace

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: string result", "[lleventdispatcher]")
    {
        DispatchResult service;
        LLSD result{ service("strfunc", "a string") };
        CHECK(result.asString() == "got a string");
    }

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: void result", "[lleventdispatcher]")
    {
        DispatchResult service;
        LLSD result{ service("voidfunc", LLSD()) };
        CHECK(result.isUndefined());
    }

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: Integer result", "[lleventdispatcher]")
    {
        DispatchResult service;
        LLSD result{ service("intfunc", -17) };
        CHECK(result.asInteger() == 17);
    }

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: LLSD echo", "[lleventdispatcher]")
    {
        DispatchResult service;
        LLSD result{ service("llsdfunc", llsd::map("op", "llsdfunc", "reqid", 17)) };
        CHECK_THAT(result,
                      ll_test::LLSDEquals(llsd::map("op", "llsdfunc", "reqid", 17, "with", "string")));
    }

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: map LLSD result", "[lleventdispatcher]")
    {
        DispatchResult service;
        LLSD result{ service("mapfunc", llsd::array(-12, "value")) };
        CHECK_THAT(result, ll_test::LLSDEquals(llsd::map("i", 12, "str", "got value")));
    }

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: array LLSD result", "[lleventdispatcher]")
    {
        DispatchResult service;
        LLSD result{ service("arrayfunc", llsd::array(-8, "word")) };
        CHECK_THAT(result, ll_test::LLSDEquals(llsd::array(8, "got word")));
    }

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: a listener error without a reply pump throws", "[lleventdispatcher]")
    {
        DispatchResult service;
        ::call_exc(
            [&service]()
            { service.post(llsd::map("op", "nosuchfunc", "reqid", 17)); },
            "nosuchfunc");
    }

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: listener error with reply", "[lleventdispatcher]")
    {
        DispatchResult service;
        LLCaptureListener<LLSD> result;
        service.post(llsd::map("op", "nosuchfunc", "reqid", 17, "reply", result.getName()));
        LLSD reply{ result.get() };
        REQUIRE(reply.isDefined());
        CHECK(reply["reqid"].asInteger() == 17);
        require_contains(reply["error"].asString(), "nosuchfunc");
    }

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: listener call to void function", "[lleventdispatcher]")
    {
        DispatchResult service;
        LLCaptureListener<LLSD> result;
        result.set("non-empty");
        for (const auto& func: StringVec{ "voidfunc", "emptyfunc" })
        {
            service.post(llsd::map(
                             "op", func,
                             "reqid", 17,
                             "reply", result.getName()));
            CAPTURE(func);
            REQUIRE(result.get().asString() == "non-empty");
        }
    }

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: listener call to string function", "[lleventdispatcher]")
    {
        DispatchResult service;
        LLCaptureListener<LLSD> result;
        service.post(llsd::map(
                         "op", "strfunc",
                         "args", llsd::array("a string"),
                         "reqid", 17,
                         "reply", result.getName()));
        LLSD reply{ result.get() };
        CHECK(reply["reqid"].asInteger() == 17);
        CHECK(reply["data"].asString() == "got a string");
    }

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: listener call to map function", "[lleventdispatcher]")
    {
        DispatchResult service;
        LLCaptureListener<LLSD> result;
        service.post(llsd::map(
                         "op", "mapfunc",
                         "args", llsd::array(-7, "value"),
                         "reqid", 17,
                         "reply", result.getName()));
        LLSD reply{ result.get() };
        CHECK(reply["reqid"].asInteger() == 17);
        CHECK(reply["i"].asInteger() == 7);
        CHECK(reply["str"] == "got value");
    }

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: batched map success", "[lleventdispatcher]")
    {
        DispatchResult service;
        LLCaptureListener<LLSD> result;
        service.post(llsd::map(
                         "op", llsd::map(
                             "strfunc", "some string",
                             "intfunc", 2,
                             "voidfunc", LLSD(),
                             "arrayfunc", llsd::array(-5, "other string")),
                         "reqid", 17,
                         "reply", result.getName()));
        LLSD reply{ result.get() };
        CHECK(reply["reqid"].asInteger() == 17);
        reply.erase("reqid");
        CHECK_THAT(reply,
            ll_test::LLSDEquals(llsd::map(
                "strfunc", "got some string",
                "intfunc", -2,
                "voidfunc", LLSD(),
                "arrayfunc", llsd::array(5, "got other string"))));
    }

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: batched map error", "[lleventdispatcher]")
    {
        DispatchResult service;
        LLCaptureListener<LLSD> result;
        service.post(llsd::map(
                         "op", llsd::map(
                             "badfunc", 34, // !
                             "strfunc", "some string",
                             "intfunc", 2,
                             "missing", LLSD(), // !
                             "voidfunc", LLSD(),
                             "arrayfunc", llsd::array(-5, "other string")),
                         "reqid", 17,
                         "reply", result.getName()));
        LLSD reply{ result.get() };
        CHECK(reply["reqid"].asInteger() == 17);
        reply.erase("reqid");
        auto error{ reply["error"].asString() };
        reply.erase("error");
        require_contains(error, "badfunc");
        require_contains(error, "missing");
        CHECK_THAT(reply,
            ll_test::LLSDEquals(llsd::map(
                "strfunc", "got some string",
                "intfunc", -2,
                "voidfunc", LLSD(),
                "arrayfunc", llsd::array(5, "got other string"))));
    }

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: batched map exception", "[lleventdispatcher]")
    {
        DispatchResult service;
        auto error = ::call_exc(
            [&service]()
            {
                service.post(llsd::map(
                                 "op", llsd::map(
                                     "badfunc", 34, // !
                                     "strfunc", "some string",
                                     "intfunc", 2,
                                     "missing", LLSD(), // !
                                     "voidfunc", LLSD(),
                                     "arrayfunc", llsd::array(-5, "other string")),
                                 "reqid", 17));
                // no "reply"
            },
            "badfunc");
        require_contains(error, "missing");
    }

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: batched array success", "[lleventdispatcher]")
    {
        DispatchResult service;
        LLCaptureListener<LLSD> result;
        service.post(llsd::map(
                         "op", llsd::array(
                             llsd::array("strfunc", "some string"),
                             llsd::array("intfunc", 2),
                             "arrayfunc",
                             "voidfunc"),
                         "args", llsd::array(
                             LLSD(),
                             LLSD(),
                             llsd::array(-5, "other string")),
                         // args array deliberately short, since the default
                         // [3] is undefined, which should work for voidfunc
                         "reqid", 17,
                         "reply", result.getName()));
        LLSD reply{ result.get() };
        CHECK(reply["reqid"].asInteger() == 17);
        reply.erase("reqid");
        CHECK_THAT(reply,
            ll_test::LLSDEquals(llsd::map(
                "data", llsd::array(
                    "got some string",
                    -2,
                    llsd::array(5, "got other string"),
                    LLSD()))));
    }

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: batched array error", "[lleventdispatcher]")
    {
        DispatchResult service;
        LLCaptureListener<LLSD> result;
        service.post(llsd::map(
                         "op", llsd::array(
                             llsd::array("strfunc", "some string"),
                             llsd::array("intfunc", 2, "whoops"), // bad form
                             "arrayfunc",
                             "voidfunc"),
                         "args", llsd::array(
                             LLSD(),
                             LLSD(),
                             llsd::array(-5, "other string")),
                         // args array deliberately short, since the default
                         // [3] is undefined, which should work for voidfunc
                         "reqid", 17,
                         "reply", result.getName()));
        LLSD reply{ result.get() };
        CHECK(reply["reqid"].asInteger() == 17);
        reply.erase("reqid");
        auto error{ reply["error"] };
        reply.erase("error");
        require_contains(error, "[1]");
        require_contains(error, "unsupported");
        CHECK_THAT(reply,
                      ll_test::LLSDEquals(llsd::map("data", llsd::array("got some string"))));
    }

    TEST_CASE_METHOD(lleventdispatcher_data, "lleventdispatcher: batched array exception", "[lleventdispatcher]")
    {
        DispatchResult service;
        auto error = ::call_exc(
            [&service]()
            {
                service.post(llsd::map(
                                 "op", llsd::array(
                                     llsd::array("strfunc", "some string"),
                                     llsd::array("intfunc", 2, "whoops"), // bad form
                                     "arrayfunc",
                                     "voidfunc"),
                                 "args", llsd::array(
                                     LLSD(),
                                     LLSD(),
                                     llsd::array(-5, "other string")),
                                 // args array deliberately short, since the default
                                 // [3] is undefined, which should work for voidfunc
                                 "reqid", 17));
                // no "reply"
            },
            "[1]");
        require_contains(error, "unsupported");
    }
} // anonymous namespace
