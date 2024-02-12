/**
 * @file lleasymessagereader.h
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
 **/

#ifndef EASY_MESSAGE_READER_H
#define EASY_MESSAGE_READER_H

#include "llmessagelog.h"
#include "message.h"
#include "lltemplatemessagereader.h"
#include "llmessagetemplate.h"

class LLViewerRegion;

class LLEasyMessageReader
{
public:
	LLEasyMessageReader();
	~LLEasyMessageReader();

	LLMessageTemplate* decodeTemplateMessage(U8* data, S32 data_len, const LLHost& from_host);
	LLMessageTemplate* decodeTemplateMessage(U8* data, S32 data_len, const LLHost& from_host, U32& sequence_id);

	S32 getNumberOfBlocks(const char *blockname);

	std::string var2Str(const char* block_name, S32 block_num, LLMessageVariable* variable, BOOL &returned_hex, BOOL summary_mode=FALSE);

private:
	LLTemplateMessageReader mTemplateMessageReader;
	U8	mRecvBuffer[MAX_BUFFER_SIZE];
};

#endif
