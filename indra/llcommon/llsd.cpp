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

#include "llerror.h"
#include "llmath.h"
#include "llformat.h"
#include "llsdserialize.h"
#include "stringize.h"

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
S32	sLLSDAllocationCount = 0;
S32 sLLSDNetObjects = 0;

} // namespace llsd

#define	ALLOC_LLSD_OBJECT			{ llsd::sLLSDNetObjects++;	llsd::sLLSDAllocationCount++;	}
#define	FREE_LLSD_OBJECT			{ llsd::sLLSDNetObjects--;									}

class LLSD::Impl
	/**< This class is the abstract base class of the implementation of LLSD
		 It provides the reference counting implementation, and the default
		 implementation of most methods for most data types.  It also serves
		 as a working implementation of the Undefined type.
		
	*/
{
protected:
	Impl();

	enum StaticAllocationMarker { STATIC_USAGE_COUNT = 0xFFFFFFFF };
	Impl(StaticAllocationMarker);
		///< This constructor is used for static objects and causes the
		//   suppresses adjusting the debugging counters when they are
		//	 finally initialized.
		
	virtual ~Impl();
	
	bool shared() const							{ return (mUseCount > 1) && (mUseCount != STATIC_USAGE_COUNT); }
	
	U32 mUseCount;

public:
	static void reset(Impl*& var, Impl* impl);
		///< safely set var to refer to the new impl (possibly shared)

    static void move(Impl*& var, Impl*& impl);
        ///< safely move impl from one object to another

	static       Impl& safe(      Impl*);
	static const Impl& safe(const Impl*);
		///< since a NULL Impl* is used for undefined, this ensures there is
		//	 always an object you call virtual member functions on
		
	virtual ImplMap& makeMap(Impl*& var);
	virtual ImplArray& makeArray(Impl*& var);
		///< sure var is a modifiable, non-shared map or array
	
	virtual LLSD::Type type() const				{ return LLSD::TypeUndefined; }
	
	static  void assignUndefined(LLSD::Impl*& var);
	static  void assign(LLSD::Impl*& var, const LLSD::Impl* other);
	
	virtual void assign(Impl*& var, LLSD::Boolean);
	virtual void assign(Impl*& var, LLSD::Integer);
	virtual void assign(Impl*& var, LLSD::Real);
	virtual void assign(Impl*& var, LLSD::String);
	virtual void assign(Impl*& var, LLSD::UUID);
	virtual void assign(Impl*& var, LLSD::Date);
	virtual void assign(Impl*& var, LLSD::URI);
	virtual void assign(Impl*& var, LLSD::Binary);
		///< If the receiver is the right type and unshared, these are simple
		//   data assignments, othewise the default implementation handless
		//   constructing the proper Impl subclass
		 
	virtual Boolean	asBoolean() const			{ return false; }
	virtual Integer	asInteger() const			{ return 0; }
	virtual Real	asReal() const				{ return 0.0; }
	virtual String	asString() const			{ return std::string(); }
	virtual UUID	asUUID() const				{ return LLUUID(); }
	virtual Date	asDate() const				{ return LLDate(); }
	virtual URI		asURI() const				{ return LLURI(); }
	virtual const Binary&	asBinary() const	{ static const std::vector<U8> empty; return empty; }

	virtual const String& asStringRef() const { static const std::string empty; return empty; } 
	
	virtual bool has(const std::string_view) const		{ return false; }
	virtual LLSD get(const std::string_view) const		{ return LLSD(); }
	virtual LLSD getKeys() const				{ return LLSD::emptyArray(); }
	virtual void erase(const String&)			{ }
	virtual const LLSD& ref(const std::string_view) const{ return undef(); }
	
	virtual int size() const					{ return 0; }
	virtual LLSD get(Integer) const				{ return LLSD(); }
	virtual void erase(Integer)					{ }
	virtual const LLSD& ref(Integer) const		{ return undef(); }

	virtual const LLSD::map_t& map() const { static const LLSD::map_t empty; return empty; }
	virtual LLSD::map_t& map() { static LLSD::map_t empty; return empty; }
	LLSD::map_const_iterator beginMap() const { return endMap(); }
	LLSD::map_const_iterator endMap() const { return map().end(); }
	virtual const std::vector<LLSD>& array() const { static const std::vector<LLSD> empty; return empty; }
	virtual std::vector<LLSD>& array() { static std::vector<LLSD> empty; return empty; }
	LLSD::array_const_iterator beginArray() const { return endArray(); }
	LLSD::array_const_iterator endArray() const { return array().end(); }


	virtual void dumpStats() const;
	virtual void calcStats(S32 type_counts[], S32 share_counts[]) const;
	// Container subclasses contain LLSD objects, rather than directly
	// containing Impl objects. This helper forwards through LLSD.
	void calcStats(const LLSD& llsd, S32 type_counts[], S32 share_counts[]) const
	{
		safe(llsd.impl).calcStats(type_counts, share_counts);
	}

	static const Impl& getImpl(const LLSD& llsd)	{ return safe(llsd.impl); }
	static Impl& getImpl(LLSD& llsd)				{ return safe(llsd.impl); }

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
	template<LLSD::Type T, class Data>
	class ImplBase : public LLSD::Impl
		///< This class handles most of the work for a subclass of Impl
		//   for a given simple data type.  Subclasses of this provide the
		//   conversion functions and a constructor.
	{
	protected:
		Data mValue;
		
		typedef ImplBase Base;

	public:
        ImplBase(Data value) : mValue(std::move(value)) {}

		LLSD::Type type() const override { return T; }

		using LLSD::Impl::assign; // Unhiding base class virtuals...
		virtual void assign(LLSD::Impl*& var, Data value) 
		{
			if (shared())
			{
                Impl::assign(var, std::move(value));
			}
			else
			{
				mValue = std::move(value);
			}
		}
	};

	
	class ImplBoolean final
		: public ImplBase<LLSD::TypeBoolean, LLSD::Boolean>
	{
	public:
		ImplBoolean(LLSD::Boolean v) : Base(v) { }
		
		virtual LLSD::Boolean	asBoolean() const	{ return mValue; }
		virtual LLSD::Integer	asInteger() const	{ return mValue ? 1 : 0; }
		virtual LLSD::Real		asReal() const		{ return mValue ? 1 : 0; }
		virtual LLSD::String	asString() const;
	};

	LLSD::String ImplBoolean::asString() const
		// *NOTE: The reason that false is not converted to "false" is
		// because that would break roundtripping,
		// e.g. LLSD(false).asString().asBoolean().  There are many
		// reasons for wanting LLSD("false").asBoolean() == true, such
		// as "everything else seems to work that way".
		{ return mValue ? "true" : ""; }


	class ImplInteger final
		: public ImplBase<LLSD::TypeInteger, LLSD::Integer>
	{
	public:
		ImplInteger(LLSD::Integer v) : Base(v) { }
		
		virtual LLSD::Boolean	asBoolean() const	{ return mValue != 0; }
		virtual LLSD::Integer	asInteger() const	{ return mValue; }
		virtual LLSD::Real		asReal() const		{ return mValue; }
		virtual LLSD::String	asString() const;
	};

	LLSD::String ImplInteger::asString() const
		{ return llformat("%d", mValue); }


	class ImplReal final
		: public ImplBase<LLSD::TypeReal, LLSD::Real>
	{
	public:
		ImplReal(LLSD::Real v) : Base(v) { }
				
		virtual LLSD::Boolean	asBoolean() const;
		virtual LLSD::Integer	asInteger() const;
		virtual LLSD::Real		asReal() const		{ return mValue; }
		virtual LLSD::String	asString() const;
	};

	LLSD::Boolean ImplReal::asBoolean() const
		{ return !llisnan(mValue)  &&  mValue != 0.0; }
		
	LLSD::Integer ImplReal::asInteger() const
		{ return !llisnan(mValue) ? (LLSD::Integer)mValue : 0; }
		
	LLSD::String ImplReal::asString() const
		{ return llformat("%lg", mValue); }


	class ImplString final
		: public ImplBase<LLSD::TypeString, LLSD::String>
	{
	public:
		ImplString(LLSD::String v) : Base(std::move(v)) { }
				
		virtual LLSD::Boolean	asBoolean() const	{ return !mValue.empty(); }
		virtual LLSD::Integer	asInteger() const;
		virtual LLSD::Real		asReal() const;
		virtual LLSD::String	asString() const	{ return mValue; }
		virtual LLSD::UUID		asUUID() const	{ return LLUUID(mValue); }
		virtual LLSD::Date		asDate() const	{ return LLDate(mValue); }
		virtual LLSD::URI		asURI() const	{ return LLURI(mValue); }
		virtual int				size() const	{ return mValue.size(); }
		virtual const LLSD::String&	asStringRef() const { return mValue; }
	};
	
	LLSD::Integer	ImplString::asInteger() const
	{
		// This must treat "1.23" not as an error, but as a number, which is
		// then truncated down to an integer.  Hence, this code doesn't call
		// std::istringstream::operator>>(int&), which would not consume the
		// ".23" portion.
		
		return (int)asReal();
	}
	
	LLSD::Real		ImplString::asReal() const
	{
		F64 v = 0.0;
		std::istringstream i_stream(mValue);
		i_stream >> v;

		// we would probably like to ignore all trailing whitespace as
		// well, but for now, simply eat the next character, and make
		// sure we reached the end of the string.
		// *NOTE: gcc 2.95 does not generate an eof() event on the
		// stream operation above, so we manually get here to force it
		// across platforms.
		int c = i_stream.get();
		return ((EOF ==c) ? v : 0.0);
	}
	

	class ImplUUID final
		: public ImplBase<LLSD::TypeUUID, LLSD::UUID>
	{
	public:
		ImplUUID(LLSD::UUID v) : Base(std::move(v)) { }
				
		virtual LLSD::String	asString() const{ return mValue.asString(); }
		virtual LLSD::UUID		asUUID() const	{ return mValue; }
	};


	class ImplDate final
		: public ImplBase<LLSD::TypeDate, LLSD::Date>
	{
	public:
		ImplDate(LLSD::Date v) : Base(std::move(v)) { }
		
		virtual LLSD::Integer asInteger() const
		{
			return (LLSD::Integer)(mValue.secondsSinceEpoch());
		}
		virtual LLSD::Real asReal() const
		{
			return mValue.secondsSinceEpoch();
		}
		virtual LLSD::String	asString() const{ return mValue.asString(); }
		virtual LLSD::Date		asDate() const	{ return mValue; }
	};


	class ImplURI final
		: public ImplBase<LLSD::TypeURI, LLSD::URI>
	{
	public:
		ImplURI(LLSD::URI v) : Base(std::move(v)) { }
				
		virtual LLSD::String	asString() const{ return mValue.asString(); }
		virtual LLSD::URI		asURI() const	{ return mValue; }
	};


	class ImplBinary final
		: public ImplBase<LLSD::TypeBinary, LLSD::Binary>
	{
	public:
		ImplBinary(LLSD::Binary v) : Base(std::move(v)) { }
				
		virtual const LLSD::Binary&	asBinary() const{ return mValue; }
	};


	class ImplMap final : public LLSD::Impl
	{
	private:
		typedef LLSD::map_t	DataMap;
		
		DataMap mData;
		
	protected:
		ImplMap(DataMap data) : mData(std::move(data)) { }
		
	public:
		ImplMap() = default;
		
        ImplMap& makeMap(LLSD::Impl*&) override;

        LLSD::Type type() const override { return LLSD::TypeMap; }

        LLSD::Boolean asBoolean() const override { return !mData.empty(); }

        bool has(const std::string_view) const override;

		using LLSD::Impl::get; // Unhiding get(LLSD::Integer)
		using LLSD::Impl::erase; // Unhiding erase(LLSD::Integer)
		using LLSD::Impl::ref; // Unhiding ref(LLSD::Integer)
        LLSD get(const std::string_view) const override;
        LLSD getKeys() const override;
		        void insert(const LLSD::String& k, const LLSD& v);
        void erase(const LLSD::String&) override;
		              LLSD& ref(const std::string_view);
        const LLSD& ref(const std::string_view) const override;

        int size() const override { return mData.size(); }

		DataMap& map() final override { return mData; }
		const DataMap& map() const final override { return mData; }

        void dumpStats() const override;
        void calcStats(S32 type_counts[], S32 share_counts[]) const override;
	};
	
	ImplMap& ImplMap::makeMap(LLSD::Impl*& var)
	{
		if (shared())
		{
			ImplMap* i = new ImplMap(mData);
			Impl::assign(var, i);
			return *i;
		}
		else
		{
			return *this;
		}
	}
	
	bool ImplMap::has(const std::string_view k) const
	{
		DataMap::const_iterator i = mData.find(k);
		return i != mData.end();
	}
	
	LLSD ImplMap::get(const std::string_view k) const
	{
		DataMap::const_iterator i = mData.find(k);
		return (i != mData.end()) ? i->second : LLSD();
	}

	LLSD ImplMap::getKeys() const
	{ 
		LLSD keys = LLSD::emptyArray();
		DataMap::const_iterator iter = mData.begin();
		while (iter != mData.end())
		{
			keys.append((*iter).first);
			iter++;
		}
		return keys;
	}

	void ImplMap::insert(const LLSD::String& k, const LLSD& v)
	{
		mData.emplace(DataMap::value_type(k, v));
	}
	
	void ImplMap::erase(const LLSD::String& k)
	{
		mData.erase(k);
	}
	
	LLSD& ImplMap::ref(const std::string_view k)
	{
		DataMap::iterator i = mData.find(k);
		if (i == mData.end())
		{
			return mData.emplace(k, LLSD()).first->second;
		}

		return i->second;
	}
	
	const LLSD& ImplMap::ref(const std::string_view k) const
	{
		DataMap::const_iterator i = mData.find(k);
		if (i == mData.end())
		{
			return undef();
		}
		
		return i->second;
	}

	void ImplMap::dumpStats() const
	{
		std::cout << "Map size: " << mData.size() << std::endl;

		std::cout << "LLSD Net Objects: " << llsd::sLLSDNetObjects << std::endl;
		std::cout << "LLSD allocations: " << llsd::sLLSDAllocationCount << std::endl;

		std::cout << "LLSD::Impl Net Objects: " << sOutstandingCount << std::endl;
		std::cout << "LLSD::Impl allocations: " << sAllocationCount << std::endl;

		Impl::dumpStats();
	}

	void ImplMap::calcStats(S32 type_counts[], S32 share_counts[]) const
	{
		LLSD::map_const_iterator iter = beginMap();
		while (iter != endMap())
		{
			//std::cout << "  " << (*iter).first << ": " << (*iter).second << std::endl;
			Impl::calcStats((*iter).second, type_counts, share_counts);
			++iter;
		}

		// Add in the values for this map
		Impl::calcStats(type_counts, share_counts);
	}


	class ImplArray final : public LLSD::Impl
	{
	private:
		typedef std::vector<LLSD>	DataVector;
		
		DataVector mData;
		
	protected:
		ImplArray(DataVector data) : mData(std::move(data)) { }
		
	public:
        ImplArray() = default;
		
        ImplArray& makeArray(Impl*&) override;

        LLSD::Type type() const override { return LLSD::TypeArray; }

        LLSD::Boolean asBoolean() const override { return !mData.empty(); }

		using LLSD::Impl::get; // Unhiding get(LLSD::String)
		using LLSD::Impl::erase; // Unhiding erase(LLSD::String)
		using LLSD::Impl::ref; // Unhiding ref(LLSD::String)
        int size() const override;
        LLSD get(LLSD::Integer) const override;
		        void set(LLSD::Integer, const LLSD&);
		        void insert(LLSD::Integer, const LLSD&);
		        LLSD& append(const LLSD&);
        void erase(LLSD::Integer) override;
		              LLSD& ref(LLSD::Integer);
        const LLSD& ref(LLSD::Integer) const override;

		DataVector& array() final override { return mData; }
		const DataVector& array() const final override { return mData; }

        void calcStats(S32 type_counts[], S32 share_counts[]) const override;
	};

	ImplArray& ImplArray::makeArray(Impl*& var)
	{
		if (shared())
		{
			ImplArray* i = new ImplArray(mData);
			Impl::assign(var, i);
			return *i;
		}
		else
		{
			return *this;
		}
	}
	
	int ImplArray::size() const		{ return mData.size(); }
	
	LLSD ImplArray::get(LLSD::Integer i) const
	{
		if (i < 0) { return LLSD(); }
		DataVector::size_type index = i;
		
		return (index < mData.size()) ? mData[index] : LLSD();
	}
	
	void ImplArray::set(LLSD::Integer i, const LLSD& v)
	{
		if (i < 0) { return; }
		DataVector::size_type index = i;
		
		if (index >= mData.size())
		{
			mData.resize(index + 1);
		}
		
		mData[index] = v;
	}
	
	void ImplArray::insert(LLSD::Integer i, const LLSD& v)
	{
		if (i < 0) 
		{
			return;
		}
		DataVector::size_type index = i;
		
		if (index >= mData.size())	// tbd - sanity check limit for index ?
		{
			mData.resize(index + 1);
		}
		
		mData.insert(mData.begin() + index, v);
	}
	
	LLSD& ImplArray::append(const LLSD& v)
	{
		mData.push_back(v);
		return mData.back();
	}
	
	void ImplArray::erase(LLSD::Integer i)
	{
		if (i < 0) { return; }
		DataVector::size_type index = i;
		
		if (index < mData.size())
		{
			mData.erase(mData.begin() + index);
		}
	}
	
	LLSD& ImplArray::ref(LLSD::Integer i)
	{
		DataVector::size_type index = i >= 0 ? i : 0;
		
		if (index >= mData.size())
		{
			mData.resize(i + 1);
		}
		
		return mData[index];
	}

	const LLSD& ImplArray::ref(LLSD::Integer i) const
	{
		if (i < 0) { return undef(); }
		DataVector::size_type index = i;
		
		if (index >= mData.size())
		{
			return undef();
		}
		
		return mData[index];
	}

	void ImplArray::calcStats(S32 type_counts[], S32 share_counts[]) const
	{
		LLSD::array_const_iterator iter = beginArray();
		while (iter != endArray())
		{	// Add values for all items held in the array
			Impl::calcStats((*iter), type_counts, share_counts);
			++iter;
		}

		// Add in the values for this array
		Impl::calcStats(type_counts, share_counts);
	}
}

