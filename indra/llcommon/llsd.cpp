/**
 * @file llsd.cpp
 * @brief LLSD flexible data system
 *
 * $LicenseInfo:firstyear=2005&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

// Must turn on conditional declarations in header file so definitions end up
// with proper linkage.
#define LLSD_DEBUG_INFO
#include "linden_common.h"
#include "llsd.h"

#include "llbase64.h"
#include "llerror.h"
#include "llsdserialize.h"
#include "stringize.h"

#include <charconv>
#include <limits>

#include <fast_float/fast_float.h>
#include <fmt/format.h>

// Defend against a caller forcibly passing a negative number into an unsigned
// size_t index param
inline
bool was_negative(size_t i)
{
    return (i > std::numeric_limits<int>::max());
}
#define NEGATIVE_EXIT(i) if (was_negative(i)) return
#define NEGATIVE_RETURN(i, result) NEGATIVE_EXIT(i) (result)

#ifndef LL_RELEASE_FOR_DOWNLOAD
#define NAME_UNNAMED_NAMESPACE
#endif

#ifdef NAME_UNNAMED_NAMESPACE
namespace LLSDUnnamedNamespace
#else
namespace
#endif
{
    class ImplMap;
    class ImplArray;
}

#ifdef NAME_UNNAMED_NAMESPACE
using namespace LLSDUnnamedNamespace;
#endif

namespace llsd
{

// statics
S32 sLLSDAllocationCount = 0;
S32 sLLSDNetObjects = 0;

} // namespace llsd

// These counters feed llsd::dumpStats() only, but cost two writes to shared
// globals on every LLSD construction/destruction (including moves and
// temporaries), so they are compiled out unless explicitly enabled here.
#define LLSD_TRACK_OBJECT_COUNTS 0

#if LLSD_TRACK_OBJECT_COUNTS
#define ALLOC_LLSD_OBJECT           { llsd::sLLSDNetObjects++;  llsd::sLLSDAllocationCount++;   }
#define FREE_LLSD_OBJECT            { llsd::sLLSDNetObjects--;                                  }
#else
#define ALLOC_LLSD_OBJECT
#define FREE_LLSD_OBJECT
#endif

class LLSD::Impl
    /**< This class is the abstract base class of the heap-backed value
         implementations (String, UUID, URI, Binary, Map, Array). It provides
         the reference counting implementation and default conversions.
         Boolean, Integer, Real and Date values are stored inline in LLSD
         itself and never reach this hierarchy.
    */
{
protected:
    Impl();

    virtual ~Impl();

    bool shared() const                         { return mUseCount > 1; }

    U32 mUseCount;

public:
    static void retain(Impl* impl);
        ///< safely increment the reference count

    static void destruct(Impl*& var);
        ///< safely decrement or destroy var

    static void reset(Impl*& var, Impl* impl);
        ///< safely set var to refer to the new impl (possibly shared)

    static ImplMap& mutableMap(LLSD& sd);
    static ImplArray& mutableArray(LLSD& sd);
        ///< ensure sd is a modifiable, unshared map or array

    template <class ImplT, class V>
    static void assignValue(LLSD& sd, LLSD::Type type, V&& value)
        ///< assign a heap-backed value, reusing an unshared impl in place
    {
        if (sd.mType == type && !sd.mData.impl->shared())
        {
            static_cast<ImplT*>(sd.mData.impl)->set(std::forward<V>(value));
        }
        else
        {
            sd.releaseValue();
            sd.mData.impl = nullptr;
            reset(sd.mData.impl, new ImplT(std::forward<V>(value)));
            sd.mType = type;
        }
    }

    virtual Boolean asBoolean() const           { return false; }
    virtual Integer asInteger() const           { return 0; }
    virtual Real    asReal() const              { return 0.0; }
    virtual String  asString() const            { return std::string(); }
    virtual UUID    asUUID() const              { return LLUUID(); }
    virtual Date    asDate() const              { return LLDate(); }
    virtual URI     asURI() const               { return LLURI(); }
    virtual const Binary&   asBinary() const    { static const std::vector<U8> empty; return empty; }

    virtual const String& asStringRef() const { static const std::string empty; return empty; }

    virtual void asXMLRPCValue(std::ostream& os) const { os << "<nil/>"; }

    static void calcStats(const LLSD& sd, S32 type_counts[], S32 share_counts[]);
    static void dumpStats(const LLSD& sd);

    static const LLSD& undef();

    static U32 sAllocationCount;
    static U32 sOutstandingCount;
};

