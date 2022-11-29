/**
 * @file lleasymessagesender.cpp
 *
 * $LicenseInfo:firstyear=2015&license=viewerlgpl$
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
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "lleasymessagesender.h"
#include "llmessagetemplate.h"
#include "lltemplatemessagebuilder.h"
#include "llviewerregion.h"
#include "llagent.h"
#include "llnotificationsutil.h"
#include "llworld.h"
#include "bufferstream.h"

#include <boost/algorithm/string/join.hpp>

//we don't use static methods to prepare for when this class will use its own message builder.

bool LLEasyMessageSender::sendMessage(const LLHost& region_host, const std::string& str_message)
{
	std::string msg_verb = str_message.substr(0, str_message.find(' '));
	LLStringUtil::toUpper(msg_verb);
	if(msg_verb == "OUT" || msg_verb == "IN")
	{
		return sendLLUDPMessage(region_host, str_message);
	}
	else if(httpVerbAsMethod(msg_verb) != HTTP_INVALID)
	{
		return sendHTTPMessage(region_host, str_message);
	}
	else
	{
		printError(llformat("Unrecognized verb '%s'", msg_verb.c_str()));
	}
	return false;
}

bool LLEasyMessageSender::sendLLUDPMessage(const LLHost& region_host, const std::string& str_message)
{
	std::vector<std::string> lines = split(str_message, "\n");
	if(lines.empty())
	{
		LLNotificationsUtil::add("GenericAlert", LLSD().with("MESSAGE", "Not enough information :O"));
		return false;
	}
	std::vector<std::string> tokens = split(lines[0], " ");
	if(tokens.empty())
	{
		LLNotificationsUtil::add("GenericAlert", LLSD().with("MESSAGE", "Not enough information :O"));
		return false;
	}
	std::string dir_str = tokens[0];
	LLStringUtil::toUpper(dir_str);

	BOOL outgoing;
	if(dir_str == "OUT") {
		outgoing = TRUE;
	} else if(dir_str == "IN") {
		outgoing = FALSE;
	} else {
		LLNotificationsUtil::add("GenericAlert", LLSD().with("MESSAGE", "Expected direction 'in' or 'out'"));
		return false;
	}
	// Message
	std::string message = "Invalid";
	if(tokens.size() > 1)
	{
		if(tokens.size() > 2)
		{
			LLNotificationsUtil::add("GenericAlert", LLSD().with("MESSAGE","Unexpected extra stuff at the top"));
			return false;
		}
		message = tokens[1];
		LLStringUtil::trim(message);
	}
	// Body
	std::vector<parts_block> parts;
	if(lines.size() > 1)
	{
		std::vector<std::string>::iterator line_end = lines.end();
		std::vector<std::string>::iterator line_iter = lines.begin();
		++line_iter;
		std::string current_block = LLStringUtil::null;
		S32 current_block_index = -1;
		for( ; line_iter != line_end; ++line_iter)
		{
			std::string line = (*line_iter);
			LLStringUtil::trim(line);

			//skip empty lines
			if(!line.length())
				continue;

			//check if this line is the start of a new block
			if(line.substr(0, 1) == "[" && line.substr(line.size() - 1, 1) == "]")
			{
				current_block = line.substr(1, line.length() - 2);
				LLStringUtil::trim(current_block);
				++current_block_index;
				parts_block pb;
				pb.name = current_block;
				parts.push_back(pb);
			}
			//should be a key->value pair
			else
			{
				if(current_block.empty())
				{
					LLNotificationsUtil::add("GenericAlert", LLSD().with("MESSAGE", "Got a field before the start of a block"));
					return false;
				}

				size_t eqpos = line.find('=');
				if(eqpos == line.npos)
				{
					LLNotificationsUtil::add("GenericAlert", LLSD().with("MESSAGE", "Missing an equal sign"));
					return false;
				}

				std::string field = line.substr(0, eqpos);
				LLStringUtil::trim(field);
				if(!field.length())
				{
					LLNotificationsUtil::add("GenericAlert", LLSD().with("MESSAGE", "Missing name of field"));
					return false;
				}

				std::string value = line.substr(eqpos + 1);
				LLStringUtil::trim(value);
				parts_var pv;

				//check if this is a hex value
				if(value.substr(0, 1) == "|")
				{
					pv.hex = TRUE;
					value = value.substr(1);
					LLStringUtil::trim(value);
				}
				else
					pv.hex = FALSE;

				pv.name = std::move(field);
				pv.value = std::move(value);
				parts[current_block_index].vars.push_back(pv);
			}
		}
	}

	//Make sure everything's kosher with the message we built

	//check if the message type is one that we know about
	std::map<const char *, LLMessageTemplate*>::iterator template_iter=
		gMessageSystem->mMessageTemplates.find( LLMessageStringTable::getInstance()->getString(message.c_str()) );
	if (template_iter == gMessageSystem->mMessageTemplates.end())
	{
		LLNotificationsUtil::add("GenericAlert", LLSD().with(
			"MESSAGE", llformat("Don't know how to build a '%s' message", message.c_str())));
		return false;
	}

	LLMessageTemplate* temp = (*template_iter).second;

	std::vector<parts_block>::iterator parts_end = parts.end();
	std::vector<parts_block>::iterator parts_iter = parts.begin();
	LLMessageTemplate::message_block_map_t::iterator blocks_end = temp->mMemberBlocks.end();

	for (LLMessageTemplate::message_block_map_t::iterator blocks_iter = temp->mMemberBlocks.begin();
		 blocks_iter != blocks_end; )
	{
		LLMessageBlock* block = (*blocks_iter);
		const char* block_name = block->mName;

		//are we at the end of the block or does this block belongs at this spot in the message?
		if(parts_iter == parts_end || (*parts_iter).name != block_name)
		{
			//did the block end too early?
			if(block->mType != MBT_VARIABLE)
			{
				LLNotificationsUtil::add("GenericAlert", LLSD().with(
					"MESSAGE",llformat("Expected '%s' block", block_name)));
				return false;
			}

			//skip to the next block
			++blocks_iter;
			continue;
		}

		std::vector<parts_var>::iterator part_var_end = (*parts_iter).vars.end();
		std::vector<parts_var>::iterator part_var_iter = (*parts_iter).vars.begin();
		LLMessageBlock::message_variable_map_t::iterator var_end = block->mMemberVariables.end();
		for (LLMessageBlock::message_variable_map_t::iterator var_iter = block->mMemberVariables.begin();
			 var_iter != var_end; ++var_iter)
		{
			LLMessageVariable* variable = (*var_iter);
			const char* var_name = variable->getName();

			//are there less keypairs in this block than there should be?
			if(part_var_iter == part_var_end || (*part_var_iter).name != var_name)
			{
				LLNotificationsUtil::add("GenericAlert", LLSD().with(
					"MESSAGE", llformat("Expected '%s' field under '%s' block", var_name, block_name)));
				return false;
			}

			//keep the type of data that this value is supposed to contain for later
			(*part_var_iter).var_type = variable->getType();

			++part_var_iter;
		}

		//there were more keypairs in the block than there should have been
		if(part_var_iter != part_var_end)
		{
			LLNotificationsUtil::add("GenericAlert", LLSD().with(
				"MESSAGE", llformat("Unexpected field(s) at end of '%s' block", block_name)));
			return false;
		}

		++parts_iter;

		//if this block isn't going to repeat, change to the next block
		if(!((block->mType != MBT_SINGLE) && (parts_iter != parts_end) && ((*parts_iter).name == block_name)))
			++blocks_iter;

	}

	//there were more blocks specified in the message than there should have been
	if(parts_iter != parts_end)
	{
		LLNotificationsUtil::add("GenericAlert", LLSD().with(
			"MESSAGE", llformat("Unexpected block(s) at end: %s", (*parts_iter).name.c_str())));
		return false;
	}

	// Build and send
	gMessageSystem->newMessage( message.c_str() );
	for(parts_iter = parts.begin(); parts_iter != parts_end; ++parts_iter)
	{
		const char* block_name = (*parts_iter).name.c_str();
		gMessageSystem->nextBlock(block_name);
		std::vector<parts_var>::iterator part_var_end = (*parts_iter).vars.end();
		for(std::vector<parts_var>::iterator part_var_iter = (*parts_iter).vars.begin();
			part_var_iter != part_var_end; ++part_var_iter)
		{
			parts_var pv = (*part_var_iter);
			if(!addField(pv.var_type, pv.name.c_str(), pv.value, pv.hex))
			{
				LLNotificationsUtil::add("GenericAlert", LLSD().with(
					"MESSAGE", llformat("Error adding the provided data for %s '%s' to '%s' block", 
						mvtstr(pv.var_type).c_str(), pv.name.c_str(), block_name)));
				gMessageSystem->clearMessage();
				return false;
			}
		}
	}


	if (outgoing) 
	{
		return (gMessageSystem->sendMessage(region_host) > 0);
	}
    else
	{
		U8 builtMessageBuffer[MAX_BUFFER_SIZE];

		S32 message_size = gMessageSystem->mTemplateMessageBuilder->buildMessage(builtMessageBuffer, MAX_BUFFER_SIZE, 0);
		gMessageSystem->clearMessage();
        LockMessageChecker lmc(gMessageSystem);
		return lmc.checkMessages(0, true, builtMessageBuffer, region_host, message_size);
	}
}

bool LLEasyMessageSender::sendHTTPMessage(const LLHost& region_host, const std::string& str_message) const
{
	size_t header_start = str_message.find('\n');
    if (header_start == std::string::npos)
    {
        header_start = str_message.size();
    }
	std::string proto_line = str_message.substr(0, header_start);
	std::vector<std::string> proto_tokens = split(proto_line, " ");
	if(proto_tokens.size() != 2)
	{
		printError("Protocol line should be [VERB] [URL]");
		return false;
	}
	std::string http_verb = proto_tokens[0];
	LLStringUtil::toUpper(http_verb);
	EHTTPMethod http_method = httpVerbAsMethod(http_verb);
	if(http_method == HTTP_INVALID)
	{
		printError(llformat("Invalid HTTP verb '%s'", http_verb.c_str()));
		return false;
	}
	std::string target = proto_tokens[1];

	// This isn't a fully-qualified URL
	if(target.find(':') == std::string::npos)
	{
		LLViewerRegion* region = LLWorld::getInstance()->getRegion(region_host);
		if(!region)
		{
			printError("No region to do CAPS lookup on");
			return false;
		}

		std::vector<std::string> split_url = split(target, "/");
		std::string cap_url = region->getCapability(split_url[0]);
		if(cap_url.empty())
		{
			printError(llformat("Invalid CAP name: %s", split_url[0].c_str()));
			return false;
		}
		split_url[0] = cap_url;
		target = boost::algorithm::join(split_url, "/");
	}

    auto headers = std::make_shared<LLCore::HttpHeaders>();
    auto body = LLCore::BufferArray::ptr_t(new LLCore::BufferArray());
    LLCore::BufferArrayStream bas(body.get());

	// TODO: This does way too many string copies. The path's not very hot
	// but it's still gross.
	// Start by adding any headers
	if(header_start < str_message.size())
	{
		static const std::string BODY_SEPARATOR("\n\n");
		size_t header_end = str_message.find(BODY_SEPARATOR);
		if(header_end == std::string::npos)
			header_end = str_message.size();
		if(header_start != header_end)
		{
			auto header_section = str_message.substr(header_start, header_end - header_start);
			auto header_vec = split(header_section, "\n");
            for (const auto& iter : header_vec)
			{
				auto header_tokens = split(iter, ":");
				if(header_tokens.size() != 2)
				{
                    printError(std::string("Malformed Header: ").append(iter));
					return false;
				}

				std::string header_name = header_tokens[0];
				std::string header_val = header_tokens[1];
				LLStringUtil::trim(header_name);
				if(!header_val.empty() && header_val[0] == ' ')
					header_val = header_val.substr(1);

                headers->append(header_name, header_val);
			}
		}

		// Now add the body
		size_t body_start = header_end + BODY_SEPARATOR.size();
		if(body_start < str_message.size())
		{
            bas.write(str_message.c_str() + body_start, str_message.size());
		}
	}

	//LLHTTPClient::builderRequest(http_method, target, body, body_len, header_map);
	return true;
}

// Unused right now, but for the future!
#ifdef ALCH_ADDON_API
//wrapper so we can send whole message-builder format messages
bool LLEasyMessageSender::addonSendRawMessage(const std::string& region_host, const std::string& str_message)
{
	addonClearMessage();

	LLHost proper_region_host = LLHost(region_host);

	//check that this was a valid host
	if(proper_region_host.isOk())
		return sendMessage(proper_region_host, str_message);

	return false;
}

bool LLEasyMessageSender::addonSendRawMessage(const std::string& str_message)
{
	return addonSendRawMessage(gAgent.getRegionHost().getString(), str_message);
}

//buffered message builder methods
bool LLEasyMessageSender::addonSendMessage(const std::string& region_host)
{
	return addonSendRawMessage(region_host, mMessageBuffer);
}

bool LLEasyMessageSender::addonSendMessage()
{
	return addonSendRawMessage(gAgent.getRegionHost().getString(), mMessageBuffer);
}

void LLEasyMessageSender::addonNewMessage(const std::string& message_name, const std::string& direction, bool include_agent_boilerplate)
{
	//clear out any message that may be in the buffer
	addonClearMessage();

	mMessageBuffer = direction + " " + message_name + "\n";

	//include the agentdata block with our agentid and sessionid automagically
	if(include_agent_boilerplate)
		mMessageBuffer += "[AgentData]\nAgentID = $AgentID\nSessionID = $SessionID\n";
}

void LLEasyMessageSender::addonClearMessage()
{
	mMessageBuffer = LLStringUtil::null;
}

void LLEasyMessageSender::addonAddBlock(const std::string& blockname)
{
	mMessageBuffer += "[" + blockname + "]\n";
}

void LLEasyMessageSender::addonAddField(const std::string& name, const std::string& value)
{
	mMessageBuffer += name + " = " + value + "\n";
}

void LLEasyMessageSender::addonAddHexField(const std::string& name, const std::string& value)
{
	mMessageBuffer += name + " =| " + value + "\n";
}
#endif // ALCH_ADDON_API

BOOL LLEasyMessageSender::addField(e_message_variable_type var_type, const char* var_name, std::string input, BOOL hex) const
{
	LLStringUtil::trim(input);
	if(input.length() < 1 && var_type != MVT_VARIABLE)
		return FALSE;
	U8 valueU8;
	U16 valueU16;
	U32 valueU32;
	U64 valueU64;
	S8 valueS8;
	S16 valueS16;
	S32 valueS32;
	// S64 valueS64;
	F32 valueF32;
	F64 valueF64;
	LLVector3 valueVector3;
	LLVector3d valueVector3d;
	LLVector4 valueVector4;
	LLQuaternion valueQuaternion;
	LLUUID valueLLUUID;
	BOOL valueBOOL;
	std::string input_lower = input;
	LLStringUtil::toLower(input_lower);

	if(input_lower == "$agentid")
		input = gAgent.getID().asString();
	else if(input_lower == "$sessionid")
		input = gAgent.getSessionID().asString();
	else if(input_lower == "$uuid")
	{
		LLUUID id;
		id.generate();
		input = id.asString();
	}
	else if(input_lower == "$circuitcode")
	{
		std::stringstream temp_stream;
		temp_stream << gMessageSystem->mOurCircuitCode;
		input = temp_stream.str();
	}
	else if(input_lower == "$regionhandle")
	{
		std::stringstream temp_stream;
		temp_stream << (gAgent.getRegion() ? gAgent.getRegion()->getHandle() : 0);
		input = temp_stream.str();
	}
	else if(input_lower == "$position" || input_lower == "$pos")
	{
		std::stringstream temp_stream;
		valueVector3 = gAgent.getPositionAgent();
		temp_stream << "<" << valueVector3[0] << ", " << valueVector3[1] << ", " << valueVector3[2] << ">";
		input = temp_stream.str();
	}

	//convert from a text representation of hex to binary
	if(hex)
	{
		if(var_type != MVT_VARIABLE && var_type != MVT_FIXED)
			return FALSE;

		int         len  = input_lower.length();
		const char* cstr = input_lower.c_str();
		std::string new_input;
		BOOL        nibble = FALSE;
		char        byte   = 0;

		for(int i = 0; i < len; i++)
		{
			char c = cstr[i];
			if(c >= 0x30 && c <= 0x39)
				c -= 0x30;
			else if(c >= 0x61 && c <= 0x66)
				c -= 0x57;
			else if(c != 0x20)
				return FALSE;
			else
				continue;
			if(!nibble)
				byte = c << 4;
			else
				new_input.push_back(byte | c);
			nibble = !nibble;
		}

		if(nibble)
			return FALSE;

		input = std::move(new_input);
	}

	std::stringstream stream(input);
	std::vector<std::string> tokens;

	switch(var_type)
	{
	case MVT_U8:
		if(input.substr(0, 1) == "-")
			return FALSE;
		if((stream >> valueU32).fail())
			return FALSE;
		valueU8 = (U8)valueU32;
		gMessageSystem->addU8(var_name, valueU8);
		return TRUE;
		break;
	case MVT_U16:
		if(input.substr(0, 1) == "-")
			return FALSE;
		if((stream >> valueU16).fail())
			return FALSE;
		gMessageSystem->addU16(var_name, valueU16);
		return TRUE;
		break;
	case MVT_U32:
		if(input.substr(0, 1) == "-")
			return FALSE;
		if((stream >> valueU32).fail())
			return FALSE;
		gMessageSystem->addU32(var_name, valueU32);
		return TRUE;
		break;
	case MVT_U64:
		if(input.substr(0, 1) == "-")
			return FALSE;
		if((stream >> valueU64).fail())
			return FALSE;
		gMessageSystem->addU64(var_name, valueU64);
		return TRUE;
		break;
	case MVT_S8:
		if((stream >> valueS8).fail())
			return FALSE;
		gMessageSystem->addS8(var_name, valueS8);
		return TRUE;
		break;
	case MVT_S16:
		if((stream >> valueS16).fail())
			return FALSE;
		gMessageSystem->addS16(var_name, valueS16);
		return TRUE;
		break;
	case MVT_S32:
		if((stream >> valueS32).fail())
			return FALSE;
		gMessageSystem->addS32(var_name, valueS32);
		return TRUE;
		break;
	/*
	case MVT_S64:
		if((stream >> valueS64).fail())
			return FALSE;
		gMessageSystem->addS64(var_name, valueS64);
		return TRUE;
		break;
	*/
	case MVT_F32:
		if((stream >> valueF32).fail())
			return FALSE;
		gMessageSystem->addF32(var_name, valueF32);
		return TRUE;
		break;
	case MVT_F64:
		if((stream >> valueF64).fail())
			return FALSE;
		gMessageSystem->addF64(var_name, valueF64);
		return TRUE;
		break;
	case MVT_LLVector3:
		LLStringUtil::trim(input);
		if(input.substr(0, 1) != "<" || input.substr(input.length() - 1, 1) != ">")
			return FALSE;
		tokens = split(input.substr(1, input.length() - 2), ",");
		if(tokens.size() != 3)
			return FALSE;
		for(int i = 0; i < 3; i++)
		{
			stream.clear();
			stream.str(tokens[i]);
			if((stream >> valueF32).fail())
				return FALSE;
			valueVector3.mV[i] = valueF32;
		}
		gMessageSystem->addVector3(var_name, valueVector3);
		return TRUE;
		break;
	case MVT_LLVector3d:
		LLStringUtil::trim(input);
		if(input.substr(0, 1) != "<" || input.substr(input.length() - 1, 1) != ">")
			return FALSE;
		tokens = split(input.substr(1, input.length() - 2), ",");
		if(tokens.size() != 3)
			return FALSE;
		for(int i = 0; i < 3; i++)
		{
			stream.clear();
			stream.str(tokens[i]);
			if((stream >> valueF64).fail())
				return FALSE;
			valueVector3d.mdV[i] = valueF64;
		}
		gMessageSystem->addVector3d(var_name, valueVector3d);
		return TRUE;
		break;
	case MVT_LLVector4:
		LLStringUtil::trim(input);
		if(input.substr(0, 1) != "<" || input.substr(input.length() - 1, 1) != ">")
			return FALSE;
		tokens = split(input.substr(1, input.length() - 2), ",");
		if(tokens.size() != 4)
			return FALSE;
		for(int i = 0; i < 4; i++)
		{
			stream.clear();
			stream.str(tokens[i]);
			if((stream >> valueF32).fail())
				return FALSE;
			valueVector4.mV[i] = valueF32;
		}
		gMessageSystem->addVector4(var_name, valueVector4);
		return TRUE;
		break;
	case MVT_LLQuaternion:
		LLStringUtil::trim(input);
		if(input.substr(0, 1) != "<" || input.substr(input.length() - 1, 1) != ">")
			return FALSE;
		tokens = split(input.substr(1, input.length() - 2), ",");
		if(tokens.size() == 3)
		{
			for(int i = 0; i < 3; i++)
			{
				stream.clear();
				stream.str(tokens[i]);
				if((stream >> valueF32).fail())
					return FALSE;
				valueVector3.mV[i] = valueF32;
			}
			valueQuaternion.unpackFromVector3(valueVector3);
		}
		else if(tokens.size() == 4)
		{
			for(int i = 0; i < 4; i++)
			{
				stream.clear();
				stream.str(tokens[i]);
				if((stream >> valueF32).fail())
					return FALSE;
				valueQuaternion.mQ[i] = valueF32;
			}
		}
		else
			return FALSE;

		gMessageSystem->addQuat(var_name, valueQuaternion);
		return TRUE;
		break;
	case MVT_LLUUID:
		if((stream >> valueLLUUID).fail())
			return FALSE;
		gMessageSystem->addUUID(var_name, valueLLUUID);
		return TRUE;
		break;
	case MVT_BOOL:
		if(input_lower == "true")
			valueBOOL = TRUE;
		else if(input_lower == "false")
			valueBOOL = FALSE;
		else if((stream >> valueBOOL).fail())
			return FALSE;
		gMessageSystem->addBOOL(var_name, valueBOOL);
		//gMessageSystem->addU8(var_name, (U8)valueBOOL);
		return TRUE;
		break;
	case MVT_IP_ADDR:
		if((stream >> valueU32).fail())
			return FALSE;
		gMessageSystem->addIPAddr(var_name, valueU32);
		return TRUE;
		break;
	case MVT_IP_PORT:
		if((stream >> valueU16).fail())
			return FALSE;
		gMessageSystem->addIPPort(var_name, valueU16);
		return TRUE;
		break;
	case MVT_VARIABLE:
		if(!hex)
		{
			//input is null terminated .size does not include the null byte
			gMessageSystem->addBinaryData(var_name, input.c_str(), input.size() + 1);
		}
		else
		{
			gMessageSystem->addBinaryData(var_name, input.c_str(), input.size());
		}
		return TRUE;
		break;
	case MVT_FIXED:
		gMessageSystem->addBinaryData(var_name, input.c_str(), input.size());
		return TRUE;
		break;
	default:
		break;
	}
	return FALSE;
}

