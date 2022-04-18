#include"Ali_SynthEngine.h"


CAliSynthEngine::CAliSynthEngine()noexcept
								:CSynthgEngine<Ali_synth_channel_t>(),
								m_engine(NULL),
								m_SynthPool(NULL),
								m_channelFactory(NULL)					
{

}

CAliSynthEngine::~CAliSynthEngine()noexcept
{

}

bool CAliSynthEngine::EngineInit(mrcp_engine_t* engine)
{
	if (NULL == engine)
		return FALSE;
	
	ALI_PARAM_GET(appkey, ALI_PARAM_appkey, "default")
	ALI_PARAM_GET(AccessKey, ALI_PARAM_AccessKey, "default")
	ALI_PARAM_GET(AccessSecret, ALI_PARAM_AccessSecret, "default")
	ALI_PARAM_GET(voice, ALI_PARAM_voice, "xiaoyun")
	ALI_PARAM_GET(format, ALI_PARAM_format, "pcm")
	ALI_PARAM_GET(sample, ALI_PARAM_sample, "8000")
	ALI_PARAM_GET(volume, ALI_PARAM_volume, "50")
	ALI_PARAM_GET(speech, ALI_PARAM_speech, "0")
	ALI_PARAM_GET(pitch, ALI_PARAM_pitch, "0")

	ALI_PARAM_GET(recogPath, ALI_RECORD_PATH, "../var")
	ALI_PARAM_GET(record, ALI_RECORD, "false")
	ALI_PARAM_GET(AliSdkLog, ALI_LOG, "../log")

	AliNlsClientInit(AliSdkLog);

	m_engine = engine;
	m_SynthPool = new CSynthPool();
	if (m_SynthPool->Init(engine->pool,1,engine->config->max_channel_count) != APR_SUCCESS) {
		LOG_ERROR("Synth pool init failed ");
		return FALSE;
	}

	m_channelFactory = new AliSynthChannelFactory();
	if (!m_channelFactory->Init(engine->config->max_channel_count,
		m_engine->pool,
		appkey,
		AccessKey,
		AccessSecret,
		voice,
		format,
		sample,
		volume,
		speech,
		pitch,
		recogPath,
		record)) {
		LOG_ERROR("Synth channel factory  Init failed ");
		return FALSE;
	}
	LOG_INFO("Ali Synth Engine init success");
	return TRUE;
}
void CAliSynthEngine::EngineUinit()
{
	if (m_SynthPool) {
		m_SynthPool->UInit();
		delete m_SynthPool;
	}

	if (m_channelFactory) {
		m_channelFactory->UInit();
		delete m_channelFactory;
	}

	m_SynthPool = NULL;
	m_channelFactory = NULL;
	m_engine = NULL;

	AliNlsClientUinit();

	return;
}

bool CAliSynthEngine::EngineSynthStart(mrcp_message_t* request, Ali_synth_channel_t * EngineCh,const char* speakTxt)
{
	if (NULL == EngineCh || NULL == speakTxt)
		return FALSE;

	if (!m_channelFactory || !m_SynthPool)
		return FALSE;

	CAliSynthChannel * pCh = m_channelFactory->NewChannel();
	mrcp_synth_header_t* req_synth_header;
	std::string voice_param;
	req_synth_header = (mrcp_synth_header_t*)mrcp_resource_header_get(request);
	if (req_synth_header) {
		if (mrcp_resource_header_property_check(request, SYNTHESIZER_HEADER_VOICE_NAME) == TRUE) {
			voice_param = req_synth_header->voice_param.name.buf;
		}
	}
	if (pCh && pCh->start(voice_param,speakTxt,EngineCh)) {
		EngineCh->AliSynthCh = pCh;
		m_SynthPool->TaskPush(pCh, CAliSynthChannel::synthMain);
	}
	else {
		LOG_ERROR("Channel start failed id :%u", pCh->getChannelId());
		m_channelFactory->ReleaseChannel(pCh);
		EngineCh->AliSynthCh = NULL;
		return FALSE;
	}

	return TRUE;
}

int  CAliSynthEngine::EngineReadFrame(Ali_synth_channel_t * EngineCh, mpf_frame_t **frame)
{

	apr_status_t stu = APR_SUCCESS;
	do {

		if (!EngineCh || !EngineCh->AliSynthCh) {
			stu = -1; break;
		}

		if (!EngineCh->AliSynthCh->is_Play()) {
			stu = -1; break;
		}

		EngineCh->AliSynthCh->readVoiceData((unsigned char*)(*frame)->codec_frame.buffer, (*frame)->codec_frame.size);
		(*frame)->type |= MEDIA_FRAME_TYPE_AUDIO;

	} while (FALSE);

	if (-1 == stu) {
		memset((*frame)->codec_frame.buffer, 0, (*frame)->codec_frame.size);
	}

	return stu;
}

bool CAliSynthEngine::EngineSynthStop(Ali_synth_channel_t * EngineCh)
{
	if (!EngineCh || !EngineCh->AliSynthCh)
		return TRUE;

	EngineCh->AliSynthCh->stop();
	m_channelFactory->ReleaseChannel(EngineCh->AliSynthCh);
	EngineCh->AliSynthCh = NULL;

	return TRUE;
}

void CAliSynthEngine::AliNlsClientInit(const char* logPath)
{
	NlsClient::getInstance()->setLogConfig(logPath, LogLevel::LogDebug);
	NlsClient::getInstance()->startWorkThread(4);
}

void CAliSynthEngine::AliNlsClientUinit()
{
	NlsClient::releaseInstance();
}

CAliSynthEngine* CAliSynthEngine::GetAliSynthEngine()
{
	static CAliSynthEngine Synthengine;
	return &Synthengine;
}