#ifdef NAME_UNNAMED_NAMESPACE
namespace LLSDUnnamedNamespace
#else
namespace
#endif
{
    template<class Data, class DataRef = Data, class DataMove = Data>
    class ImplBase : public LLSD::Impl
        ///< This class handles the work for a subclass of Impl for a given
        //   heap-backed data type. Subclasses provide conversion functions.
    {
    protected:
        Data mValue;

        typedef ImplBase Base;

    public:
        ImplBase(DataRef value) : mValue(value) { }
        ImplBase(DataMove value) : mValue(std::move(value)) { }

        void set(DataRef value)  { mValue = value; }
        void set(DataMove value) { mValue = std::move(value); }
    };


    class ImplString final
        : public ImplBase<LLSD::String, const LLSD::String&, LLSD::String&&>
    {
    public:
        ImplString(const LLSD::String& v) : Base(v) { }
        ImplString(LLSD::String&& v) : Base(std::move(v)) {}

        virtual LLSD::Boolean   asBoolean() const   { return !mValue.empty(); }
        virtual LLSD::Integer   asInteger() const;
        virtual LLSD::Real      asReal() const;
        virtual LLSD::String    asString() const    { return mValue; }
        virtual LLSD::UUID      asUUID() const  { return LLUUID(mValue); }
        virtual LLSD::Date      asDate() const  { return LLDate(mValue); }
        virtual LLSD::URI       asURI() const   { return LLURI(mValue); }
        virtual const LLSD::String& asStringRef() const { return mValue; }

        virtual void asXMLRPCValue(std::ostream& os) const { os << "<string>" << LLStringFn::xml_encode(mValue) << "</string>"; }

        size_t size() const { return mValue.size(); }

        using Base::set;
        void set(const char* value) { mValue = value; }
    };

    LLSD::Integer ImplString::asInteger() const
    {
        // This must treat "1.23" not as an error, but as a number, which is
        // then truncated down to an integer.  Hence, this code doesn't call
        // std::istringstream::operator>>(int&), which would not consume the
        // ".23" portion.

        return (int)asReal();
    }

    LLSD::Real ImplString::asReal() const
    {
        return llsd::string_to_real(mValue);
    }


    class ImplUUID final
        : public ImplBase<LLSD::UUID, const LLSD::UUID&, LLSD::UUID&&>
    {
    public:
        ImplUUID(const LLSD::UUID& v) : Base(v) { }
        ImplUUID(LLSD::UUID&& v) : Base(std::move(v)) { }

        virtual LLSD::String    asString() const{ return mValue.asString(); }
        virtual LLSD::UUID      asUUID() const  { return mValue; }

        virtual void asXMLRPCValue(std::ostream& os) const { os << "<string>" << mValue << "</string>"; }
    };


    class ImplURI final
        : public ImplBase<LLSD::URI, const LLSD::URI&, LLSD::URI&&>
    {
    public:
        ImplURI(const LLSD::URI& v) : Base(v) { }
        ImplURI(LLSD::URI&& v) : Base(std::move(v)) { }

        virtual LLSD::String    asString() const{ return mValue.asString(); }
        virtual LLSD::URI       asURI() const   { return mValue; }

        virtual void asXMLRPCValue(std::ostream& os) const { os << "<string>" << LLStringFn::xml_encode(mValue.asString()) << "</string>"; }
    };


    class ImplBinary final
        : public ImplBase<LLSD::Binary, const LLSD::Binary&, LLSD::Binary&&>
    {
    public:
        ImplBinary(const LLSD::Binary& v) : Base(v) { }
        ImplBinary(LLSD::Binary&& v) : Base(std::move(v)) { }

        virtual const LLSD::Binary& asBinary() const{ return mValue; }

        virtual void asXMLRPCValue(std::ostream& os) const { os << "<base64>" << LLBase64::encode(mValue.data(), mValue.size()) << "</base64>"; }
    };


    class ImplMap final : public LLSD::Impl
    {
    private:
        using DataMap = LLSD::llsd_map_t;

        DataMap mData;

    protected:
        ImplMap(const DataMap& data) : mData(data) { }

    public:
        ImplMap() { }

        ImplMap* clone() const { return new ImplMap(mData); }

        virtual LLSD::Boolean asBoolean() const { return !mData.empty(); }

        virtual void asXMLRPCValue(std::ostream& os) const
        {
            os << "<struct>";
            for (const auto& it : mData)
            {
                os << "<member><name>" << LLStringFn::xml_encode(it.first) << "</name>";
                it.second.asXMLRPCValue(os);
                os << "</member>";
            }
            os << "</struct>";
        }

        bool has(std::string_view) const;
        LLSD get(std::string_view) const;
        LLSD getKeys() const;
        void insert(std::string&& k, const LLSD& v);
        void insert(std::string&& k, LLSD&& v);
        void insert(std::string_view k, const LLSD& v);
        void insert(std::string_view k, LLSD&& v);
        void erase(const LLSD::String&);
        LLSD& ref(std::string&&);
        LLSD& ref(std::string_view);
        const LLSD& ref(std::string_view) const;

        size_t size() const { return mData.size(); }

        LLSD::map_iterator beginMap() { return mData.begin(); }
        LLSD::map_iterator endMap() { return mData.end(); }
        LLSD::map_const_iterator beginMap() const { return mData.begin(); }
        LLSD::map_const_iterator endMap() const { return mData.end(); }
    };

    bool ImplMap::has(const std::string_view k) const
    {
        LL_PROFILE_ZONE_SCOPED_CATEGORY_LLSD;
        DataMap::const_iterator i = mData.find(k);
        return i != mData.end();
    }

    LLSD ImplMap::get(const std::string_view k) const
    {
        LL_PROFILE_ZONE_SCOPED_CATEGORY_LLSD;
        DataMap::const_iterator i = mData.find(k);
        return (i != mData.end()) ? i->second : LLSD();
    }

    LLSD ImplMap::getKeys() const
    {
        LL_PROFILE_ZONE_SCOPED_CATEGORY_LLSD;
        LLSD keys = LLSD::emptyReservedArray(mData.size());
        for (const auto& entry : mData)
        {
            keys.append(entry.first);
        }
        return keys;
    }

    // The inserts probe with lower_bound before constructing the node so a
    // duplicate key doesn't pay for a node + key string that map::emplace
    // would immediately discard. Like emplace, they do not overwrite.

    void ImplMap::insert(std::string&& k, const LLSD& v)
    {
        LL_PROFILE_ZONE_SCOPED_CATEGORY_LLSD;
        DataMap::iterator i = mData.lower_bound(k);
        if (i == mData.end() || mData.key_comp()(k, i->first))
        {
            mData.emplace_hint(i, std::piecewise_construct,
                               std::forward_as_tuple(std::move(k)),
                               std::forward_as_tuple(v));
        }
    }

    void ImplMap::insert(std::string&& k, LLSD&& v)
    {
        LL_PROFILE_ZONE_SCOPED_CATEGORY_LLSD;
        DataMap::iterator i = mData.lower_bound(k);
        if (i == mData.end() || mData.key_comp()(k, i->first))
        {
            mData.emplace_hint(i, std::piecewise_construct,
                               std::forward_as_tuple(std::move(k)),
                               std::forward_as_tuple(std::move(v)));
        }
    }

    void ImplMap::insert(std::string_view k, const LLSD& v)
    {
        LL_PROFILE_ZONE_SCOPED_CATEGORY_LLSD;
        DataMap::iterator i = mData.lower_bound(k);
        if (i == mData.end() || mData.key_comp()(k, i->first))
        {
            mData.emplace_hint(i, std::piecewise_construct,
                               std::forward_as_tuple(k),
                               std::forward_as_tuple(v));
        }
    }

    void ImplMap::insert(std::string_view k, LLSD&& v)
    {
        LL_PROFILE_ZONE_SCOPED_CATEGORY_LLSD;
        DataMap::iterator i = mData.lower_bound(k);
        if (i == mData.end() || mData.key_comp()(k, i->first))
        {
            mData.emplace_hint(i, std::piecewise_construct,
                               std::forward_as_tuple(k),
                               std::forward_as_tuple(std::move(v)));
        }
    }

    void ImplMap::erase(const LLSD::String& k)
    {
        LL_PROFILE_ZONE_SCOPED_CATEGORY_LLSD;
        mData.erase(k);
    }

    LLSD& ImplMap::ref(std::string&& k)
    {
        DataMap::iterator i = mData.lower_bound(k);
        if (i == mData.end() || mData.key_comp()(k, i->first))
        {
            return mData.emplace_hint(i, std::piecewise_construct,
                                      std::forward_as_tuple(std::move(k)),
                                      std::forward_as_tuple())->second;
        }

        return i->second;
    }

    LLSD& ImplMap::ref(std::string_view k)
    {
        DataMap::iterator i = mData.lower_bound(k);
        if (i == mData.end() || mData.key_comp()(k, i->first))
        {
            return mData.emplace_hint(i, std::piecewise_construct,
                                      std::forward_as_tuple(k),
                                      std::forward_as_tuple())->second;
        }

        return i->second;
    }

    const LLSD& ImplMap::ref(std::string_view k) const
    {
        DataMap::const_iterator i = mData.lower_bound(k);
        if (i == mData.end() || mData.key_comp()(k, i->first))
        {
            return undef();
        }

        return i->second;
    }


    class ImplArray final : public LLSD::Impl
    {
    private:
        typedef std::vector<LLSD> DataVector;

        DataVector mData;

    protected:
        ImplArray(const DataVector& data) : mData(data) { }

    public:
        ImplArray() = default;

        ImplArray* clone() const { return new ImplArray(mData); }

        virtual LLSD::Boolean asBoolean() const { return !mData.empty(); }

        virtual void asXMLRPCValue(std::ostream& os) const
        {
            os << "<array><data>";
            for (const auto& it : mData)
            {
                it.asXMLRPCValue(os);
            }
            os << "</data></array>";
        }

        size_t size() const { return mData.size(); }
        LLSD get(size_t) const;
        void set(size_t, const LLSD&);
        void set(size_t, LLSD&&);
        void insert(size_t, const LLSD&);
        void insert(size_t, LLSD&&);
        LLSD& append(const LLSD&);
        LLSD& append(LLSD&&);
        void erase(size_t);
        LLSD& ref(size_t);
        const LLSD& ref(size_t) const;
        void reserve(size_t size) { mData.reserve(size); }

        LLSD::array_iterator beginArray() { return mData.begin(); }
        LLSD::array_iterator endArray() { return mData.end(); }
        LLSD::reverse_array_iterator rbeginArray() { return mData.rbegin(); }
        LLSD::reverse_array_iterator rendArray() { return mData.rend(); }
        LLSD::array_const_iterator beginArray() const { return mData.begin(); }
        LLSD::array_const_iterator endArray() const { return mData.end(); }
    };

    LLSD ImplArray::get(size_t i) const
    {
        NEGATIVE_RETURN(i, LLSD());
        DataVector::size_type index = i;

        return (index < mData.size()) ? mData[index] : LLSD();
    }

    void ImplArray::set(size_t i, const LLSD& v)
    {
        NEGATIVE_EXIT(i);
        DataVector::size_type index = i;

        if (index >= mData.size())
        {
            mData.resize(index + 1);
        }

        mData[index] = v;
    }

    void ImplArray::set(size_t i, LLSD&& v)
    {
        NEGATIVE_EXIT(i);
        DataVector::size_type index = i;

        if (index >= mData.size())
        {
            mData.resize(index + 1);
        }

        mData[index] = std::move(v);
    }

    void ImplArray::insert(size_t i, const LLSD& v)
    {
        NEGATIVE_EXIT(i);
        DataVector::size_type index = i;

        if (index >= mData.size())  // tbd - sanity check limit for index ?
        {
            // beyond the end: pad with undefined so v lands at index, rather
            // than double-shifting and leaving a trailing undefined element
            mData.resize(index);
            mData.push_back(v);
        }
        else
        {
            mData.insert(mData.begin() + index, v);
        }
    }

    void ImplArray::insert(size_t i, LLSD&& v)
    {
        NEGATIVE_EXIT(i);
        DataVector::size_type index = i;

        if (index >= mData.size()) // tbd - sanity check limit for index ?
        {
            mData.resize(index);
            mData.push_back(std::move(v));
        }
        else
        {
            mData.insert(mData.begin() + index, std::move(v));
        }
    }

    LLSD& ImplArray::append(const LLSD& v)
    {
        mData.push_back(v);
        return mData.back();
    }

    LLSD& ImplArray::append(LLSD&& v)
    {
        mData.push_back(std::move(v));
        return mData.back();
    }

    void ImplArray::erase(size_t i)
    {
        NEGATIVE_EXIT(i);
        DataVector::size_type index = i;

        if (index < mData.size())
        {
            mData.erase(mData.begin() + index);
        }
    }

    LLSD& ImplArray::ref(size_t i)
    {
        DataVector::size_type index = was_negative(i)? 0 : i;

        if (index >= mData.size())
        {
            mData.resize(index + 1);
        }

        return mData[index];
    }

    const LLSD& ImplArray::ref(size_t i) const
    {
        NEGATIVE_RETURN(i, undef());
        DataVector::size_type index = i;

        if (index >= mData.size())
        {
            return undef();
        }

        return mData[index];
    }
}

