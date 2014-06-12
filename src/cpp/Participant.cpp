/*************************************************************************
 * Copyright (c) 2014 eProsima. All rights reserved.
 *
 * This copy of eProsima RTPS is licensed to you under the terms described in the
 * EPROSIMARTPS_LIBRARY_LICENSE file included in this distribution.
 *
 *************************************************************************/

/**
 * @file Participant.cpp
 *
 */

#include "eprosimartps/Participant.h"

#include "eprosimartps/writer/StatelessWriter.h"
#include "eprosimartps/reader/StatelessReader.h"
#include "eprosimartps/reader/StatefulReader.h"
#include "eprosimartps/writer/StatefulWriter.h"
#include "eprosimartps/reader/RTPSReader.h"
#include "eprosimartps/writer/RTPSWriter.h"


#include "eprosimartps/discovery/ParticipantDiscoveryProtocol.h"
#include "eprosimartps/discovery/SimplePDP.h"

#include "eprosimartps/liveliness/WriterLiveliness.h"

#include "eprosimartps/utils/RTPSLog.h"
#include "eprosimartps/utils/IPFinder.h"

namespace eprosima {
namespace rtps {

typedef std::vector<RTPSReader*>::iterator p_ReaderIterator;
typedef std::vector<RTPSWriter*>::iterator p_WriterIterator;


ParticipantImpl::ParticipantImpl(const ParticipantAttributes& PParam,const GuidPrefix_t& guidP,uint32_t ID):

