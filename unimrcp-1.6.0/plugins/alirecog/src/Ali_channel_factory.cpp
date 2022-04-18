#include"Ali_channel_factory.h"
//#include"Ali_define.h"

mrcp_engine_t* AliChannelFactory::m_engine = NULL;

AliChannelFactory::AliChannelFactory()
{

}

AliChannelFactory::~AliChannelFactory()
{

}


apt_bool_t AliChannelFactory::Init(
	apr_pool_t*pool,
	apr_size_t MaxCh,
	const char* Appkey,
	const char * AccessKeyID,
	const char * AccessKeySecret,
	const char * Format,
	const char * SampleRate,
	const char * intermediateResult,
	const char * inverseText,
	const char * voiceDetection,
	const char * maxEndSilence,
	const char * maxStartSilence,
	const char * PunctuationPrediction,
	const char * CustomizationId,
	const char * VocabularyId,
	const char * OutputFormat,
	const char * ContextParam,
	const char * recordPath,
	apt_bool_t	 record)
{
	if (NULL == pool)
		return FALSE;

	m_pool = pool;
	m_appKey = Appkey;
	m_AccessKey = AccessKeyID;
	m_Secret =	AccessKeySecret;
	m_Format =	Format;
	m_SampleRate = SampleRate;

	if (apr_thread_mutex_create(&m_Mutex, APR_THREAD_MUTEX_DEFAULT, m_pool) != APR_SUCCESS) {
		LOG_ERROR("Recog init channelFac failed...");
		return FALSE;
	}

	unsigned int i = 0;
	for (i = 0; i < MaxCh; i++)
	{
		AliChannel * AliCh = new AliChannel(
		i,
		Appkey, 
		AccessKeyID, 
		AccessKeySecret,
		Format, 
		SampleRate, 
		intermediateResult, 
		inverseText, 
		voiceDetection, 
		maxEndSilence, 
		maxStartSilence,
		PunctuationPrediction,
		CustomizationId,
		VocabularyId,
		OutputFormat,
		ContextParam,
		recordPath, 
		record);

		if (AliCh->Init(m_pool)) {
			ResetChannelPoolState(AliCh);
		}
		else {
			AliCh->Uninit();
			delete AliCh;
			AliCh = NULL;
			LOG_WARN("AliChannel init failed Ch id :%d", i);
		}
	}
	m_Chid = i;


	return TRUE;
}

//根据工作状态添加到忙或闲的池
void AliChannelFactory::ResetChannelPoolState(class AliChannel*pCh)
{
	Thread_mutex_lock();

	if (NULL != pCh) {
		if (!pCh->IsWork()) {
			//添加到空闲池
			MapChPool::iterator it = m_ChIdlePool.find(pCh->GetChannelId());
			if (it == m_ChIdlePool.end()) {
				m_ChIdlePool[pCh->GetChannelId()] = pCh;
			}
			
		}
		//添加到使用池
		else {
			MapChPool::iterator it = m_ChBusyPool.find(pCh->GetChannelId());
			if (it == m_ChBusyPool.end()) {
				m_ChBusyPool[pCh->GetChannelId()] = pCh;
			}
		}

	}

	Thread_mutex_ulock();
}

void AliChannelFactory::Uinit()
{


	MapChPool::iterator begine = m_ChBusyPool.begin();
	for (; begine != m_ChBusyPool.end(); begine++)
	{
		begine->second->Stop();
		begine->second->Uninit();
		delete begine->second;

	}

	begine = m_ChIdlePool.begin();
	for (; begine != m_ChIdlePool.end(); begine++)
	{
		begine->second->Stop();
		begine->second->Uninit();
		delete begine->second;

	}

	apr_thread_mutex_destroy(m_Mutex);

	return;
}


AliChannel * AliChannelFactory::GetIdleChannel()
{
	AliChannel *pCh = NULL;

	MapChPool::iterator begine = m_ChBusyPool.begin();
	for (; begine != m_ChBusyPool.end(); ) {
		if (!begine->second->IsWork()) {
			m_ChIdlePool[begine->second->GetChannelId()] = begine->second;
			m_ChBusyPool.erase(begine++);
		}
		else {
			begine++;
		}
	}

	begine = m_ChIdlePool.begin();
	for (; begine != m_ChIdlePool.end(); begine++) {
		if (!begine->second->IsWork()) {
			pCh = begine->second;
			m_ChIdlePool.erase(pCh->GetChannelId());
			break;
		}
	}

	


	return pCh;
}

//获取空闲的channel
AliChannel *AliChannelFactory::NewChannel()
{

	Thread_mutex_lock();

	AliChannel *pCh = NULL;
	pCh = GetIdleChannel();

	Thread_mutex_ulock();

	return pCh;
}
//放到空闲池
void AliChannelFactory::ReleaseChannel(class AliChannel*pCh)
{
	Thread_mutex_lock();

	//do {
		//if (NULL == pCh)
			//break;

		if (!pCh->IsWork()) {
			if (m_ChIdlePool.find(pCh->GetChannelId()) == m_ChIdlePool.end()) {
				m_ChIdlePool[pCh->GetChannelId()] = pCh;

				if (m_ChBusyPool.find(pCh->GetChannelId()) != m_ChBusyPool.end()) {
					m_ChBusyPool.erase(pCh->GetChannelId());
				}

			}
		}

		//pCh->Clean();

	//} while (false);

	Thread_mutex_ulock();

	return;
}


void AliChannelFactory::Thread_mutex_lock()
{
	apr_thread_mutex_lock(m_Mutex);
}

void AliChannelFactory::Thread_mutex_ulock()
{
	apr_thread_mutex_unlock(m_Mutex);
}