LLSD::Impl::Impl()
    : mUseCount(0)
{
    ++sAllocationCount;
    ++sOutstandingCount;
}

LLSD::Impl::~Impl()
{
    --sOutstandingCount;
}

void LLSD::Impl::retain(Impl* impl)
{
    if (impl)
    {
        ++impl->mUseCount;
    }
}

void LLSD::Impl::destruct(Impl*& var)
{
    if (var && --var->mUseCount == 0)
    {
        delete var;
    }
}

void LLSD::Impl::reset(Impl*& var, Impl* impl)
{
    if (impl)
    {
        ++impl->mUseCount;
    }
    if (var && --var->mUseCount == 0)
    {
        delete var;
    }
    var = impl;
}

ImplMap& LLSD::Impl::mutableMap(LLSD& sd)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_LLSD;
    if (sd.mType == LLSD::TypeMap)
    {
        ImplMap* m = static_cast<ImplMap*>(sd.mData.impl);
        if (m->shared())
        {
            ImplMap* copy = m->clone();
            reset(sd.mData.impl, copy);
            return *copy;
        }
        return *m;
    }
    sd.releaseValue();
    sd.mData.impl = nullptr;
    ImplMap* im = new ImplMap;
    reset(sd.mData.impl, im);
    sd.mType = LLSD::TypeMap;
    return *im;
}

