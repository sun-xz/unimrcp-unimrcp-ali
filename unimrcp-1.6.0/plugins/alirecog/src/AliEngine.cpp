#include"AliEngine.h"

CAliEngine::CAliEngine():CRecogEngine<Ali_recog_channel>(),
						m_engine(NULL),
						m_ChFactory(NULL),
						m_RecogPool(NULL)
{

}

CAliEngine::~CAliEngine()
{

}

bool CAliEngine::EngineInit(mrcp_engine_t* engine)
{
	if (NULL == engine)
		return FALSE;

	const char* RecordPath = mrcp_engine_param_get(engine, ALI_RECORD_PATH);
	if (NULL == RecordPath) {
#if _WIN32
		RecordPath = "..\\var";
#else
		RecordPath = "../var";
#endif
	}

	const char* Record = mrcp_engine_param_get(engine, ALI_RECORD);
	if (NULL == Record) {
		Record = "false";
	}

	const char* Alilog = mrcp_engine_param_get(engine, ALI_LOG);
	if (NULL == Alilog) {
#if _WIN32
		Alilog = "..\\log\\alirecogsdk.log";
#else
		Alilog = "../log/alirecogsdk.log";
#endif
	}

	const char * appKey = NULL;
	ALI_CFG_PARAM_GET(appKey, ALI_APPID, "", engine);

	const char * AccessKey = NULL;
	ALI_CFG_PARAM_GET(AccessKey, ALI_KEYID, "", engine);

	const char * AccessSecret = NULL;
	ALI_CFG_PARAM_GET(AccessSecret, ALI_SECRET, "", engine);

	const char * Format = NULL;
	ALI_CFG_PARAM_GET(Format, ALI_FOEMAT, "pcm", engine);

	const char * SampleRate = NULL;
	ALI_CFG_PARAM_GET(SampleRate, ALI_SAMPLERATE, "8000", engine);

	const char * SpeechRecogUrl = NULL;
	ALI_CFG_PARAM_GET(SpeechRecogUrl, ALI_RECOG_URL, "", engine);

	const char * IntermediateResult = NULL;
	ALI_CFG_PARAM_GET(IntermediateResult, ALI_INTERMEDIATE_RESULT, "false", engine);

	const char * PunctuationPrediction = NULL;
	ALI_CFG_PARAM_GET(PunctuationPrediction, ALI_PUNCTUATION_PREDICTION, "false", engine);

	const char * InverseTextNormalization = NULL;
	ALI_CFG_PARAM_GET(InverseTextNormalization, ALI_INVERSE_TEXT, "false", engine);

	const char * EnableVoiceDetection = NULL;
	ALI_CFG_PARAM_GET(EnableVoiceDetection, ALI_VOICE_DETEION, "false", engine);

	const char * StartSilence = NULL;
	ALI_CFG_PARAM_GET(StartSilence, ALI_START_SILENCE, "3", engine);

	const char * EndSilence = NULL;
	ALI_CFG_PARAM_GET(EndSilence, ALI_END_SILENCE, "0", engine);

	const char * CustomizationId = NULL;
	ALI_CFG_PARAM_GET(CustomizationId, ALI_CUSTOMIZATION_ID, "", engine);

	const char * VocabularyId = NULL;
	ALI_CFG_PARAM_GET(VocabularyId, ALI_VOCABULARY_ID, "", engine);

	const char * OutputFormat = NULL;
	ALI_CFG_PARAM_GET(OutputFormat, ALI_OUT_PUT_FORMAT, "UTF-8", engine);

	const char * ContextParam = NULL;
	ALI_CFG_PARAM_GET(ContextParam, ALI_CONTEXT_PARAM, "", engine);


	m_engine = engine;
	AliNlsClientInit(Alilog);

	apt_bool_t record = (string(Record) == "true") ? true : false;
	m_ChFactory = new AliChannelFactory;
	if (!m_ChFactory->Init(
		engine->pool,
		engine->config->max_channel_count,
		appKey,
		AccessKey,
		AccessSecret,
		Format,
		SampleRate,
		IntermediateResult,
		InverseTextNormalization,
		EnableVoiceDetection,
		EndSilence,
		StartSilence,
		PunctuationPrediction,
		CustomizationId,
		VocabularyId,
		OutputFormat,
		ContextParam,
		RecordPath,
		record)) {
		LOG_ERROR("Recog ChannelFactory Init failed ...");
		return FALSE;
	}

	m_RecogPool = new CRecogPool;
	if (apr_status_t rv = m_RecogPool->Init(m_engine->pool, 1, engine->config->max_channel_count)) {
		LOG_ERROR("Recog pool init failed Msg :%s", apr_error(rv).c_str());
		return FALSE;
	}

	LOG_INFO("Ali Recog Engine Init success");

	return true;
}

void CAliEngine::EngineUinit()
{
	if (m_ChFactory) {
		m_ChFactory->Uinit();
		delete m_ChFactory;
		m_ChFactory = NULL;
	}
	if (m_RecogPool) {
		m_RecogPool->Uinit();
		delete m_RecogPool;
		m_RecogPool = NULL;
	}

	AliNlsClientUinit();
}

bool CAliEngine::EngineRecogStart(mrcp_message_t* request, Ali_recog_channel * pCh)
{
	if (NULL == pCh)
		return FALSE;
	pCh->ch = m_ChFactory->NewChannel();
	if (NULL == pCh->ch) {
		LOG_ERROR("Get recog channel failed");
		return FALSE;
	}
	else {
		pCh->ch->SetRecogChannel(pCh);
		if (!pCh->ch->Start()) {
			pCh->ch->Stop();
			LOG_ERROR("Start recog failed");
			return FALSE;
		}
		if (mrcp_generic_header_property_check(request, GENERIC_HEADER_VENDOR_SPECIFIC_PARAMS) == TRUE) {
			mrcp_generic_header_t* generic_header = mrcp_generic_header_get(request);
			if (generic_header && generic_header->vendor_specific_params) {
				apt_pair_arr_t* vendor_specific_params = apt_pair_array_copy(generic_header->vendor_specific_params, request->pool);
				pCh->ch->SetParams(vendor_specific_params);
			}
		}
		apr_status_t stu = m_RecogPool->TaskPush(pCh->ch, AliChannel::RecogMain);
		if (APR_SUCCESS != stu) {
			LOG_ERROR("Recog task push failed errMsg:%s code:%d",apr_error(stu).c_str(),stu);
			return FALSE;
		}

		m_ChFactory->ResetChannelPoolState(pCh->ch);
	}

	return TRUE;
}

bool CAliEngine::EngineRecogStop(Ali_recog_channel * pCh)
{
	if (NULL == pCh)
		return TRUE;
	pCh->ch->Stop();
	m_ChFactory->ReleaseChannel(pCh->ch);

	return true;
}

int	CAliEngine::EngineWriteFrame(Ali_recog_channel * pCh, const mpf_frame_t *frame)
{
	if (NULL == pCh || NULL == frame)
		return -1;
	if (pCh->ch->IsWork()) {
		pCh->ch->WriteFrame(frame);
	}
	return APR_SUCCESS;
}

CAliEngine* CAliEngine::GetAliEngine()
{
	static CAliEngine engine;
	return &engine;
}

void CAliEngine::AliNlsClientInit(const char* logPath)
{
	NlsClient::getInstance()->setLogConfig(logPath, LogDebug);
	NlsClient::getInstance()->startWorkThread(4);
}

void CAliEngine::AliNlsClientUinit()
{
	NlsClient::getInstance()->releaseInstance();
}
