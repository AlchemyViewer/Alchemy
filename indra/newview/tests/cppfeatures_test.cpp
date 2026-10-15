/**
 * @file cppfeatures_test
 * @author Vir
 * @date 2021-03
 * @brief cpp features
 *
 * $LicenseInfo:firstyear=2021&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2021, Linden Research, Inc.
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

// Tests related to newer C++ features, for verifying support across compilers and platforms

#include "linden_common.h"
#include <catch2/catch_test_macros.hpp>

#include <type_traits>

namespace
{

// bracket initializers
// Can initialize containers or values using curly brackets
TEST_CASE("LLCPPFeatures: bracket initializers set values and containers", "[LLCPPFeatures]")
{
    S32 explicit_val{3};
    CHECK(explicit_val==3);

    S32 default_val{};
    CHECK(default_val==0);

    std::vector<S32> fibs{1,1,2,3,5};
    CHECK(fibs[4]==5);
}

// auto
//
// https://en.cppreference.com/w/cpp/language/auto
//
// Can use auto in place of a more complex type specification, if the compiler can infer the type
TEST_CASE("LLCPPFeatures: auto deduces an element and an iterator type", "[LLCPPFeatures]")
{
    std::vector<S32> numbers{3,6,9};

    // auto element
    auto& aval = numbers[1];
    CHECK(aval==6);

    // auto iterator (non-const)
    auto it = numbers.rbegin();
    *it += 1;
    S32 val = *it;
    CHECK(val==10);
}

// range for
//
// https://en.cppreference.com/w/cpp/language/range-for
//
// Can iterate over containers without explicit iterator
TEST_CASE("LLCPPFeatures: range for walks a container or a C array", "[LLCPPFeatures]")
{

    // Traditional iterator for with container
    //
    // Problems:
    // * Have to create a new variable for the iterator, which is unrelated to the problem you're trying to solve.
    // * Redundant and somewhat fragile. Have to make sure begin() and end() are both from the right container.
    std::vector<S32> numbers{3,6,9};
    for (auto it = numbers.begin(); it != numbers.end(); ++it)
    {
        auto& n = *it;
        n *= 2;
    }
    CHECK(numbers[2]==18);

    // Range for with container
    //
    // Under the hood, this is doing the same thing as the traditional
    // for loop above. Still uses begin() and end() but you don't have
    // to access them directly.
    std::vector<S32> numbersb{3,6,9};
    for (auto& n: numbersb)
    {
        n *= 2;
    }
    CHECK(numbersb[2]==18);

    // Range for over a C-style array.
    //
    // This is handy because the language determines the range automatically.
    // Getting this right manually is a little trickier.
    S32 pows[] = {1,2,4,8,16};
    S32 sum{};
    for (const auto& v: pows)
    {
        sum += v;
    }
    CHECK(sum==31);
}

// override specifier
//
// https://en.cppreference.com/w/cpp/language/override
//
// Specify that a particular class function is an override of a virtual function.
// Benefits:
// * Makes code somewhat easier to read by showing intent.
// * Prevents mistakes where you think something is an override but it doesn't actually match the declaration in the parent class.
// Drawbacks:
// * Some compilers require that any class using override must use it consistently for all functions.
//   This makes switching a class to use override a lot more work.


class Foo
{
public:
    // Not happy itself, so that a happy answer through a Foo is Bar's.
    virtual bool is_happy() const { return false; }
};

class Bar: public Foo
{
public:
    bool is_happy() const override { return true; }
    // Override would fail: non-const declaration doesn't match parent
    // bool is_happy() override { return true; }
    // Override would fail: wrong name
    // bool is_happx() override { return true; }
};


TEST_CASE("LLCPPFeatures: an override specifier marks a virtual override", "[LLCPPFeatures]")
{
    // Asked through its base, which only an override answers.
    Bar b;
    const Foo& as_foo = b;
    CHECK(as_foo.is_happy());
}

// final
//
// https://en.cppreference.com/w/cpp/language/final: "Specifies that a
// virtual function cannot be overridden in a derived class or that a
// class cannot be inherited from."


class Vehicle
{
public:
    // No wheels itself, so that wheels through a Vehicle are the final
    // override's.
    virtual bool has_wheels() const { return false; }
};

class WheeledVehicle: public Vehicle
{
public:
    virtual bool has_wheels() const final override { return true; }
};

class Bicycle: public WheeledVehicle
{
public:
    // Error: can't override final version in WheeledVehicle
    // virtual bool has_wheels() override const { return true; }
};


TEST_CASE("LLCPPFeatures: a class derives from one with a final override", "[LLCPPFeatures]")
{
    // Asked through the root, a Bicycle answers with the final override it
    // inherits.
    Bicycle bi;
    const Vehicle& as_vehicle = bi;
    CHECK(as_vehicle.has_wheels());
}

// deleted function declaration
//
// https://en.cppreference.com/w/cpp/language/function#Deleted_functions
//
// Typical case: copy constructor doesn't make sense for a particular class, so you want to make
// sure the no one tries to copy-construct an instance of the class, and that the
// compiler won't generate a copy constructor for  you automatically.
// Traditional fix is to declare a
// copy constructor but never implement it, giving you a link-time error if anyone tries to use it.
// Now you can explicitly declare a function to be deleted, which has at least two advantages over
// the old way:
// * Makes the intention clear
// * Creates an error sooner, at compile time


class DoNotCopy
{
public:
    DoNotCopy() {}
    DoNotCopy(const DoNotCopy& ref) = delete;
};


// Default construction is checked by compiling; the copy is asked of the
// type, since a copy written out would not compile.
TEST_CASE("LLCPPFeatures: a deleted copy constructor forbids copies but not default construction", "[LLCPPFeatures]")
{
    DoNotCopy nc; // OK, default constructor
    //DoNotCopy nc2(nc); // No, can't copy
    //DoNotCopy nc3 = nc; // No, this also calls copy constructor (even though it looks like an assignment)
    CHECK_FALSE(std::is_copy_constructible_v<DoNotCopy>);
}

// defaulted function declaration
//
// https://en.cppreference.com/w/cpp/language/function#Function_definition
//
// What about the complementary case to the deleted function declaration, where you want a copy constructor
// and are happy with the default implementation the compiler will make (memberwise copy).
// Now you can explicitly declare that too.
// Usage: I guess it makes the intent clearer, but otherwise not obviously useful.

class DefaultCopyOK
{
public:
    DefaultCopyOK(): mVal(123) {}
    DefaultCopyOK(const DefaultCopyOK&) = default;
    S32 val() const { return mVal; }
private:
    S32 mVal;
};


TEST_CASE("LLCPPFeatures: a defaulted copy constructor copies the members", "[LLCPPFeatures]")
{
    DefaultCopyOK d; // OK
    DefaultCopyOK d2(d); // OK
    DefaultCopyOK d3 = d; // OK
    CHECK(d.val()==123);
    CHECK(d.val()==d2.val());
    CHECK(d.val()==d3.val());
}

// initialize class members inline
//
// https://en.cppreference.com/w/cpp/language/data_members#Member_initialization
//
// Default class member values can be set where they are declared, using either brackets or =

// It is preferred to skip creating a constructor if all the work can be done by inline initialization:
// http://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines.html#c45-dont-define-a-default-constructor-that-only-initializes-data-members-use-in-class-member-initializers-instead
//

class InitInline
{
public:
    S32 mFoo{10};
};

class InitInlineWithConstructor
{
public:
    // Here mFoo is not specified, so you will get the default value of 10.
    // mBar is specified, so 25 will override the default value.
    InitInlineWithConstructor():
        mBar(25)
    {}

    // Default values set using two different styles, same effect.
    S32 mFoo{10};
    S32 mBar = 20;
};


TEST_CASE("LLCPPFeatures: inline member initializers give defaults a constructor overrides", "[LLCPPFeatures]")
{
    InitInline ii;
    CHECK(ii.mFoo==10);

    InitInlineWithConstructor iici;
    CHECK(iici.mFoo==10);
    CHECK(iici.mBar==25);
}

// constexpr
//
// https://en.cppreference.com/w/cpp/language/constexpr
//
// Various things can be computed at compile time, and flagged as constexpr.

constexpr S32 compute2() { return 2; }

constexpr S32 ce_factorial(S32 n)
{
    if (n<=0)
    {
        return 1;
    }
    else
    {
        return n*ce_factorial(n-1);
    }
}


TEST_CASE("LLCPPFeatures: constexpr functions compute values", "[LLCPPFeatures]")
{
    S32 val = compute2();
    CHECK(val==2);

    // Compile-time factorial. You used to need complex templates to do something this useless.
    S32 fac5 = ce_factorial(5);
    CHECK(fac5==120);
}

// static assert
//
// https://en.cppreference.com/w/cpp/language/static_assert
//
// You can add asserts to be checked at compile time. The thing to be checked must be a constexpr.
// There are two forms:
// * static_assert(expr); (since C++17)
// * static_assert(expr, message);
//
// The case's checks are the compiler's: it passes by compiling, and then by returning.

TEST_CASE("LLCPPFeatures: static_assert checks a constexpr at compile time", "[LLCPPFeatures]")
{
    static_assert(ce_factorial(6)==720);
    static_assert(ce_factorial(6)==720, "bad factorial");
}

// type aliases
//
// https://en.cppreference.com/w/cpp/language/type_alias
//
// You can use the "using" statement to create simpler templates that
// are aliases for more complex ones. "Template typedef"

// This makes stringmap<T> an alias for std::map<std::string, T>
template<typename T>
using stringmap = std::map<std::string, T>;

TEST_CASE("LLCPPFeatures: a template type alias names a map type", "[LLCPPFeatures]")
{
    stringmap<S32> name_counts{ {"alice", 3}, {"bob", 2} };
    CHECK(name_counts["bob"]==2);
}

// Other possibilities:

// nullptr

// class enums

// std::unique_ptr and make_unique

// std::shared_ptr and make_shared

// lambdas

// perfect forwarding

// variadic templates

// std::thread

// std::mutex

// thread_local

// rvalue reference &&

// move semantics

// std::move

// string_view

} // anonymous namespace