LLSD::Impl::Impl()
	: mUseCount(0)
{
	++sAllocationCount;
	++sOutstandingCount;
}

LLSD::Impl::Impl(StaticAllocationMarker)
	: mUseCount(0)
{
}

LLSD::Impl::~Impl()
{
	--sOutstandingCount;
}

void LLSD::Impl::reset(Impl*& var, Impl* impl)
{
	if (var != impl)
	{
		if (impl && impl->mUseCount != STATIC_USAGE_COUNT)
		{
			++impl->mUseCount;
		}
		if (var && var->mUseCount != STATIC_USAGE_COUNT && --var->mUseCount == 0)
		{
			delete var;
		}
		var = impl;
	}
}

void LLSD::Impl::move(Impl*& var, Impl*& impl)
{
	if (var == impl) return; // Bail out var is impl

	if (var && var->mUseCount != STATIC_USAGE_COUNT && --var->mUseCount == 0)
	{
		delete var; // destroy var if usage falls to 0 and not static
	}
	var = impl; // Steal impl to var without incrementing use since this is a move
	impl = nullptr; // null out old-impl pointer
}

LLSD::Impl& LLSD::Impl::safe(Impl* impl)
{
	static Impl theUndefined(STATIC_USAGE_COUNT);
	return impl ? *impl : theUndefined;
}