ImplArray& LLSD::Impl::mutableArray(LLSD& sd)
{
    if (sd.mType == LLSD::TypeArray)
    {
        ImplArray* a = static_cast<ImplArray*>(sd.mData.impl);
        if (a->shared())
        {
            ImplArray* copy = a->clone();
            reset(sd.mData.impl, copy);
            return *copy;
        }
        return *a;
    }
    sd.releaseValue();
    sd.mData.impl = nullptr;
    ImplArray* ia = new ImplArray;
    reset(sd.mData.impl, ia);
    sd.mType = LLSD::TypeArray;
    return *ia;
}

const LLSD& LLSD::Impl::undef()
{
    static const LLSD immutableUndefined;
    return immutableUndefined;
}

void LLSD::Impl::calcStats(const LLSD& sd, S32 type_counts[], S32 share_counts[])
{
    S32 tp = S32(sd.type());
    if (0 <= tp && tp < LLSD::TypeLLSDNumTypes)
    {
        type_counts[tp]++;
        if (sd.isHeap() && sd.mData.impl->shared())
        {
            share_counts[tp]++;
        }
    }

    if (sd.isMap())
    {
        for (LLSD::map_const_iterator it = sd.beginMap(), end = sd.endMap(); it != end; ++it)
        {
            calcStats(it->second, type_counts, share_counts);
        }
    }
    else if (sd.isArray())
    {
        for (LLSD::array_const_iterator it = sd.beginArray(), end = sd.endArray(); it != end; ++it)
        {
            calcStats(*it, type_counts, share_counts);
        }
    }
}

