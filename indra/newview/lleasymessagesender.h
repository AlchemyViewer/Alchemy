/**
 * @file lleasymessagesender.h
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

#ifndef LL_EASYMESSAGE_SENDER_H
#define LL_EASYMESSAGE_SENDER_H

#include <llmessagetemplate.h>

class LLEasyMessageSender
{
public:
	bool sendMessage(const LLHost& region_host, const std::string& str_message );
	bool sendLLUDPMessage(const LLHost& region_host, const std::string& str_message );
	bool sendHTTPMessage(const LLHost& region_host, const std::string& str_message ) const;

#ifdef ALCH_ADDON_API
	bool addonSendRawMessage(const std::string& region_host, const std::string& str_message);
	bool addonSendRawMessage(const std::string& str_message);
	bool addonSendMessage(const std::string& region_host);
	bool addonSendMessage();
	void addonNewMessage(const std::string& message_name, const std::string& direction, bool include_agent_boilerplate=false);
	void addonClearMessage();
	void addonAddBlock(const std::string& blockname);
	void addonAddField(const std::string& name, const std::string& value);
	void addonAddHexField(const std::string& name, const std::string& value);
#endif // ALCH_ADDON_API
	
private:

	BOOL addField(e_message_variable_type var_type, const char* var_name, std::string input, BOOL hex) const;

	//a key->value pair in a message
	struct parts_var
	{
		std::string name;
		std::string value;
		BOOL hex;
		e_message_variable_type var_type;
	};

	//a block containing key->value pairs
	struct parts_block
	{
		std::string name;
		std::vector<parts_var> vars;
	};

	std::string mMessageBuffer;

	static void printError(const std::string& error);
	static std::string mvtstr(e_message_variable_type var_type);

	std::vector<std::string> split(const std::string& input, const std::string& separator) const;
};
#endif
