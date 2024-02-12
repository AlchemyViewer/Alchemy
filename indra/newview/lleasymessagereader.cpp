/**
 * @file lleasymessagereader.cpp
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
#include "lleasymessagereader.h"
#include "llsdserialize.h"

LLEasyMessageReader::LLEasyMessageReader()
    : mTemplateMessageReader(gMessageSystem->mMessageNumbers)
{
}

LLEasyMessageReader::~LLEasyMessageReader()
{
}

//we might want the sequenceid of the packet, which we can't get from
//a messagetemplate pointer, allow for passing in a U32 to be replaced
//with the sequenceid
LLMessageTemplate* LLEasyMessageReader::decodeTemplateMessage(U8 *data, S32 data_len, const LLHost& from_host)
{
	U32 fake_id = 0;
	return decodeTemplateMessage(data, data_len, from_host, fake_id);
}

LLMessageTemplate* LLEasyMessageReader::decodeTemplateMessage(U8 *data, S32 data_len, const LLHost& from_host, U32& sequence_id)
{
	if(data_len > MAX_BUFFER_SIZE)
	{
		LL_ERRS("") << "Tried to decode a template message of size " << data_len << ", greater than MAX_BUFFER_SIZE!" << LL_ENDL;
		return nullptr;
	}

	U8* decodep = (&mRecvBuffer[0]);
	memcpy(decodep, data, data_len);

	// note if packet acks are appended.
	if ((decodep[0] & LL_ACK_FLAG))
	{
		S32 acks = decodep[--data_len];
		if (data_len >= ((S32)(acks * sizeof(TPACKETID) + LL_MINIMUM_VALID_PACKET_SIZE)))
		{
			data_len -= acks * sizeof(TPACKETID);
		}
		else
		{
			// mal-formed packet. ignore it and continue with
			// the next one
			LL_WARNS("Messaging") << "Malformed packet received. Packet size "
				<< data_len << " with invalid no. of acks " << acks
				<< LL_ENDL;
			return nullptr;
		}
	}

	LLMessageTemplate* message_template = nullptr;

	gMessageSystem->zeroCodeExpand(&decodep, &data_len);

	if(data_len >= LL_MINIMUM_VALID_PACKET_SIZE)
	{
		U32 net_sec_id;
		memcpy(&net_sec_id, &decodep[1], sizeof(net_sec_id));
		sequence_id = ntohl(net_sec_id);

		mTemplateMessageReader.clearMessage();
		if(mTemplateMessageReader.validateMessage(decodep, data_len, from_host, TRUE))
		{
			if(mTemplateMessageReader.decodeData(decodep, from_host, TRUE))
			{
				message_template = mTemplateMessageReader.getTemplate();
			}
		}
	}
	return message_template;
}

S32 LLEasyMessageReader::getNumberOfBlocks(const char *blockname)
{
	return mTemplateMessageReader.getNumberOfBlocks(blockname);
}

std::string LLEasyMessageReader::var2Str(const char* block_name, S32 block_num, LLMessageVariable* variable, BOOL &returned_hex, BOOL summary_mode)
{
	const char* var_name = variable->getName();
	e_message_variable_type var_type = variable->getType();

	returned_hex = FALSE;
	std::stringstream stream;

	U32 valueU32;
	U16 valueU16;
	LLVector3 valueVector3;
	LLVector3d valueVector3d;
	LLVector4 valueVector4;
	LLQuaternion valueQuaternion;
	LLUUID valueLLUUID;

	switch(var_type)
	{
	case MVT_U8:
		U8 valueU8;
		mTemplateMessageReader.getU8(block_name, var_name, valueU8, block_num);
		stream << U32(valueU8);
		break;
	case MVT_U16:
		mTemplateMessageReader.getU16(block_name, var_name, valueU16, block_num);
		stream << valueU16;
		break;
	case MVT_U32:
		mTemplateMessageReader.getU32(block_name, var_name, valueU32, block_num);
		stream << valueU32;
		break;
	case MVT_U64:
		U64 valueU64;
		mTemplateMessageReader.getU64(block_name, var_name, valueU64, block_num);
		stream << valueU64;
		break;
	case MVT_S8:
		S8 valueS8;
		mTemplateMessageReader.getS8(block_name, var_name, valueS8, block_num);
		stream << S32(valueS8);
		break;
	case MVT_S16:
		S16 valueS16;
		mTemplateMessageReader.getS16(block_name, var_name, valueS16, block_num);
		stream << valueS16;
		break;
	case MVT_S32:
		S32 valueS32;
		mTemplateMessageReader.getS32(block_name, var_name, valueS32, block_num);
		stream << valueS32;
		break;
	/*case MVT_S64:
		S64 valueS64;
		mTemplateMessageReader.getS64(block_name, var_name, valueS64, block_num);
		stream << valueS64;
		break;*/
	case MVT_F32:
		F32 valueF32;
		mTemplateMessageReader.getF32(block_name, var_name, valueF32, block_num);
		stream << valueF32;
		break;
	case MVT_F64:
		F64 valueF64;
		mTemplateMessageReader.getF64(block_name, var_name, valueF64, block_num);
		stream << valueF64;
		break;
	case MVT_LLVector3:
		mTemplateMessageReader.getVector3(block_name, var_name, valueVector3, block_num);
		//stream << valueVector3;
		stream << "<" << valueVector3.mV[0] << ", " << valueVector3.mV[1] << ", " << valueVector3.mV[2] << ">";
		break;
	case MVT_LLVector3d:
		mTemplateMessageReader.getVector3d(block_name, var_name, valueVector3d, block_num);
		//stream << valueVector3d;
		stream << "<" << valueVector3d.mdV[0] << ", " << valueVector3d.mdV[1] << ", " << valueVector3d.mdV[2] << ">";
		break;
	case MVT_LLVector4:
		mTemplateMessageReader.getVector4(block_name, var_name, valueVector4, block_num);
		//stream << valueVector4;
		stream << "<" << valueVector4.mV[0] << ", " << valueVector4.mV[1] << ", " << valueVector4.mV[2] << ", " << valueVector4.mV[3] << ">";
		break;
	case MVT_LLQuaternion:
		mTemplateMessageReader.getQuat(block_name, var_name, valueQuaternion, block_num);
		//stream << valueQuaternion;
		stream << "<" << valueQuaternion.mQ[0] << ", " << valueQuaternion.mQ[1] << ", " << valueQuaternion.mQ[2] << ", " << valueQuaternion.mQ[3] << ">";
		break;
	case MVT_LLUUID:
		mTemplateMessageReader.getUUID(block_name, var_name, valueLLUUID, block_num);
		stream << valueLLUUID;
		break;
	case MVT_BOOL:
		BOOL valueBOOL;
		mTemplateMessageReader.getBOOL(block_name, var_name, valueBOOL, block_num);
		stream << valueBOOL;
		break;
	case MVT_IP_ADDR:
		mTemplateMessageReader.getIPAddr(block_name, var_name, valueU32, block_num);
		stream << LLHost(valueU32, 0).getIPString();
		break;
	case MVT_IP_PORT:
		mTemplateMessageReader.getIPPort(block_name, var_name, valueU16, block_num);
		stream << valueU16;
		break;
	case MVT_VARIABLE:
	case MVT_FIXED:
	default:
		S32 size = mTemplateMessageReader.getSize(block_name, block_num, var_name);
		if(size)
		{
			auto value = std::make_unique<char[]>(size + 1);
			mTemplateMessageReader.getBinaryData(block_name, var_name, value.get(), size, block_num);
			value[size] = '\0';
			S32 readable = 0;
			S32 unreadable = 0;
			S32 end = (summary_mode && (size > 64)) ? 64 : size;
			for(S32 i = 0; i < end; i++)
			{
				if(!value[i])
				{
					if(i != (end - 1))
					{ // don't want null terminator hiding data
						unreadable = S32_MAX;
						break;
					}
				}
				else if(value[i] < 0x20 || value[i] >= 0x7F)
				{
					if(summary_mode)
						unreadable++;
					else
					{ // never want any wrong characters outside of summary mode
						unreadable = S32_MAX;
						break;
					}
				}
				else readable++;
			}
			if(readable >= unreadable)
			{
				if(summary_mode && (size > 64))
				{
					for(S32 i = 60; i < 63; i++)
						value[i] = '.';
					value[63] = '\0';
				}
				stream << value.get();
			}
			else
			{
				returned_hex = TRUE;
				S32 end = (summary_mode && (size > 8)) ? 8 : size;
				for(S32 i = 0; i < end; i++)
					//stream << std::uppercase << std::hex << U32(value[i]) << " ";
					stream << llformat("%02X ", (U8)value[i]);
				if(summary_mode && (size > 8))
					stream << " ... ";
			}
		}
		break;
	}

	return stream.str();
}