void LLSD::Impl::dumpStats(const LLSD& sd)
{
    if (sd.isMap())
    {
        std::cout << "Map size: " << sd.size() << std::endl;
    }

    std::cout << "LLSD Net Objects: " << llsd::sLLSDNetObjects << std::endl;
    std::cout << "LLSD allocations: " << llsd::sLLSDAllocationCount << std::endl;

    std::cout << "LLSD::Impl Net Objects: " << sOutstandingCount << std::endl;
    std::cout << "LLSD::Impl allocations: " << sAllocationCount << std::endl;

    S32 type_counts[LLSD::TypeLLSDNumTypes + 1];
    memset(&type_counts, 0, sizeof(type_counts));

    S32 share_counts[LLSD::TypeLLSDNumTypes + 1];
    memset(&share_counts, 0, sizeof(share_counts));

    calcStats(sd, type_counts, share_counts);

    S32 type_index = LLSD::TypeLLSDTypeBegin;
    while (type_index != LLSD::TypeLLSDTypeEnd)
    {
        std::cout << LLSD::typeString((LLSD::Type)type_index) << " type "
            << type_counts[type_index] << " objects, "
            << share_counts[type_index] << " shared"
            << std::endl;
        type_index++;
    }
}

U32 LLSD::Impl::sAllocationCount = 0;
U32 LLSD::Impl::sOutstandingCount = 0;


void LLSD::releaseValue()
{
    if (isHeap())
    {
        Impl::destruct(mData.impl);
    }
}

LLSD::LLSD() : mType(TypeUndefined)     { mData.impl = nullptr; ALLOC_LLSD_OBJECT; }
LLSD::~LLSD()                           { FREE_LLSD_OBJECT; releaseValue(); }

LLSD::LLSD(const LLSD& other) : mType(TypeUndefined)
{
    mData.impl = nullptr;
    ALLOC_LLSD_OBJECT;
    assign(other);
}

void LLSD::assign(const LLSD& other)
{
    // copy other's value before releasing ours so self-assignment is safe
    const Type t = other.mType;
    Data d = other.mData;
    if (isHeapType(t))
    {
        Impl::retain(d.impl);
    }
    releaseValue();
    mData = d;
    mType = t;
}

LLSD::LLSD(LLSD&& other) noexcept
    : mData(other.mData)
    , mType(other.mType)
{
    ALLOC_LLSD_OBJECT;
    other.mData.impl = nullptr;
    other.mType = TypeUndefined;
}

void LLSD::assign(LLSD&& other)
{
    if (this != &other)
    {
        releaseValue();
        mData = other.mData;
        mType = other.mType;
        other.mData.impl = nullptr;
        other.mType = TypeUndefined;
    }
}

LLSD& LLSD::operator=(LLSD&& other) noexcept
{
    assign(std::move(other));
    return *this;
}

void LLSD::clear()
{
    releaseValue();
    mData.impl = nullptr;
    mType = TypeUndefined;
}

// Scalar Constructors
LLSD::LLSD(Boolean v) : mType(TypeBoolean)  { mData.b = v; ALLOC_LLSD_OBJECT; }
LLSD::LLSD(Integer v) : mType(TypeInteger)  { mData.i = v; ALLOC_LLSD_OBJECT; }
LLSD::LLSD(Real v) : mType(TypeReal)        { mData.r = v; ALLOC_LLSD_OBJECT; }
LLSD::LLSD(const Date& v) : mType(TypeDate) { mData.r = v.secondsSinceEpoch(); ALLOC_LLSD_OBJECT; }
LLSD::LLSD(Date&& v) : mType(TypeDate)      { mData.r = v.secondsSinceEpoch(); ALLOC_LLSD_OBJECT; }
LLSD::LLSD(const String& v) : mType(TypeUndefined)  { mData.impl = nullptr; ALLOC_LLSD_OBJECT; assign(v); }
LLSD::LLSD(const UUID& v) : mType(TypeUndefined)    { mData.impl = nullptr; ALLOC_LLSD_OBJECT; assign(v); }
LLSD::LLSD(const URI& v) : mType(TypeUndefined)     { mData.impl = nullptr; ALLOC_LLSD_OBJECT; assign(v); }
LLSD::LLSD(const Binary& v) : mType(TypeUndefined)  { mData.impl = nullptr; ALLOC_LLSD_OBJECT; assign(v); }
LLSD::LLSD(String&& v) : mType(TypeUndefined)       { mData.impl = nullptr; ALLOC_LLSD_OBJECT; assign(std::move(v)); }
LLSD::LLSD(UUID&& v) : mType(TypeUndefined)         { mData.impl = nullptr; ALLOC_LLSD_OBJECT; assign(std::move(v)); }
LLSD::LLSD(URI&& v) : mType(TypeUndefined)          { mData.impl = nullptr; ALLOC_LLSD_OBJECT; assign(std::move(v)); }
LLSD::LLSD(Binary&& v) : mType(TypeUndefined)       { mData.impl = nullptr; ALLOC_LLSD_OBJECT; assign(std::move(v)); }