const LLSD::Impl& LLSD::Impl::safe(const Impl* impl)
{
	static Impl theUndefined(STATIC_USAGE_COUNT);
	return impl ? *impl : theUndefined;
}

ImplMap& LLSD::Impl::makeMap(Impl*& var)
{
	ImplMap* im = new ImplMap;
	reset(var, im);
	return *im;
}

ImplArray& LLSD::Impl::makeArray(Impl*& var)
{
	ImplArray* ia = new ImplArray;
	reset(var, ia);
	return *ia;
}


void LLSD::Impl::assign(Impl*& var, const Impl* other)
{
	reset(var, const_cast<Impl*>(other));
}

void LLSD::Impl::assignUndefined(Impl*& var)
{
	reset(var, 0);
}

void LLSD::Impl::assign(Impl*& var, LLSD::Boolean v)
{
	reset(var, new ImplBoolean(v));
}

void LLSD::Impl::assign(Impl*& var, LLSD::Integer v)
{
	reset(var, new ImplInteger(v));
}

void LLSD::Impl::assign(Impl*& var, LLSD::Real v)
{
	reset(var, new ImplReal(v));
}

void LLSD::Impl::assign(Impl*& var, LLSD::String v)
{
	reset(var, new ImplString(std::move(v)));
}

void LLSD::Impl::assign(Impl*& var, LLSD::UUID v)
{
	reset(var, new ImplUUID(std::move(v)));
}