							m_defaultUnicastLocatorList(PParam.defaultUnicastLocatorList),
							m_defaultMulticastLocatorList(PParam.defaultMulticastLocatorList),
							m_participantName(PParam.name),
							m_guid(guidP,c_EntityId_Participant),
							m_send_thr(this),
							m_event_thr(this),
							mp_ResourceSemaphore(new boost::interprocess::interprocess_semaphore(0)),
							IdCounter(0),
							mp_PDP(NULL),
							mp_WL(NULL),
							m_participantID(ID),
							m_send_socket_buffer_size(PParam.sendSocketBufferSize),
							m_listen_socket_buffer_size(PParam.listenSocketBufferSize)
{
	Locator_t loc;
	loc.port = PParam.defaultSendPort;
	m_send_thr.initSend(loc);
	m_event_thr.init_thread();

	if(m_defaultUnicastLocatorList.empty())
	{
		pWarning("Participant created with NO default Unicast Locator List, adding Locator 0.0.0.0:11111"<<endl);
		LocatorList_t myIP;
		IPFinder::getIPAddress(&myIP);
		for(LocatorListIterator lit = myIP.begin();lit!=myIP.end();++lit)
		{
			lit->port=7555;
			m_defaultUnicastLocatorList.push_back(*lit);
		}
	}

	pInfo("Participant \"" <<  m_participantName << "\" with guidPrefix: " <<m_guid.guidPrefix<< endl);


	m_discovery = PParam.discovery;
	//START WRITER LIVELINESS PROTOCOL
	if(m_discovery.use_WriterLivelinessProtocol)
	{
		mp_WL = new WriterLiveliness(this);
	}
	//START LIVELINESS PROTOCOL
	if(m_discovery.use_SIMPLE_ParticipantDiscoveryProtocol)
	{
		mp_PDP = (ParticipantDiscoveryProtocol*) new SimplePDP(this);
		mp_PDP->initPDP(PParam.discovery, this->getParticipantId());
	}


}


ParticipantImpl::~ParticipantImpl()
{
	pDebugInfo("Participant destructor"<<endl;);
	//Destruct threads:
//	for(std::vector<ResourceListen*>::iterator it=m_threadListenList.begin();
//			it!=m_threadListenList.end();++it)
//		delete(*it);
	for(std::vector<ListenResource*>::iterator it=m_listenResourceList.begin();
				it!=m_listenResourceList.end();++it)
			delete(*it);

	for(std::vector<RTPSReader*>::iterator it=m_userReaderList.begin();
			it!=m_userReaderList.end();++it)
		delete(*it);

	for(std::vector<RTPSWriter*>::iterator it=m_userWriterList.begin();
			it!=m_userWriterList.end();++it)
		delete(*it);

	delete(this->mp_ResourceSemaphore);

	if(mp_PDP!=NULL)
		delete(mp_PDP);
}



bool ParticipantImpl::createWriter(RTPSWriter** WriterOut,
		PublisherAttributes& param, uint32_t payload_size, bool isBuiltin,
		StateKind_t kind, DDSTopicDataType* ptype, PublisherListener* inlisten,
		const EntityId_t& entityId)
{
	std::string type = (kind == STATELESS) ? "STATELESS" :"STATEFUL";
	pDebugInfo("Creating " << type << " Writer"<<endl);
	EntityId_t entId;
	if(entityId== c_EntityId_Unknown)
	{
		if(param.topic.getTopicKind() == NO_KEY)
			entId.value[3] = 0x03;
		else if(param.topic.getTopicKind() == WITH_KEY)
			entId.value[3] = 0x02;
		IdCounter++;
		octet* c = (octet*)&IdCounter;
		entId.value[2] = c[0];
		entId.value[1] = c[1];
		entId.value[0] = c[2];
	}
	else
	{
		entId = entityId;
	}
	RTPSWriter* SWriter = NULL;
	if(kind == STATELESS)
		SWriter = (RTPSWriter*)new StatelessWriter(param, m_guid.guidPrefix,entId,ptype);
	else if(kind == STATEFUL)
		SWriter = (RTPSWriter*)new StatefulWriter(param, m_guid.guidPrefix,entId,ptype);
	if(SWriter==NULL)
		return false;
	//If NO UNICAST
	SWriter->setListener(inlisten);
	SWriter->setQos(param.qos,true);
	SWriter->mp_send_thr = &this->m_send_thr;
	SWriter->mp_event_thr = &this->m_event_thr;
	if(kind == STATEFUL)
	{
		if(!assignEndpointListenResources((Endpoint*)SWriter,isBuiltin))
		{
			delete(SWriter);
			return false;
		}
	}
	if(!isBuiltin)
	{
		m_userWriterList.push_back(SWriter);
		if(mp_PDP!=NULL)
			mp_PDP->localWriterMatching(SWriter,true);
		if(mp_WL!=NULL)
			mp_WL->addLocalWriter(SWriter);
	}
	m_allWriterList.push_back(SWriter);


	*WriterOut = SWriter;
	return true;
}



bool ParticipantImpl::createReader(RTPSReader** ReaderOut,
		SubscriberAttributes& param, uint32_t payload_size, bool isBuiltin,
		StateKind_t kind, DDSTopicDataType* ptype, SubscriberListener* inlisten,
		const EntityId_t& entityId)
{
	std::string type = (kind == STATELESS) ? "STATELESS" :"STATEFUL";
		pDebugInfo("Creating " << type << " Reader"<<endl);
	EntityId_t entId;
	if(entityId == c_EntityId_Unknown)
	{
		if(param.topic.getTopicKind() == NO_KEY)
			entId.value[3] = 0x04;
		else if(param.topic.getTopicKind() == WITH_KEY)
			entId.value[3] = 0x07;
		IdCounter++;
		octet* c = (octet*)&IdCounter;
		entId.value[2] = c[0];
		entId.value[1] = c[1];
		entId.value[0] = c[2];
	}
	else
		entId = entityId;
	RTPSReader* SReader = NULL;
	if(kind == STATELESS)
		SReader = (RTPSReader*)new StatelessReader(param, m_guid.guidPrefix,entId,ptype);
	else if(kind == STATEFUL)
		SReader = (RTPSReader*)new StatefulReader(param, m_guid.guidPrefix,entId,ptype);
	if(SReader==NULL)
		return false;
	SReader->setListener(inlisten);
	SReader->setQos(param.qos,true);
	SReader->mp_send_thr = &this->m_send_thr;
	SReader->mp_event_thr = &this->m_event_thr;
	if(!assignEndpointListenResources((Endpoint*)SReader,isBuiltin))
	{
		delete(SReader);
		return false;
	}
	if(!isBuiltin)
	{
		m_userReaderList.push_back(SReader);
		if(mp_PDP!=NULL)
			mp_PDP->localReaderMatching(SReader,true);
	}
	m_allReaderList.push_back(SReader);


	*ReaderOut = SReader;
	return true;
}

bool ParticipantImpl::assignEndpointListenResources(Endpoint* endp,bool isBuiltin)
{
	bool valid = true;
	if(endp->unicastLocatorList.empty() && !isBuiltin )
	{
		std::string auxstr = endp->getEndpointKind() == WRITER ? "WRITER" : "READER";
		pWarning(auxstr << " created with no unicastLocatorList, adding default List"<<endl);
		for(LocatorListIterator lit = m_defaultUnicastLocatorList.begin();lit!=m_defaultUnicastLocatorList.end();++lit)
		{
			assignLocator2ListenResources(endp,lit,false,false);
		}
		endp->unicastLocatorList = m_defaultUnicastLocatorList;
	}
	else
	{
		for(LocatorListIterator lit = endp->unicastLocatorList.begin();lit!=endp->unicastLocatorList.end();++lit)
		{
			valid &= assignLocator2ListenResources(endp,lit,false,!isBuiltin);
		}
	}
	//MULTICAST
	if(endp->multicastLocatorList.empty() && !isBuiltin)
	{
		for(LocatorListIterator lit =m_defaultMulticastLocatorList.begin();lit!=m_defaultMulticastLocatorList.end();++lit)
		{
			valid &= assignLocator2ListenResources(endp,lit,true,false);
		}
		endp->multicastLocatorList = m_defaultMulticastLocatorList;
	}
	else
	{
		for(LocatorListIterator lit = endp->multicastLocatorList.begin();lit!=endp->multicastLocatorList.end();++lit)
		{
			valid &= assignLocator2ListenResources((Endpoint*)endp,lit,true,!isBuiltin);
		}
	}
	return valid;
}


bool ParticipantImpl::assignLocator2ListenResources(Endpoint* endp,LocatorListIterator lit,bool isMulti,bool isFixed)
{
	for(std::vector<ListenResource*>::iterator it = m_listenResourceList.begin();it!=m_listenResourceList.end();++it)
	{
		if((*it)->isListeningTo(*lit))
		{
			(*it)->addAssociatedEndpoint(endp);
			return true;
		}
	}
	ListenResource* LR = new ListenResource(this);
	Locator_t loc = LR->init_thread(*lit,isMulti,isFixed);
	if(loc.kind>0)
	{
		LR->addAssociatedEndpoint(endp);
		*lit = loc;
		m_listenResourceList.push_back(LR);
		return true;
	}
	else
	{
		delete(LR);
		return false;
	}
}




bool ParticipantImpl::deleteUserEndpoint(Endpoint* p_endpoint,char type)
{
	bool found = false;
	if(type == 'W')
	{
		for(p_WriterIterator wit=m_userWriterList.begin();
				wit!=m_userWriterList.end();++wit)
		{
			if((*wit)->getGuid().entityId == p_endpoint->getGuid().entityId) //Found it
			{
				m_userWriterList.erase(wit);
				found = true;
				break;
			}
		}
	}
	if(type == 'R')
	{
		for(p_ReaderIterator rit=m_userReaderList.begin()
				;rit!=m_userReaderList.end();++rit)
		{
			if((*rit)->getGuid().entityId == p_endpoint->getGuid().entityId) //Found it
			{
				m_userReaderList.erase(rit);
				found = true;
				break;
			}
		}
	}
	if(!found)
		return false;
	//Remove it from threadListenList
	std::vector<ListenResource*>::iterator thit;
	for(thit=m_listenResourceList.begin();
			thit!=m_listenResourceList.end();thit++)
	{
		(*thit)->removeAssociatedEndpoint(p_endpoint);
		if(!(*thit)->hasAssociatedEndpoints())
			delete(*thit);
	}
	//FIXME REMOVE ENDPOINT FROM DISCOVERY PROTOCOL
//	if(mp_PDP!=NULL)
//		mp_PDP->
	if(mp_WL!=NULL && p_endpoint->getEndpointKind()==WRITER)
		mp_WL->removeLocalWriter((RTPSWriter*)p_endpoint);
	delete(p_endpoint);
	return true;
}

void ParticipantImpl::announceParticipantState()
{
	this->mp_PDP->announceParticipantState(false);
}

void ParticipantImpl::stopParticipantAnnouncement()
{
	this->mp_PDP->stopParticipantAnnouncement();
}

void ParticipantImpl::resetParticipantAnnouncement()
{
	this->mp_PDP->resetParticipantAnnouncement();
}

void ParticipantImpl::ResourceSemaphorePost()
{
	if(mp_ResourceSemaphore!=NULL)
	{
		mp_ResourceSemaphore->post();
	}
}

void ParticipantImpl::ResourceSemaphoreWait()
{
	if(mp_ResourceSemaphore!=NULL)
	{
		mp_ResourceSemaphore->wait();
	}
}



} /* namespace rtps */
} /* namespace eprosima */