// Scalar Assignment
void LLSD::assign(Boolean v)
{
    releaseValue();
    mType = TypeBoolean;
    mData.b = v;
}

void LLSD::assign(Integer v)
{
    releaseValue();
    mType = TypeInteger;
    mData.i = v;
}

void LLSD::assign(Real v)
{
    releaseValue();
    mType = TypeReal;
    mData.r = v;
}

void LLSD::assign(const Date& v)
{
    releaseValue();
    mType = TypeDate;
    mData.r = v.secondsSinceEpoch();
}

void LLSD::assign(Date&& v)
{
    releaseValue();
    mType = TypeDate;
    mData.r = v.secondsSinceEpoch();
}

void LLSD::assign(const String& v)  { Impl::assignValue<ImplString>(*this, TypeString, v); }
void LLSD::assign(String&& v)       { Impl::assignValue<ImplString>(*this, TypeString, std::move(v)); }
void LLSD::assign(const UUID& v)    { Impl::assignValue<ImplUUID>(*this, TypeUUID, v); }
void LLSD::assign(UUID&& v)         { Impl::assignValue<ImplUUID>(*this, TypeUUID, std::move(v)); }
void LLSD::assign(const URI& v)     { Impl::assignValue<ImplURI>(*this, TypeURI, v); }
void LLSD::assign(URI&& v)          { Impl::assignValue<ImplURI>(*this, TypeURI, std::move(v)); }
void LLSD::assign(const Binary& v)  { Impl::assignValue<ImplBinary>(*this, TypeBinary, v); }
void LLSD::assign(Binary&& v)       { Impl::assignValue<ImplBinary>(*this, TypeBinary, std::move(v)); }

// Scalar Accessors
LLSD::Boolean LLSD::asBoolean() const
{
    switch (mType)
    {
    case TypeBoolean:   return mData.b;
    case TypeInteger:   return mData.i != 0;
    case TypeReal:      return !std::isnan(mData.r) && mData.r != 0.0;
    case TypeDate:
    case TypeUndefined: return false;
    default:            return mData.impl->asBoolean();
    }
}

LLSD::Integer LLSD::asInteger() const
{
    switch (mType)
    {
    case TypeBoolean:   return mData.b ? 1 : 0;
    case TypeInteger:   return mData.i;
    case TypeReal:      return !std::isnan(mData.r) ? (Integer)mData.r : 0;
    case TypeDate:      return (Integer)mData.r;
    case TypeUndefined: return 0;
    default:            return mData.impl->asInteger();
    }
}

LLSD::Real LLSD::asReal() const
{
    switch (mType)
    {
    case TypeBoolean:   return mData.b ? 1.0 : 0.0;
    case TypeInteger:   return mData.i;
    case TypeReal:
    case TypeDate:      return mData.r;
    case TypeUndefined: return 0.0;
    default:            return mData.impl->asReal();
    }
}

LLSD::String LLSD::asString() const
{
    switch (mType)
    {
    case TypeBoolean:
        // *NOTE: The reason that false is not converted to "false" is
        // because that would break roundtripping,
        // e.g. LLSD(false).asString().asBoolean().  There are many
        // reasons for wanting LLSD("false").asBoolean() == true, such
        // as "everything else seems to work that way".
        return mData.b ? "true" : "";
    case TypeInteger:
    {
        char buf[16];
        char* end = std::to_chars(buf, buf + sizeof(buf), mData.i).ptr;
        return String(buf, end);
    }
    case TypeReal:
        // {:g} matches the printf %g formatting this historically used
        return fmt::format("{:g}", mData.r);
    case TypeDate:
        return Date(mData.r).asString();
    case TypeUndefined:
        return String();
    default:
        return mData.impl->asString();
    }
}

LLSD::UUID LLSD::asUUID() const
{
    return isHeap() ? mData.impl->asUUID() : LLUUID();
}

LLSD::Date LLSD::asDate() const
{
    if (mType == TypeDate)
    {
        return Date(mData.r);
    }
    return isHeap() ? mData.impl->asDate() : LLDate();
}

LLSD::URI LLSD::asURI() const
{
    return isHeap() ? mData.impl->asURI() : LLURI();
}

const LLSD::Binary& LLSD::asBinary() const
{
    if (isHeap())
    {
        return mData.impl->asBinary();
    }
    static const Binary empty;
    return empty;
}

const LLSD::String& LLSD::asStringRef() const
{
    if (isHeap())
    {
        return mData.impl->asStringRef();
    }
    static const String empty;
    return empty;
}

