/**
 * @file llmessagetemplateparser_tut.cpp
 * @date April 2007
 * @brief LLMessageTemplateParser unit tests
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
#include "llmessagetemplateparser.h"
#include <catch2/catch_test_macros.hpp>

namespace
{
    struct LLMessageTemplateParserTestData {
        LLMessageTemplateParserTestData()
        {
        }

        ~LLMessageTemplateParserTestData()
        {
        }

        void require_next(LLTemplateTokenizer & tokens,
                          std::string value,
                          U32 line)
        {
            CAPTURE(value);
            std::string next = tokens.next();
            REQUIRE(next == value);
            REQUIRE(tokens.line() == line);
        }

        char * prehash(const char * name)
        {
            return LLMessageStringTable::getInstance()->getString(name);
        }

        void require_block_attributes(const std::string& identifier,
                                      const LLMessageTemplate * message,
                                      const char * name,
                                      EMsgBlockType type,
                                      S32 number,
                                      S32 total_size)
        {
            const LLMessageBlock * block = message->getBlock(prehash(name));
            INFO(identifier << ":" << message->mName << ":" << name << " block");
            REQUIRE(block != NULL);
            REQUIRE(block->mName == prehash(name));
            REQUIRE(block->mType == type);
            REQUIRE(block->mNumber == number);
            REQUIRE(block->mTotalSize == total_size);
        }

        void require_variable_attributes(const std::string& identifier,
                                         const LLMessageBlock * block,
                                         const char * name,
                                         EMsgVariableType type,
                                         S32 size)
        {
            const LLMessageVariable * var = block->getVariable(prehash(name));
            INFO(identifier << ":" << block->mName << ":" << name << " variable");
            REQUIRE(var != NULL);
            REQUIRE(var->getName() == prehash(name));
            REQUIRE(var->getType() == type);
            REQUIRE(var->getSize() == size);
        }

    };


    TEST_CASE_METHOD(LLMessageTemplateParserTestData, "LLMessageTemplateParser: tokenizer next walks the tokens and counts lines", "[LLMessageTemplateParser]")
        // tests tokenizer constructor and next methods
    {
        LLTemplateTokenizer tokens("first line\nnext\t line\n\nfourth");
        require_next(tokens, "first", 1);
        require_next(tokens, "line", 1);
        require_next(tokens, "next", 2);
        require_next(tokens, "line", 2);
        require_next(tokens, "fourth", 4);

        tokens = LLTemplateTokenizer("\n\t{ \t   Test1 Fixed \n 523 }\n\n");
        CHECK(tokens.want("{"));
        require_next(tokens, "Test1", 2);
        require_next(tokens, "Fixed", 2);
        require_next(tokens, "523", 3);
        CHECK(tokens.want("}"));

        tokens = LLTemplateTokenizer("first line\nnext\t line\n\nfourth");
        CHECK(tokens.want("first"));
        require_next(tokens, "line", 1);
        require_next(tokens, "next", 2);
        require_next(tokens, "line", 2);
        CHECK(tokens.want("fourth"));
    }

    TEST_CASE_METHOD(LLMessageTemplateParserTestData, "LLMessageTemplateParser: tokenizer want consumes only a matching token", "[LLMessageTemplateParser]")
        // tests tokenizer want method
    {
        // *NOTE: order matters
        LLTemplateTokenizer tokens("first line\nnext\t line\n\nfourth");
        CHECK(tokens.want("first"));
        CHECK_FALSE(tokens.want("blar"));
        CHECK(tokens.want("line"));
    }

    TEST_CASE_METHOD(LLMessageTemplateParserTestData, "LLMessageTemplateParser: tokenizer is at EOF only once wantEOF steps past the end", "[LLMessageTemplateParser]")
        // tests tokenizer eof methods
    {
        LLTemplateTokenizer tokens("single\n\n");
        UNSCOPED_INFO("at the start");
        CHECK_FALSE(tokens.atEOF());
        CHECK_FALSE(tokens.wantEOF());
        UNSCOPED_INFO("wants the first token just to consume it");
        CHECK(tokens.want("single"));
        UNSCOPED_INFO("on the last token");
        CHECK_FALSE(tokens.atEOF());
        CHECK(tokens.wantEOF());
        CHECK(tokens.atEOF());
    }

    TEST_CASE_METHOD(LLMessageTemplateParserTestData, "LLMessageTemplateParser: parseVariable reads the name and type and size", "[LLMessageTemplateParser]")
        // tests variable parsing method
    {
        LLTemplateTokenizer tokens(std::string("{    Test0  \n\t\n   U32 \n\n }"));
        LLMessageVariable * var = LLTemplateParser::parseVariable(tokens);

        REQUIRE(var != 0);
        CHECK(std::string(var->getName()) == std::string("Test0"));
        CHECK(var->getType() == MVT_U32);
        CHECK(var->getSize() == 4);

        delete var;

        std::string message_string("\n\t{ \t   Test1 Fixed \n 523 }\n\n");
        tokens = LLTemplateTokenizer(message_string);
        var = LLTemplateParser::parseVariable(tokens);

        REQUIRE(var != 0);
        CHECK(std::string(var->getName()) == std::string("Test1"));
        CHECK(var->getType() == MVT_FIXED);
        CHECK(var->getSize() == 523);

        delete var;

        // *NOTE: the parsers call LL_ERRS() on invalid input, so we can't really
        // test that  :-(
    }

    TEST_CASE_METHOD(LLMessageTemplateParserTestData, "LLMessageTemplateParser: parseBlock reads Single and Variable and Multiple blocks", "[LLMessageTemplateParser]")
        // tests block parsing method
    {
        LLTemplateTokenizer tokens("{ BlockA Single { VarX F32 } }");
        LLMessageBlock * block = LLTemplateParser::parseBlock(tokens);

        REQUIRE(block != 0);
        CHECK(std::string(block->mName) == std::string("BlockA"));
        CHECK(block->mType == MBT_SINGLE);
        CHECK(block->mTotalSize == 4);
        UNSCOPED_INFO("the number defaults to 1");
        CHECK(block->mNumber == 1);
        CHECK(block->getVariableType(prehash("VarX")) == MVT_F32);
        CHECK(block->getVariableSize(prehash("VarX")) == 4);

        delete block;

        tokens = LLTemplateTokenizer("{ Stuff Variable { Id LLUUID } }");
        block = LLTemplateParser::parseBlock(tokens);

        REQUIRE(block != 0);
        CHECK(std::string(block->mName) == std::string("Stuff"));
        CHECK(block->mType == MBT_VARIABLE);
        CHECK(block->mTotalSize == 16);
        UNSCOPED_INFO("the number defaults to 1");
        CHECK(block->mNumber == 1);
        CHECK(block->getVariableType(prehash("Id")) == MVT_LLUUID);
        CHECK(block->getVariableSize(prehash("Id")) == 16);

        delete block;

        tokens = LLTemplateTokenizer("{ Stuff2 Multiple 45 { Shid LLVector3d } }");
        block = LLTemplateParser::parseBlock(tokens);

        REQUIRE(block != 0);
        CHECK(std::string(block->mName) == std::string("Stuff2"));
        CHECK(block->mType == MBT_MULTIPLE);
        CHECK(block->mTotalSize == 24);
        CHECK(block->mNumber == 45);
        CHECK(block->getVariableType(prehash("Shid")) == MVT_LLVector3d);
        CHECK(block->getVariableSize(prehash("Shid")) == 24);

        delete block;
    }

    TEST_CASE_METHOD(LLMessageTemplateParserTestData, "LLMessageTemplateParser: parseMessage reads the header of a simple message", "[LLMessageTemplateParser]")
        // tests message parsing method on a simple message
    {
        std::string message_skel(
            "{\n"
            "TestMessage Low 1 NotTrusted Zerocoded\n"
            "// comment \n"
            "  {\n"
            "TestBlock1      Single\n"
            "      {   Test1       U32 }\n"
            "  }\n"
            "  {\n"
            "      NeighborBlock       Multiple        4\n"
            "      {   Test0       U32 }\n"
            "      {   Test1       U32 }\n"
            "      {   Test2       U32 }\n"
            "  }\n"
            "}");
        LLTemplateTokenizer tokens(message_skel);
        LLMessageTemplate * message = LLTemplateParser::parseMessage(tokens);

        REQUIRE(message != 0);
        CHECK(std::string(message->mName) == std::string("TestMessage"));
        CHECK(message->mFrequency == MFT_LOW);
        CHECK(message->mTrust == MT_NOTRUST);
        CHECK(message->mMessageNumber == (U32)((255 << 24) | (255 << 16) | 1));
        CHECK(message->mEncoding == ME_ZEROCODED);
        CHECK(message->mDeprecation == MD_NOTDEPRECATED);

        LLMessageBlock * block = message->getBlock(prehash("NonexistantBlock"));
        CHECK(block == 0);

        delete message;
    }

    TEST_CASE_METHOD(LLMessageTemplateParserTestData, "LLMessageTemplateParser: parseMessage reads a deprecated message", "[LLMessageTemplateParser]")
        // tests message parsing method on a deprecated message
    {
        std::string message_skel(
            "{\n"
            "TestMessageDeprecated High 34 Trusted Unencoded Deprecated\n"
            "  {\n"
            "TestBlock2      Single\n"
            "      {   Test2       S32 }\n"
            "  }\n"
            "}");
        LLTemplateTokenizer tokens(message_skel);
        LLMessageTemplate * message = LLTemplateParser::parseMessage(tokens);

        REQUIRE(message != 0);
        CHECK(std::string(message->mName) == std::string("TestMessageDeprecated"));
        CHECK(message->mFrequency == MFT_HIGH);
        CHECK(message->mTrust == MT_TRUST);
        CHECK(message->mMessageNumber == (U32)34);
        CHECK(message->mEncoding == ME_UNENCODED);
        CHECK(message->mDeprecation == MD_DEPRECATED);

        delete message;
    }

    TEST_CASE_METHOD(LLMessageTemplateParserTestData, "LLMessageTemplateParser: parseMessage reads RezMultipleAttachmentsFromInv in full", "[LLMessageTemplateParser]")
        // tests message parsing on RezMultipleAttachmentsFromInv, a possibly-faulty message
    {
        std::string message_skel(
            "{\n\
                RezMultipleAttachmentsFromInv Low 452 NotTrusted Zerocoded\n\
                {\n\
                    AgentData           Single\n\
                    {   AgentID         LLUUID  }\n\
                    {   SessionID       LLUUID  }\n\
                }   \n\
                {\n\
                    HeaderData          Single\n\
                    {   CompoundMsgID           LLUUID  }   // All messages a single \"compound msg\" must have the same id\n\
                    {   TotalObjects            U8  }\n\
                    {   FirstDetachAll          BOOL    }\n\
                }\n\
                {\n\
                    ObjectData          Variable        // 1 to 4 of these per packet\n\
                    {   ItemID                  LLUUID  }\n\
                    {   OwnerID                 LLUUID  }\n\
                    {   AttachmentPt            U8  }   // 0 for default\n\
                    {   ItemFlags               U32 }\n\
                    {   GroupMask               U32 }\n\
                    {   EveryoneMask            U32 }\n\
                    {   NextOwnerMask           U32 }\n\
                    {   Name                    Variable    1   }\n\
                    {   Description             Variable    1   }\n\
                }\n\
            }\n\
            ");
        LLTemplateTokenizer tokens(message_skel);
        LLMessageTemplate * message = LLTemplateParser::parseMessage(tokens);

        REQUIRE(message != 0);
        CHECK(message->mName == prehash("RezMultipleAttachmentsFromInv"));
        CHECK(message->mFrequency == MFT_LOW);
        CHECK(message->mTrust == MT_NOTRUST);
        CHECK(message->mMessageNumber == (U32)((255 << 24) | (255 << 16) | 452));
        CHECK(message->mEncoding == ME_ZEROCODED);

        require_block_attributes(
            "RMAFI", message, "AgentData", MBT_SINGLE, 1, 16+16);
        LLMessageBlock * block = message->getBlock(prehash("AgentData"));
        require_variable_attributes("RMAFI",
                                    block, "AgentID", MVT_LLUUID, 16);
        require_variable_attributes("RMAFI",
                                    block, "SessionID", MVT_LLUUID, 16);

        require_block_attributes(
            "RMAFI", message, "HeaderData", MBT_SINGLE, 1, 16+1+1);
        block = message->getBlock(prehash("HeaderData"));
        require_variable_attributes(
            "RMAFI", block, "CompoundMsgID", MVT_LLUUID, 16);
        require_variable_attributes(
            "RMAFI", block, "TotalObjects", MVT_U8, 1);
        require_variable_attributes(
            "RMAFI", block, "FirstDetachAll", MVT_BOOL, 1);


        require_block_attributes(
            "RMAFI", message, "ObjectData", MBT_VARIABLE, 1, -1);
        block = message->getBlock(prehash("ObjectData"));
        require_variable_attributes("RMAFI", block, "ItemID", MVT_LLUUID, 16);
        require_variable_attributes("RMAFI", block, "OwnerID", MVT_LLUUID, 16);
        require_variable_attributes("RMAFI", block, "AttachmentPt", MVT_U8, 1);
        require_variable_attributes("RMAFI", block, "ItemFlags", MVT_U32, 4);
        require_variable_attributes("RMAFI", block, "GroupMask", MVT_U32, 4);
        require_variable_attributes("RMAFI", block, "EveryoneMask", MVT_U32, 4);
        require_variable_attributes("RMAFI", block, "NextOwnerMask", MVT_U32, 4);
        require_variable_attributes("RMAFI", block, "Name", MVT_VARIABLE, 1);
        require_variable_attributes("RMAFI", block, "Description", MVT_VARIABLE, 1);

        delete message;
    }



}