void LLSD::Impl::assign(Impl*& var, LLSD::Date v)
{
	reset(var, new ImplDate(std::move(v)));
}

void LLSD::Impl::assign(Impl*& var, LLSD::URI v)
{
	reset(var, new ImplURI(std::move(v)));
}

void LLSD::Impl::assign(Impl*& var, LLSD::Binary v)
{
	reset(var, new ImplBinary(std::move(v)));
}


const LLSD& LLSD::Impl::undef()
{
	static const LLSD immutableUndefined;
	return immutableUndefined;
}

void LLSD::Impl::dumpStats() const
{
	S32 type_counts[LLSD::TypeLLSDNumTypes + 1];
	memset(&type_counts, 0, sizeof(type_counts));

	S32 share_counts[LLSD::TypeLLSDNumTypes + 1];
	memset(&share_counts, 0, sizeof(share_counts));

	// Add info from all the values this object has
	calcStats(type_counts, share_counts);

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


void LLSD::Impl::calcStats(S32 type_counts[], S32 share_counts[]) const
{
	S32 tp = S32(type());
	if (0 <= tp && tp < LLSD::TypeLLSDNumTypes)
	{
		type_counts[tp]++;	
		if (shared())
		{
			share_counts[tp]++;
		}
	}
}


U32 LLSD::Impl::sAllocationCount = 0;
U32 LLSD::Impl::sOutstandingCount = 0;



#ifdef NAME_UNNAMED_NAMESPACE
namespace LLSDUnnamedNamespace 
#else
namespace 
#endif
{
	inline LLSD::Impl& safe(LLSD::Impl* impl)
		{ return LLSD::Impl::safe(impl); }

	inline ImplMap& makeMap(LLSD::Impl*& var)
		{ return safe(var).makeMap(var); }
		
	inline ImplArray& makeArray(LLSD::Impl*& var)
		{ return safe(var).makeArray(var); }
}


LLSD::LLSD() : impl(nullptr)					{ ALLOC_LLSD_OBJECT; }
LLSD::~LLSD()							{ FREE_LLSD_OBJECT; Impl::reset(impl, nullptr); }

LLSD::LLSD(const LLSD& other) : impl(nullptr) { ALLOC_LLSD_OBJECT;  assign(other); }
void LLSD::assign(const LLSD& other)	{ Impl::assign(impl, other.impl); }

LLSD::LLSD(LLSD&& other) noexcept : impl(nullptr) { ALLOC_LLSD_OBJECT;  Impl::move(impl, other.impl); }
void  LLSD::assign(LLSD&& other) { Impl::move(impl, other.impl); }
LLSD& LLSD::operator=(LLSD&& other) noexcept { Impl::move(impl, other.impl); return *this; }

void LLSD::clear()						{ Impl::assignUndefined(impl); }

LLSD::Type LLSD::type() const			{ return safe(impl).type(); }

// Scalar Constructors
LLSD::LLSD(Boolean v) : impl(0)			{ ALLOC_LLSD_OBJECT;	assign(v); }
LLSD::LLSD(Integer v) : impl(0)			{ ALLOC_LLSD_OBJECT;	assign(v); }
LLSD::LLSD(Real v) : impl(0)			{ ALLOC_LLSD_OBJECT;	assign(v); }
LLSD::LLSD(const UUID& v) : impl(0)		{ ALLOC_LLSD_OBJECT;	assign(v); }
LLSD::LLSD(const String& v) : impl(0)	{ ALLOC_LLSD_OBJECT;	assign(v); }
LLSD::LLSD(const Date& v) : impl(0)		{ ALLOC_LLSD_OBJECT;	assign(v); }
LLSD::LLSD(const URI& v) : impl(0)		{ ALLOC_LLSD_OBJECT;	assign(v); }
LLSD::LLSD(const Binary& v) : impl(0)	{ ALLOC_LLSD_OBJECT;	assign(v); }

LLSD::LLSD(UUID&& v) : impl(0)			{ ALLOC_LLSD_OBJECT;	assign(std::move(v)); }
LLSD::LLSD(String&& v) : impl(0)		{ ALLOC_LLSD_OBJECT;	assign(std::move(v)); }
LLSD::LLSD(Date&& v) : impl(0)			{ ALLOC_LLSD_OBJECT;	assign(std::move(v)); }
LLSD::LLSD(URI&& v) : impl(0)			{ ALLOC_LLSD_OBJECT;	assign(std::move(v)); }
LLSD::LLSD(Binary&& v) : impl(0)		{ ALLOC_LLSD_OBJECT;	assign(std::move(v)); }

// Convenience Constructors
LLSD::LLSD(F32 v) : impl(0)				{ ALLOC_LLSD_OBJECT;	assign((Real)v); }

// Scalar Assignment
void LLSD::assign(Boolean v)			{ safe(impl).assign(impl, v); }
void LLSD::assign(Integer v)			{ safe(impl).assign(impl, v); }
void LLSD::assign(Real v)				{ safe(impl).assign(impl, v); }
void LLSD::assign(const String& v)		{ safe(impl).assign(impl, v); }
void LLSD::assign(const UUID& v)		{ safe(impl).assign(impl, v); }
void LLSD::assign(const Date& v)		{ safe(impl).assign(impl, v); }
void LLSD::assign(const URI& v)			{ safe(impl).assign(impl, v); }
void LLSD::assign(const Binary& v)		{ safe(impl).assign(impl, v); }

void LLSD::assign(String&& v)			{ safe(impl).assign(impl, std::move(v)); }
void LLSD::assign(UUID&& v)				{ safe(impl).assign(impl, std::move(v)); }
void LLSD::assign(Date&& v)				{ safe(impl).assign(impl, std::move(v)); }
void LLSD::assign(URI&& v)				{ safe(impl).assign(impl, std::move(v)); }
void LLSD::assign(Binary&& v)			{ safe(impl).assign(impl, std::move(v)); }

// Scalar Accessors
LLSD::Boolean	LLSD::asBoolean() const	{ return safe(impl).asBoolean(); }
LLSD::Integer	LLSD::asInteger() const	{ return safe(impl).asInteger(); }
LLSD::Real		LLSD::asReal() const	{ return safe(impl).asReal(); }
LLSD::String	LLSD::asString() const	{ return safe(impl).asString(); }
LLSD::UUID		LLSD::asUUID() const	{ return safe(impl).asUUID(); }
LLSD::Date		LLSD::asDate() const	{ return safe(impl).asDate(); }
LLSD::URI		LLSD::asURI() const		{ return safe(impl).asURI(); }
const LLSD::Binary&	LLSD::asBinary() const	{ return safe(impl).asBinary(); }

const LLSD::String& LLSD::asStringRef() const { return safe(impl).asStringRef(); }

// const char * helpers
LLSD::LLSD(const char* v) : impl(0)		{ ALLOC_LLSD_OBJECT;	assign(v); }
void LLSD::assign(const char* v)
{
	if(v) assign(std::move(std::string(v)));
	else assign(std::move(std::string()));
}


LLSD LLSD::emptyMap()
{
	LLSD v;
	makeMap(v.impl);
	return v;
}

bool LLSD::has(const std::string_view k) const	{ return safe(impl).has(k); }
LLSD LLSD::get(const std::string_view k) const	{ return safe(impl).get(k); }
LLSD LLSD::getKeys() const				{ return safe(impl).getKeys(); } 
void LLSD::insert(const String& k, const LLSD& v) {	makeMap(impl).insert(k, v); }

LLSD& LLSD::with(const String& k, const LLSD& v)
										{ 
											makeMap(impl).insert(k, v); 
											return *this;
										}
void LLSD::erase(const String& k)		{ makeMap(impl).erase(k); }

LLSD&		LLSD::operator[](const std::string_view k)
										{ return makeMap(impl).ref(k); }
const LLSD& LLSD::operator[](const std::string_view k) const
										{ return safe(impl).ref(k); }


LLSD LLSD::emptyArray()
{
	LLSD v;
	makeArray(v.impl);
	return v;
}

int LLSD::size() const					{ return safe(impl).size(); }
 
LLSD LLSD::get(Integer i) const			{ return safe(impl).get(i); } 
void LLSD::set(Integer i, const LLSD& v){ makeArray(impl).set(i, v); }
void LLSD::insert(Integer i, const LLSD& v) { makeArray(impl).insert(i, v); }

LLSD& LLSD::with(Integer i, const LLSD& v)
										{ 
											makeArray(impl).insert(i, v); 
											return *this;
										}
LLSD& LLSD::append(const LLSD& v)		{ return makeArray(impl).append(v); }
void LLSD::erase(Integer i)				{ makeArray(impl).erase(i); }

LLSD&		LLSD::operator[](Integer i)
										{ return makeArray(impl).ref(i); }
const LLSD& LLSD::operator[](Integer i) const
										{ return safe(impl).ref(i); }

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
	size_t len = out_string.length();
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

LLSD::map_t& LLSD::map() { return makeMap(impl).map(); }
const LLSD::map_t& LLSD::map() const { return safe(impl).map(); }

LLSD::map_iterator			LLSD::beginMap()		{ return map().begin(); }
LLSD::map_iterator			LLSD::endMap()			{ return map().end(); }
LLSD::map_const_iterator	LLSD::beginMap() const	{ return map().cbegin(); }
LLSD::map_const_iterator	LLSD::endMap() const	{ return map().cend(); }

std::vector<LLSD>& LLSD::array() { return makeArray(impl).array(); }
const std::vector<LLSD>& LLSD::array() const { return safe(impl).array(); }

LLSD::array_iterator		LLSD::beginArray()		{ return array().begin(); }
LLSD::array_iterator		LLSD::endArray()		{ return array().end(); }
LLSD::array_const_iterator	LLSD::beginArray() const{ return array().cbegin(); }
LLSD::array_const_iterator	LLSD::endArray() const	{ return array().cend(); }

LLSD::reverse_array_iterator	LLSD::rbeginArray()		{ return array().rbegin(); }
LLSD::reverse_array_iterator	LLSD::rendArray()		{ return array().rend(); }

namespace llsd
{

U32 allocationCount()								{ return LLSD::Impl::sAllocationCount; }
U32 outstandingCount()								{ return LLSD::Impl::sOutstandingCount; }

// Diagnostic dump of contents in an LLSD object
void dumpStats(const LLSD& llsd)					{ LLSD::Impl::getImpl(llsd).dumpStats(); }

} // namespace llsd

// static
std::string		LLSD::typeString(Type type)
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