void LLSD::asXMLRPCValue(std::ostream& os) const
{
    os << "<value>";
    switch (mType)
    {
    case TypeUndefined:
        os << "<nil/>";
        break;
    case TypeBoolean:
        os << (mData.b ? "<boolean>1</boolean>" : "<boolean>0</boolean>");
        break;
    case TypeInteger:
        os << "<int>" << mData.i << "</int>";
        break;
    case TypeReal:
    {
        // shortest representation that round-trips to the same double;
        // fmt rather than std::to_chars because Apple gates the
        // floating-point overloads behind macOS 13.3
        char buf[32];
        auto result = fmt::format_to_n(buf, sizeof(buf), "{}", mData.r);
        os << "<double>";
        os.write(buf, result.out - buf);
        os << "</double>";
        break;
    }
    case TypeDate:
        os << "<dateTime.iso8601>" << Date(mData.r).toHTTPDateString("%FT%T") << "</dateTime.iso8601>";
        break;
    default:
        mData.impl->asXMLRPCValue(os);
        break;
    }
    os << "</value>";
}

LLSD::String LLSD::asXMLRPCValue() const
{
    std::ostringstream os;
    asXMLRPCValue(os);
    return std::move(os).str();
}

// const char * helpers
LLSD::LLSD(const char* v) : mType(TypeUndefined)
{
    mData.impl = nullptr;
    ALLOC_LLSD_OBJECT;
    assign(v);
}

void LLSD::assign(const char* v)
{
    if (v)
    {
        Impl::assignValue<ImplString>(*this, TypeString, v);
    }
    else
    {
        assign(String());
    }
}

// std::string_view helpers
LLSD::LLSD(std::string_view v) : mType(TypeUndefined)
{
    mData.impl = nullptr;
    ALLOC_LLSD_OBJECT;
    assign(v);
}

void LLSD::assign(std::string_view v)
{
    Impl::assignValue<ImplString>(*this, TypeString, String(v));
}


LLSD LLSD::emptyMap()
{
    LLSD v;
    Impl::mutableMap(v);
    return v;
}

bool LLSD::has(const std::string_view k) const
{
    return mType == TypeMap && static_cast<const ImplMap*>(mData.impl)->has(k);
}

LLSD LLSD::get(const std::string_view k) const
{
    return (mType == TypeMap) ? static_cast<const ImplMap*>(mData.impl)->get(k) : LLSD();
}

LLSD LLSD::getKeys() const
{
    return (mType == TypeMap) ? static_cast<const ImplMap*>(mData.impl)->getKeys() : emptyArray();
}

void LLSD::insert(std::string&& k, const LLSD& v)    { Impl::mutableMap(*this).insert(std::move(k), v); }
void LLSD::insert(std::string&& k, LLSD&& v)         { Impl::mutableMap(*this).insert(std::move(k), std::move(v)); }
void LLSD::insert(std::string_view k, const LLSD& v) { Impl::mutableMap(*this).insert(k, v); }
void LLSD::insert(std::string_view k, LLSD&& v)      { Impl::mutableMap(*this).insert(k, std::move(v)); }

LLSD& LLSD::with(std::string&& k, const LLSD& v)
{
    Impl::mutableMap(*this).insert(std::move(k), v);
    return *this;
}
LLSD& LLSD::with(std::string&& k, LLSD&& v)
{
    Impl::mutableMap(*this).insert(std::move(k), std::move(v));
    return *this;
}
LLSD& LLSD::with(std::string_view k, const LLSD& v)
{
    Impl::mutableMap(*this).insert(k, v);
    return *this;
}
LLSD& LLSD::with(std::string_view k, LLSD&& v)
{
    Impl::mutableMap(*this).insert(k, std::move(v));
    return *this;
}

void LLSD::erase(const String& k)       { Impl::mutableMap(*this).erase(k); }

LLSD& LLSD::operator[](const std::string_view k)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_LLSD;
    return Impl::mutableMap(*this).ref(k);
}

LLSD& LLSD::operator[](std::string&& k)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_LLSD;
    return Impl::mutableMap(*this).ref(std::move(k));
}

const LLSD& LLSD::operator[](const std::string_view k) const
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_LLSD;
    return (mType == TypeMap) ? static_cast<const ImplMap*>(mData.impl)->ref(k) : Impl::undef();
}

LLSD LLSD::emptyArray()
{
    LLSD v;
    Impl::mutableArray(v);
    return v;
}

LLSD LLSD::emptyReservedArray(size_t size)
{
    LLSD v;
    Impl::mutableArray(v).reserve(size);
    return v;
}

size_t LLSD::size() const
{
    switch (mType)
    {
    case TypeMap:    return static_cast<const ImplMap*>(mData.impl)->size();
    case TypeArray:  return static_cast<const ImplArray*>(mData.impl)->size();
    case TypeString: return static_cast<const ImplString*>(mData.impl)->size();
    default:         return 0;
    }
}

LLSD LLSD::get(Integer i) const
{
    return (mType == TypeArray) ? static_cast<const ImplArray*>(mData.impl)->get(i) : LLSD();
}

void LLSD::set(Integer i, const LLSD& v){ Impl::mutableArray(*this).set(i, v); }
void LLSD::set(Integer i, LLSD&& v)     { Impl::mutableArray(*this).set(i, std::move(v)); }
void LLSD::insert(Integer i, const LLSD& v) { Impl::mutableArray(*this).insert(i, v); }
void LLSD::insert(Integer i, LLSD&& v)  { Impl::mutableArray(*this).insert(i, std::move(v)); }