/* static */ void LLEasyMessageSender::printError(const std::string& error)
{
	LLSD args;
	args["MESSAGE"] = error;
	LLNotificationsUtil::add("GenericAlert", args);
}

//convert a message variable type to it's string representation
/* static */ std::string LLEasyMessageSender::mvtstr(e_message_variable_type var_type)
{
	switch(var_type)
	{
	case MVT_U8:
		return "U8";
		break;
	case MVT_U16:
		return "U16";
		break;
	case MVT_U32:
		return "U32";
		break;
	case MVT_U64:
		return "U64";
		break;
	case MVT_S8:
		return "S8";
		break;
	case MVT_S16:
		return "S16";
		break;
	case MVT_S32:
		return "S32";
		break;
	case MVT_S64:
		return "S64";
		break;
	case MVT_F32:
		return "F32";
		break;
	case MVT_F64:
		return "F64";
		break;
	case MVT_LLVector3:
		return "LLVector3";
		break;
	case MVT_LLVector3d:
		return "LLVector3d";
		break;
	case MVT_LLVector4:
		return "LLVector4";
		break;
	case MVT_LLQuaternion:
		return "LLQuaternion";
		break;
	case MVT_LLUUID:
		return "LLUUID";
		break;
	case MVT_BOOL:
		return "BOOL";
		break;
	case MVT_IP_ADDR:
		return "IPADDR";
		break;
	case MVT_IP_PORT:
		return "IPPORT";
		break;
	case MVT_VARIABLE:
		return "Variable";
		break;
	case MVT_FIXED:
		return "Fixed";
		break;
	default:
		return "Missingno.";
		break;
	}
}

inline std::vector<std::string> LLEasyMessageSender::split(const std::string& input, const std::string& separator) const
{
	const S32 size = input.length();
	auto buffer = std::make_unique<char[]>(size + 1);
	strncpy(buffer.get(), input.c_str(), size);
	buffer[size] = '\0';
	std::vector<std::string> lines;
	char* result = strtok(buffer.get(), separator.c_str());
	while(result)
	{
		lines.push_back(result);
		result = strtok(nullptr, separator.c_str());
	}
	return lines;
}
