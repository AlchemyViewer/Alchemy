/**
 * @file lliohttpnode_tut.cpp
 * @date   May 2006
 * @brief HTTP server unit tests
 *
 * $LicenseInfo:firstyear=2006&license=viewerlgpl$
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

#include "linden_common.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "altestmatchers.h"
#include "llhttpnode.h"
#include "llsdhttpserver.h"

#include <memory>

namespace
{
    struct HTTPNodeTestData
    {
        LLHTTPNode mRoot;
        LLSD mContext;

        const LLSD& context() { return mContext; }

        std::string remainderPath()
        {
            std::ostringstream pathOutput;
            bool addSlash = false;

            LLSD& remainder = mContext[CONTEXT_REQUEST]["remainder"];
            for (LLSD::array_const_iterator i = remainder.beginArray();
                i != remainder.endArray();
                ++i)
            {
                if (addSlash) { pathOutput << '/'; }
                pathOutput << i->asString();
                addSlash = true;
            }

            return pathOutput.str();
        }

        void require_root_traversal(const std::string& path,
            const LLHTTPNode* expectedNode,
            const char* expectedRemainder)
        {
            mContext.clear();

            const LLHTTPNode* actualNode = mRoot.traverse(path, mContext);

            INFO("traverse " << path);
            REQUIRE(actualNode == expectedNode);
            REQUIRE(remainderPath() == expectedRemainder);
        }

        class Response : public LLHTTPNode::Response
        {
        public:
            static LLPointer<Response> create() {return new Response();}

            LLSD mResult;

            void result(const LLSD& result) { mResult = result; }
            void status(S32 code, const std::string& message) { }
            void extendedResult(S32 code, const std::string& message, const LLSD& headers) { }
            void extendedResult(S32 code, const LLSD& result, const LLSD& headers) { }

        private:
            Response() {;} // Must be accessed through LLPointer.
        };

        typedef LLPointer<Response> ResponsePtr;

        LLSD get(const std::string& path)
        {
            mContext.clear();
            const LLHTTPNode* node = mRoot.traverse(path, mContext);
            CAPTURE(path);
            REQUIRE(node != NULL);

            ResponsePtr response = Response::create();
            node->get(LLHTTPNode::ResponsePtr(response), mContext);
            return response->mResult;
        }

        LLSD post(const std::string& path, const LLSD& input)
        {
            mContext.clear();
            const LLHTTPNode* node = mRoot.traverse(path, mContext);
            CAPTURE(path);
            REQUIRE(node != NULL);

            ResponsePtr response = Response::create();
            node->post(LLHTTPNode::ResponsePtr(response), mContext, input);
            return response->mResult;
        }

        void require_member_string(const std::string& name,
            const LLSD& actualMap, const std::string& member,
            const std::string& expectedValue)
        {
            CAPTURE(name, member);
            REQUIRE(actualMap[member].asString() == expectedValue);
        }


        void require_in_array(const LLSD& actualArray,
            const std::string& expectedValue)
        {
            LLSD::array_const_iterator i = actualArray.beginArray();
            LLSD::array_const_iterator end = actualArray.endArray();

            for (; i != end; ++i)
            {
                std::string path = i->asString();

                if (path == expectedValue)
                {
                    return;
                }
            }

            FAIL("didn't find " + expectedValue);
        }

    };


    TEST_CASE_METHOD(HTTPNodeTestData, "http node: a lone root matches only an empty path and is its own root", "[http_node]")
    {
        // traversal of the lone node

        require_root_traversal("", &mRoot, "");
        require_root_traversal("/", &mRoot, "");
        require_root_traversal("foo", NULL, "foo");
        require_root_traversal("foo/bar", NULL, "foo/bar");

        CHECK(mRoot.rootNode() == &mRoot);
    }

    TEST_CASE_METHOD(HTTPNodeTestData, "http node: a child node matches its name with or without slashes", "[http_node]")
    {
        // simple traversal of a single node

        LLHTTPNode* helloNode = new LLHTTPNode;
        mRoot.addNode("hello", helloNode);

        require_root_traversal("hello", helloNode, "");
        require_root_traversal("/hello", helloNode, "");
        require_root_traversal("hello/", helloNode, "");
        require_root_traversal("/hello/", helloNode, "");

        require_root_traversal("hello/there", NULL, "there");

        CHECK(helloNode->rootNode() == &mRoot);
    }

    TEST_CASE_METHOD(HTTPNodeTestData, "http node: traversal finds each leaf of a multi-branched tree", "[http_node]")
    {
        // traversal of mutli-branched tree

        LLHTTPNode* greekNode = new LLHTTPNode;
        LLHTTPNode* alphaNode = new LLHTTPNode;
        LLHTTPNode* betaNode = new LLHTTPNode;
        LLHTTPNode* gammaNode = new LLHTTPNode;

        greekNode->addNode("alpha", alphaNode);
        greekNode->addNode("beta", betaNode);
        greekNode->addNode("gamma", gammaNode);
        mRoot.addNode("greek", greekNode);

        LLHTTPNode* hebrewNode = new LLHTTPNode;
        LLHTTPNode* alephNode = new LLHTTPNode;

        hebrewNode->addNode("aleph", alephNode);
        mRoot.addNode("hebrew", hebrewNode);

        require_root_traversal("greek/alpha", alphaNode, "");
        require_root_traversal("greek/beta", betaNode, "");
        require_root_traversal("greek/delta", NULL, "delta");
        require_root_traversal("greek/gamma", gammaNode, "");
        require_root_traversal("hebrew/aleph", alephNode, "");

        CHECK(greekNode->rootNode() == &mRoot);
        CHECK(alphaNode->rootNode() == &mRoot);
        CHECK(betaNode->rootNode() == &mRoot);
        CHECK(gammaNode->rootNode() == &mRoot);
        CHECK(hebrewNode->rootNode() == &mRoot);
        CHECK(alephNode->rootNode() == &mRoot);
    }

    TEST_CASE_METHOD(HTTPNodeTestData, "http node: addNode creates parents and keeps an existing node", "[http_node]")
    {
        // automatic creation of parent nodes and not overriding existing nodes

        LLHTTPNode* alphaNode = new LLHTTPNode;
        LLHTTPNode* betaNode = new LLHTTPNode;
        LLHTTPNode* gammaNode = new LLHTTPNode;
        // addNode refuses a node for a path that has one, and the refused node stays its caller's.
        auto gamma2Node = std::make_unique<LLHTTPNode>();

        mRoot.addNode("greek/alpha", alphaNode);
        mRoot.addNode("greek/beta", betaNode);

        CHECK(mRoot.addNode("greek/gamma", gammaNode));
        const bool taken = mRoot.addNode("greek/gamma", gamma2Node.get());
        CHECK_FALSE(taken);
        if (taken)
        {
            (void)gamma2Node.release(); // mRoot took it, and deletes it
        }

        LLHTTPNode* alephNode = new LLHTTPNode;

        mRoot.addNode("hebrew/aleph", alephNode);

        require_root_traversal("greek/alpha", alphaNode, "");
        require_root_traversal("greek/beta", betaNode, "");
        require_root_traversal("greek/delta", NULL, "delta");
        require_root_traversal("greek/gamma", gammaNode, "");
        require_root_traversal("hebrew/aleph", alephNode, "");

        CHECK(alphaNode->rootNode() == &mRoot);
        CHECK(betaNode->rootNode() == &mRoot);
        CHECK(gammaNode->rootNode() == &mRoot);
        CHECK(alephNode->rootNode() == &mRoot);
    }

    class IntegerNode : public LLHTTPNode
    {
    public:
        virtual void get(ResponsePtr response, const LLSD& context) const
        {
            int n = context["extra"]["value"];

            LLSD info;
            info["value"] = n;
            info["positive"] = n > 0;
            info["zero"] = n == 0;
            info["negative"] = n < 0;

            response->result(info);
        }

        virtual bool validate(const std::string& name, LLSD& context) const
        {
            int n;
            std::istringstream i_stream(name);
            i_stream >> n;

            if (i_stream.fail()  ||  i_stream.get() != EOF)
            {
                return false;
            }

            context["extra"]["value"] = n;
            return true;
        }
    };

    class SquareNode : public LLHTTPNode
    {
    public:
        virtual void get(ResponsePtr response, const LLSD& context) const
        {
            int n = context["extra"]["value"];
            response->result(n*n);
        }
    };

    TEST_CASE_METHOD(HTTPNodeTestData, "http node: a wildcard node validates its path element and stores it", "[http_node]")
    {
        // wildcard nodes

        LLHTTPNode* miscNode = new LLHTTPNode;
        LLHTTPNode* iNode = new IntegerNode;
        LLHTTPNode* sqNode = new SquareNode;

        mRoot.addNode("test/misc", miscNode);
        mRoot.addNode("test/<int>", iNode);
        mRoot.addNode("test/<int>/square", sqNode);

        require_root_traversal("test/42", iNode, "");
        CHECK(context()["extra"]["value"].asInteger() == 42);

        require_root_traversal("test/bob", NULL, "bob");
        CHECK(context()["extra"]["value"].isUndefined());

        require_root_traversal("test/3/square", sqNode, "");
        ResponsePtr response = Response::create();
        sqNode->get(LLHTTPNode::ResponsePtr(response), context());
        CHECK(response->mResult.asInteger() == 9);
    }

    class AlphaNode : public LLHTTPNode
    {
    public:
        virtual bool handles(const LLSD& remainder, LLSD& context) const
        {
            LLSD::array_const_iterator i = remainder.beginArray();
            LLSD::array_const_iterator end = remainder.endArray();

            for (; i != end; ++i)
            {
                std::string s = i->asString();
                if (s.empty() || s[0] != 'a')
                {
                    return false;
                }
            }

            return true;
        }
    };

    TEST_CASE_METHOD(HTTPNodeTestData, "http node: a node that handles remainders takes the paths it accepts", "[http_node]")
    {
        // nodes that handle remainders

        LLHTTPNode* miscNode = new LLHTTPNode;
        LLHTTPNode* aNode = new AlphaNode;
        LLHTTPNode* zNode = new LLHTTPNode;

        mRoot.addNode("test/misc", miscNode);
        mRoot.addNode("test/alpha", aNode);
        mRoot.addNode("test/alpha/zebra", zNode);

        require_root_traversal("test/alpha", aNode, "");
        require_root_traversal("test/alpha/abe", aNode, "abe");
        require_root_traversal("test/alpha/abe/amy", aNode, "abe/amy");
        require_root_traversal("test/alpha/abe/bea", NULL, "abe/bea");
        require_root_traversal("test/alpha/bob", NULL, "bob");
        require_root_traversal("test/alpha/zebra", zNode, "");
    }

    TEST_CASE_METHOD(HTTPNodeTestData, "http node: standard services answer web/hello and web/echo", "[http_node]")
    {
        // test auto registration

        LLHTTPStandardServices::useServices();
        LLHTTPRegistrar::buildAllServices(mRoot);

        {
            LLSD result = get("web/hello");
            CHECK(result.asString() == "hello");
        }
        {
            LLSD stuff = 3.14159;
            LLSD result = post("web/echo", stuff);
            CHECK_THAT(result, ll_test::LLSDEquals(stuff));
        }
    }

    // Counts the nodes alive, to show that one buildAllServices refuses is deleted.
    struct CountedNode : public LLHTTPNode
    {
        CountedNode() { ++sLive; }
        ~CountedNode() override { --sLive; }

        static int sLive;
    };
    int CountedNode::sLive = 0;

    TEST_CASE_METHOD(HTTPNodeTestData, "http node: buildAllServices deletes a node whose path is taken", "[http_node]")
    {
        static LLHTTPRegistration<CountedNode> counted("/test/registrar/counted");

        LLHTTPRegistrar::buildAllServices(mRoot);
        const int live = CountedNode::sLive;
        CHECK(live > 0);

        // Every path is taken now, so every node built is refused.
        LLHTTPRegistrar::buildAllServices(mRoot);
        CHECK(CountedNode::sLive == live);
    }

    TEST_CASE_METHOD(HTTPNodeTestData, "http node: web/server/api lists the registered paths", "[http_node]")
    {
        // test introspection

        LLHTTPRegistrar::buildAllServices(mRoot);

        mRoot.addNode("test/misc", new LLHTTPNode);
        mRoot.addNode("test/<int>", new IntegerNode);
        mRoot.addNode("test/<int>/square", new SquareNode);

        const LLSD result = get("web/server/api");

        REQUIRE(result.isArray());
        REQUIRE(result.size() >= 2);

        require_in_array(result, "web/echo");
        require_in_array(result, "web/hello");
        require_in_array(result, "test/misc");
        require_in_array(result, "test/<int>");
        require_in_array(result, "test/<int>/square");
    }

    TEST_CASE_METHOD(HTTPNodeTestData, "http node: web/server/api describes web/hello and web/echo", "[http_node]")
    {
        // test introspection details

        LLHTTPRegistrar::buildAllServices(mRoot);

        const LLSD helloDetails = get("web/server/api/web/hello");

        CHECK_THAT(helloDetails["description"].asString(), Catch::Matchers::ContainsSubstring("hello"));
        CHECK(helloDetails["api"][0].asString() == std::string("GET"));
        require_member_string("hello", helloDetails, "output", "\"hello\"");
        CHECK_THAT(helloDetails["__file__"].asString(), Catch::Matchers::ContainsSubstring("llsdhttpserver.cpp"));
        CHECK(helloDetails["__line__"].isInteger());


        const LLSD echoDetails = get("web/server/api/web/echo");

        CHECK_THAT(echoDetails["description"].asString(), Catch::Matchers::ContainsSubstring("echo"));
        CHECK(echoDetails["api"][0].asString() == std::string("POST"));
        require_member_string("echo", echoDetails, "input", "<any>");
        require_member_string("echo", echoDetails, "output", "<the input>");
        CHECK_THAT(echoDetails["__file__"].asString(), Catch::Matchers::ContainsSubstring("llsdhttpserver.cpp"));
        CHECK(echoDetails["__line__"].isInteger());
    }
}