LLSD& LLSD::with(Integer i, const LLSD& v)
{
    Impl::mutableArray(*this).insert(i, v);
    return *this;
}
LLSD& LLSD::with(Integer i, LLSD&& v)
{
    Impl::mutableArray(*this).insert(i, std::move(v));
    return *this;
}

LLSD& LLSD::append(const LLSD& v)       { return Impl::mutableArray(*this).append(v); }
LLSD& LLSD::append(LLSD&& v)            { return Impl::mutableArray(*this).append(std::move(v)); }
void LLSD::erase(Integer i)             { Impl::mutableArray(*this).erase(i); }

LLSD& LLSD::operator[](size_t i)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_LLSD;
    return Impl::mutableArray(*this).ref(i);
}

const LLSD& LLSD::operator[](size_t i) const
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_LLSD;
    return (mType == TypeArray) ? static_cast<const ImplArray*>(mData.impl)->ref(i) : Impl::undef();
}

static const char *llsd_dump(const LLSD &llsd, bool useXMLFormat)
{
    // sStorage is used to hold the string representation of the llsd last
    // passed into this function.  If this function is never called (the
    // normal case when not debugging), nothing is allocated.  Otherwise
    // sStorage will point to the result of the last call.  This will actually
    // be one leak, but since this is used only when running under the
    // debugger, it should not be an issue.
    static char *sStorage = NULL;
    delete[] sStorage;
    std::string out_string;
    {
        std::ostringstream out;
        if (useXMLFormat)
            out << LLSDXMLStreamer(llsd);
        else
            out << LLSDNotationStreamer(llsd);
        out_string = out.str();
    }
    auto len = out_string.length();
    sStorage = new char[len + 1];
    memcpy(sStorage, out_string.c_str(), len);
    sStorage[len] = '\0';
    return sStorage;
}

/// Returns XML version of llsd -- only to be called from debugger
const char *LLSD::dumpXML(const LLSD &llsd)
{
    return llsd_dump(llsd, true);
}

/// Returns Notation version of llsd -- only to be called from debugger
const char *LLSD::dump(const LLSD &llsd)
{
    return llsd_dump(llsd, false);
}

LLSD::map_iterator LLSD::beginMap()
{
    return Impl::mutableMap(*this).beginMap();
}

LLSD::map_iterator LLSD::endMap()
{
    return Impl::mutableMap(*this).endMap();
}

LLSD::map_const_iterator LLSD::beginMap() const
{
    return (mType == TypeMap) ? static_cast<const ImplMap*>(mData.impl)->beginMap() : endMap();
}

LLSD::map_const_iterator LLSD::endMap() const
{
    if (mType == TypeMap)
    {
        return static_cast<const ImplMap*>(mData.impl)->endMap();
    }
    static const llsd_map_t empty;
    return empty.end();
}

LLSD::array_iterator LLSD::beginArray()
{
    return Impl::mutableArray(*this).beginArray();
}

LLSD::array_iterator LLSD::endArray()
{
    return Impl::mutableArray(*this).endArray();
}

LLSD::array_const_iterator LLSD::beginArray() const
{
    return (mType == TypeArray) ? static_cast<const ImplArray*>(mData.impl)->beginArray() : endArray();
}

LLSD::array_const_iterator LLSD::endArray() const
{
    if (mType == TypeArray)
    {
        return static_cast<const ImplArray*>(mData.impl)->endArray();
    }
    static const std::vector<LLSD> empty;
    return empty.end();
}

LLSD::reverse_array_iterator LLSD::rbeginArray()
{
    return Impl::mutableArray(*this).rbeginArray();
}

LLSD::reverse_array_iterator LLSD::rendArray()
{
    return Impl::mutableArray(*this).rendArray();
}

namespace llsd
{

LLSD::Real string_to_real(std::string_view in_string)
{
    // Match the historical istream semantics: skip leading whitespace and an
    // optional '+', then the entire remainder must parse or the result is 0.0.
    const char* first = in_string.data();
    const char* const last = first + in_string.size();
    while (first != last && (*first == ' ' || (*first >= '\t' && *first <= '\r')))
    {
        ++first;
    }
    if (first != last && *first == '+')
    {
        ++first;
    }
    LLSD::Real v = 0.0;
    auto [ptr, ec] = fast_float::from_chars(first, last, v);
    return (ptr == last && ec == std::errc()) ? v : 0.0;
}

U32 allocationCount()                               { return LLSD::Impl::sAllocationCount; }
U32 outstandingCount()                              { return LLSD::Impl::sOutstandingCount; }

// Diagnostic dump of contents in an LLSD object
void dumpStats(const LLSD& llsd)                    { LLSD::Impl::dumpStats(llsd); }

} // namespace llsd

// static
std::string     LLSD::typeString(Type type)
{
    static const char * sTypeNameArray[] = {
        "Undefined",
        "Boolean",
        "Integer",
        "Real",
        "String",
        "UUID",
        "Date",
        "URI",
        "Binary",
        "Map",
        "Array"
    };

    if (0 <= type && type < LL_ARRAY_SIZE(sTypeNameArray))
    {
        return sTypeNameArray[type];
    }
    return STRINGIZE("** invalid type value " << type);
}
