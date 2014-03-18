/*************************************************************************
 * Copyright (c) 2013 eProsima. All rights reserved.
 *
 * This copy of FastCdr is licensed to you under the terms described in the
 * EPROSIMARTPS_LIBRARY_LICENSE file included in this distribution.
 *
 *************************************************************************/

/*
 * GapMsg.hpp
 *
 *  Created on: Feb 20, 2014
 *      Author: Gonzalo Rodriguez Canosa
 *      email:  gonzalorodriguez@eprosima.com
 */



namespace eprosima{
namespace rtps{

bool CDRMessageCreator::createMessageGap(CDRMessage_t* msg,GuidPrefix_t guidprefix,
		SequenceNumber_t seqNumFirst,SequenceNumberSet_t seqNumList,
		EntityId_t readerId,EntityId_t writerId)
{
	CDRMessage::initCDRMsg(msg, RTPSMESSAGE_MAX_SIZE);
	try
	{
		CDRMessage_t header;
		VendorId_t vendor;
		VENDORID_EPROSIMA(vendor);
		ProtocolVersion_t version;
		PROTOCOLVERSION(version);
		CDRMessageCreator::createHeader(&header,guidprefix,version,vendor);
		CDRMessage::appendMsg(msg, &header);
		CDRMessage_t submsginfots;
		CDRMessageCreator::createSubmessageInfoTS_Now(&submsginfots,false);
		CDRMessage::appendMsg(msg, &submsginfots);
		CDRMessage_t submsgdata;
		CDRMessageCreator::createSubmessageGap(&submsgdata,seqNumFirst,seqNumList,readerId, writerId);
		CDRMessage::appendMsg(msg, &submsgdata);
		//cout << "SubMEssage created and added to message" << endl;
		msg->length = msg->pos;
	}
	catch(int e)
	{
		pError("Gap message error"<<endl)
		return false;
	}
	return true;
}

bool CDRMessageCreator::createSubmessageGap(CDRMessage_t* submsg,SequenceNumber_t seqNumFirst,SequenceNumberSet_t seqNumList,EntityId_t readerId,EntityId_t writerId)
{
	CDRMessage::initCDRMsg(submsg);

	//Create the two CDR msgs
	CDRMessage_t submsgHeader,submsgElem;
	CDRMessage::initCDRMsg(&submsgHeader,RTPSMESSAGE_SUBMESSAGEHEADER_SIZE);
	CDRMessage::initCDRMsg(&submsgElem,RTPSMESSAGE_MAX_SIZE);
	octet flags = 0x0;
	if(EPROSIMA_ENDIAN == BIGEND)
	{
		flags = flags | BIT(0);
		submsgElem.msg_endian = submsgHeader.msg_endian = BIGEND;
	}
	else
	{
		submsgElem.msg_endian = submsgHeader.msg_endian = LITTLEEND;
	}

	try{
		CDRMessage::addEntityId(&submsgElem,&readerId);
		CDRMessage::addEntityId(&submsgElem,&writerId);
		//Add Sequence Number
		CDRMessage::addSequenceNumber(&submsgElem,&seqNumFirst);
		CDRMessage::addSequenceNumberSet(&submsgElem,&seqNumList);
	}
	catch(int e)
	{
		pError("Gap submessage error"<<endl)
		return false;
	}


	//Once the submessage elements are added, the header is created
	CDRMessageCreator::createSubmessageHeader(&submsgHeader, GAP,flags,submsg->length);
	//Append Submessage elements to msg
	CDRMessage::appendMsg(submsg, &submsgHeader);
	CDRMessage::appendMsg(submsg, &submsgElem);
	submsg->length = submsg->pos;
	return true;
}


}
}
