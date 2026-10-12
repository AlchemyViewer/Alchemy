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
        LLMessageTemplateParserTestData() : mMessage("unset message")
        {
        }

        ~LLMessageTemplateParserTestData()
        {
        }

        void ensure_next(LLTemplateTokenizer & tokens,
                         std::string value,
                         U32 line)
        {
            std::string next = tokens.next();
            UNSCOPED_INFO(mMessage + " token matches");
            REQUIRE(next == value);
            UNSCOPED_INFO(mMessage + " line matches");
            REQUIRE(tokens.line() == line);
        }

        char * prehash(const char * name)
        {
            return LLMessageStringTable::getInstance()->getString(name);
        }

        void ensure_block_attributes(std::string identifier,
                                     const LLMessageTemplate * message,
                                     const char * name,
                                     EMsgBlockType type,
                                     S32 number,
                                     S32 total_size)
        {
            const LLMessageBlock * block = message->getBlock(prehash(name));
            identifier = identifier + ":" + message->mName + ":" + name + " block";
            UNSCOPED_INFO(identifier + " exists");
            REQUIRE(block != NULL);
            UNSCOPED_INFO(identifier + " name");
            REQUIRE(block->mName == prehash(name));
            UNSCOPED_INFO(identifier + " type");
            REQUIRE(block->mType == type);
            UNSCOPED_INFO(identifier + " number");
            REQUIRE(block->mNumber == number);
            UNSCOPED_INFO(identifier + " total size");
            REQUIRE(block->mTotalSize == total_size);
        }

        void ensure_variable_attributes(std::string identifier,
                                        const LLMessageBlock * block,
                                        const char * name,
                                        EMsgVariableType type,
                                        S32 size)
        {
            const LLMessageVariable * var = block->getVariable(prehash(name));
            identifier = identifier + ":" + block->mName + ":" + name + " variable";
            UNSCOPED_INFO(identifier + " exists");
            REQUIRE(var != NULL);
            UNSCOPED_INFO(identifier + " name");
            REQUIRE(var->getName() == prehash(name));
            UNSCOPED_INFO(identifier + " type");
            REQUIRE(var->getType() == type);
            UNSCOPED_INFO(identifier + " size");
            REQUIRE(var->getSize() == size);
        }

        std::string mMessage;

    };


    TEST_CASE_METHOD(LLMessageTemplateParserTestData, "LLMessageTemplateParser: tokenizer next walks the tokens and counts lines", "[LLMessageTemplateParser]")
        // tests tokenizer constructor and next methods
    {
        mMessage = "test method 1 walkthrough";
        LLTemplateTokenizer tokens("first line\nnext\t line\n\nfourth");
        ensure_next(tokens, "first", 1);
        ensure_next(tokens, "line", 1);
        ensure_next(tokens, "next", 2);
        ensure_next(tokens, "line", 2);
        ensure_next(tokens, "fourth", 4);

        tokens = LLTemplateTokenizer("\n\t{ \t   Test1 Fixed \n 523 }\n\n");
        CHECK(tokens.want("{"));
        ensure_next(tokens, "Test1", 2);
        ensure_next(tokens, "Fixed", 2);
        ensure_next(tokens, "523", 3);
        CHECK(tokens.want("}"));

        tokens = LLTemplateTokenizer("first line\nnext\t line\n\nfourth");
        CHECK(tokens.want("first"));
        ensure_next(tokens, "line", 1);
        ensure_next(tokens, "next", 2);
        ensure_next(tokens, "line", 2);
        CHECK(tokens.want("fourth"));
    }

    TEST_CASE_METHOD(LLMessageTemplateParserTestData, "LLMessageTemplateParser: tokenizer want consumes only a matching token", "[LLMessageTemplateParser]")
        // tests tokenizer want method
    {
        // *NOTE: order matters
        LLTemplateTokenizer tokens("first line\nnext\t line\n\nfourth");
        UNSCOPED_INFO("wants first token");
        CHECK(tokens.want("first") == true);
        UNSCOPED_INFO("doesn't want blar token");
        CHECK(tokens.want("blar") == false);
        UNSCOPED_INFO("wants line token");
        CHECK(tokens.want("line") == true);
    }

    TEST_CASE_METHOD(LLMessageTemplateParserTestData, "LLMessageTemplateParser: tokenizer is at EOF only once wantEOF steps past the end", "[LLMessageTemplateParser]")
        // tests tokenizer eof methods
    {
        LLTemplateTokenizer tokens("single\n\n");
        UNSCOPED_INFO("is not at eof at beginning");
        CHECK(tokens.atEOF() == false);
        UNSCOPED_INFO("doesn't want eof");
        CHECK(tokens.wantEOF() == false);
        UNSCOPED_INFO("wants the first token just to consume it");
        CHECK(tokens.want("single") == true);
        UNSCOPED_INFO("is not at eof in middle");
        CHECK(tokens.atEOF() == false);
        UNSCOPED_INFO("wants eof");
        CHECK(tokens.wantEOF() == true);
        UNSCOPED_INFO("is at eof at end");
        CHECK(tokens.atEOF() == true);
    }

    TEST_CASE_METHOD(LLMessageTemplateParserTestData, "LLMessageTemplateParser: parseVariable reads the name and type and size", "[LLMessageTemplateParser]")
        // tests variable parsing method
    {
        LLTemplateTokenizer tokens(std::string("{    Test0  \n\t\n   U32 \n\n }"));
        LLMessageVariable * var = LLTemplateParser::parseVariable(tokens);

        UNSCOPED_INFO("test0 var parsed");
        REQUIRE(var != 0);
        UNSCOPED_INFO("name of variable");
        CHECK(std::string(var->getName()) == std::string("Test0"));
        UNSCOPED_INFO("type of variable is U32");
        CHECK(var->getType() == MVT_U32);
        UNSCOPED_INFO("size of variable");
        CHECK(var->getSize() == 4);

        delete var;

        std::string message_string("\n\t{ \t   Test1 Fixed \n 523 }\n\n");
        tokens = LLTemplateTokenizer(message_string);
        var = LLTemplateParser::parseVariable(tokens);

        UNSCOPED_INFO("test1 var parsed");
        REQUIRE(var != 0);
        UNSCOPED_INFO("name of variable");
        CHECK(std::string(var->getName()) == std::string("Test1"));
        UNSCOPED_INFO("type of variable is Fixed");
        CHECK(var->getType() == MVT_FIXED);
        UNSCOPED_INFO("size of variable");
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

        UNSCOPED_INFO("blockA block parsed");
        REQUIRE(block != 0);
        UNSCOPED_INFO("name of block");
        CHECK(std::string(block->mName) == std::string("BlockA"));
        UNSCOPED_INFO("type of block is Single");
        CHECK(block->mType == MBT_SINGLE);
        UNSCOPED_INFO("total size of block");
        CHECK(block->mTotalSize == 4);
        UNSCOPED_INFO("number of block defaults to 1");
        CHECK(block->mNumber == 1);
        UNSCOPED_INFO("variable type of VarX is F32");
        CHECK(block->getVariableType(prehash("VarX")) == MVT_F32);
        UNSCOPED_INFO("variable size of VarX");
        CHECK(block->getVariableSize(prehash("VarX")) == 4);

        delete block;

        tokens = LLTemplateTokenizer("{ Stuff Variable { Id LLUUID } }");
        block = LLTemplateParser::parseBlock(tokens);

        UNSCOPED_INFO("stuff block parsed");
        REQUIRE(block != 0);
        UNSCOPED_INFO("name of block");
        CHECK(std::string(block->mName) == std::string("Stuff"));
        UNSCOPED_INFO("type of block is Multiple");
        CHECK(block->mType == MBT_VARIABLE);
        UNSCOPED_INFO("total size of block");
        CHECK(block->mTotalSize == 16);
        UNSCOPED_INFO("number of block defaults to 1");
        CHECK(block->mNumber == 1);
        UNSCOPED_INFO("variable type of Id is LLUUID");
        CHECK(block->getVariableType(prehash("Id")) == MVT_LLUUID);
        UNSCOPED_INFO("variable size of Id");
        CHECK(block->getVariableSize(prehash("Id")) == 16);

        delete block;

        tokens = LLTemplateTokenizer("{ Stuff2 Multiple 45 { Shid LLVector3d } }");
        block = LLTemplateParser::parseBlock(tokens);

        UNSCOPED_INFO("stuff2 block parsed");
        REQUIRE(block != 0);
        UNSCOPED_INFO("name of block");
        CHECK(std::string(block->mName) == std::string("Stuff2"));
        UNSCOPED_INFO("type of block is Multiple");
        CHECK(block->mType == MBT_MULTIPLE);
        UNSCOPED_INFO("total size of block");
        CHECK(block->mTotalSize == 24);
        UNSCOPED_INFO("number of blocks");
        CHECK(block->mNumber == 45);
        UNSCOPED_INFO("variable type of Shid is Vector3d");
        CHECK(block->getVariableType(prehash("Shid")) == MVT_LLVector3d);
        UNSCOPED_INFO("variable size of Shid");
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

        UNSCOPED_INFO("simple message parsed");
        REQUIRE(message != 0);
        UNSCOPED_INFO("name of message");
        CHECK(std::string(message->mName) == std::string("TestMessage"));
        UNSCOPED_INFO("frequency is Low");
        CHECK(message->mFrequency == MFT_LOW);
        UNSCOPED_INFO("trust is untrusted");
        CHECK(message->mTrust == MT_NOTRUST);
        UNSCOPED_INFO("message number");
        CHECK(message->mMessageNumber == (U32)((255 << 24) | (255 << 16) | 1));
        UNSCOPED_INFO("message encoding is zerocoded");
        CHECK(message->mEncoding == ME_ZEROCODED);
        UNSCOPED_INFO("message deprecation is notdeprecated");
        CHECK(message->mDeprecation == MD_NOTDEPRECATED);

        LLMessageBlock * block = message->getBlock(prehash("NonexistantBlock"));
        UNSCOPED_INFO("Nonexistant block does not exist");
        REQUIRE(block == 0);

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

        UNSCOPED_INFO("deprecated message parsed");
        REQUIRE(message != 0);
        UNSCOPED_INFO("name of message");
        CHECK(std::string(message->mName) == std::string("TestMessageDeprecated"));
        UNSCOPED_INFO("frequency is High");
        CHECK(message->mFrequency == MFT_HIGH);
        UNSCOPED_INFO("trust is trusted");
        CHECK(message->mTrust == MT_TRUST);
        UNSCOPED_INFO("message number");
        CHECK(message->mMessageNumber == (U32)34);
        UNSCOPED_INFO("message encoding is unencoded");
        CHECK(message->mEncoding == ME_UNENCODED);
        UNSCOPED_INFO("message deprecation is deprecated");
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

        UNSCOPED_INFO("RezMultipleAttachmentsFromInv message parsed");
        REQUIRE(message != 0);
        UNSCOPED_INFO("name of message");
        REQUIRE(message->mName == prehash("RezMultipleAttachmentsFromInv"));
        UNSCOPED_INFO("frequency is low");
        CHECK(message->mFrequency == MFT_LOW);
        UNSCOPED_INFO("trust is not trusted");
        CHECK(message->mTrust == MT_NOTRUST);
        UNSCOPED_INFO("message number");
        CHECK(message->mMessageNumber == (U32)((255 << 24) | (255 << 16) | 452));
        UNSCOPED_INFO("message encoding is zerocoded");
        CHECK(message->mEncoding == ME_ZEROCODED);

        ensure_block_attributes(
            "RMAFI", message, "AgentData", MBT_SINGLE, 1, 16+16);
        LLMessageBlock * block = message->getBlock(prehash("AgentData"));
        ensure_variable_attributes("RMAFI",
                                   block, "AgentID", MVT_LLUUID, 16);
        ensure_variable_attributes("RMAFI",
                                   block, "SessionID", MVT_LLUUID, 16);

        ensure_block_attributes(
            "RMAFI", message, "HeaderData", MBT_SINGLE, 1, 16+1+1);
        block = message->getBlock(prehash("HeaderData"));
        ensure_variable_attributes(
            "RMAFI", block, "CompoundMsgID", MVT_LLUUID, 16);
        ensure_variable_attributes(
            "RMAFI", block, "TotalObjects", MVT_U8, 1);
        ensure_variable_attributes(
            "RMAFI", block, "FirstDetachAll", MVT_BOOL, 1);


        ensure_block_attributes(
            "RMAFI", message, "ObjectData", MBT_VARIABLE, 1, -1);
        block = message->getBlock(prehash("ObjectData"));
        ensure_variable_attributes("RMAFI", block, "ItemID", MVT_LLUUID, 16);
        ensure_variable_attributes("RMAFI", block, "OwnerID", MVT_LLUUID, 16);
        ensure_variable_attributes("RMAFI", block, "AttachmentPt", MVT_U8, 1);
        ensure_variable_attributes("RMAFI", block, "ItemFlags", MVT_U32, 4);
        ensure_variable_attributes("RMAFI", block, "GroupMask", MVT_U32, 4);
        ensure_variable_attributes("RMAFI", block, "EveryoneMask", MVT_U32, 4);
        ensure_variable_attributes("RMAFI", block, "NextOwnerMask", MVT_U32, 4);
        ensure_variable_attributes("RMAFI", block, "Name", MVT_VARIABLE, 1);
        ensure_variable_attributes("RMAFI", block, "Description", MVT_VARIABLE, 1);

        delete message;
    }



}
