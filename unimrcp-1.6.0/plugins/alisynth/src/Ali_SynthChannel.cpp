#include"Ali_SynthChannel.h"
#include <stdlib.h>

string CAliSynthChannel::m_token = "";
unsigned long CAliSynthChannel::m_expireTime = 0;

CAliSynthChannel::CAliSynthChannel():
									m_id(0),
									m_appKey(""),
									m_AccessKey(""),
									m_AccessSecret(""),
									m_VoiceName(""),
									m_format(""),
									m_sample(""),
									m_volume(""),
									m_speech(""),
									m_pitch(""),
									m_Text(""),
									m_Start(FALSE),
									m_Play(FALSE),
									m_Runing(FALSE),
									m_record(FALSE),
									m_recordPath(""),
									m_SynthReq(NULL),
									m_cursors(0),
									m_file(NULL),
									m_lock(NULL),
									m_VoiceQueue(NULL),
									m_pool(NULL)
{
	m_Voice.clear();
}

CAliSynthChannel::CAliSynthChannel(apr_size_t id,
									const char * appKey,
									const char * AccessKeyID,
									const char * AccessSecret,
									const char * VoiceName,
									const char * format,
									const char * sample,
									const char * volume,
									const char * speech,
									const char * pitch,
									const bool	record,
									const char * recordPaht)noexcept:
									m_id(id),
									m_appKey(appKey),
									m_AccessKey(AccessKeyID),
									m_AccessSecret(AccessSecret),
									m_VoiceName(VoiceName),
									m_format(format),
									m_sample(sample),
									m_volume(volume),
									m_speech(speech),
									m_pitch(pitch),
									m_Text(""),
									m_Start(FALSE),
									m_Play(FALSE),
									m_Runing(FALSE),
									m_record(record),
									m_recordPath(recordPaht),
									m_SynthReq(NULL),
									m_cursors(0),
									m_file(NULL),
									m_lock(NULL),
									m_VoiceQueue(NULL),
									m_pool(NULL)
{
	m_VoiceFrame.cur = 0;
	m_VoiceFrame.frame.buffer = NULL;
	m_VoiceFrame.frame.size = 0;
	m_Voice.clear();
}

CAliSynthChannel::~CAliSynthChannel() noexcept
{
	m_VoiceFrame.cur = 0;
	m_VoiceFrame.frame.buffer = NULL;
	m_VoiceFrame.frame.size = 0;
}


apt_bool_t CAliSynthChannel::init(apr_pool_t * pool)
{
	if (NULL == pool)
		return FALSE;

	apr_status_t stu = APR_SUCCESS;
	m_pool = pool;

	if ((stu = apr_thread_mutex_create(&m_lock, APR_THREAD_MUTEX_DEFAULT, m_pool)) != APR_SUCCESS) {
		LOG_ERROR("Synth Channel init lock failed id:%d status:%d", m_id,stu);
		return FALSE;
	}

	if ((stu = apr_queue_create(&m_VoiceQueue, MAX_QUEUE_SIZE, m_pool)) != APR_SUCCESS) {
		LOG_ERROR("Synth Channel init Voice Queue failed id:%d status:%d", m_id, stu);
		return FALSE;
	}

	if (m_id == 0) {
		if (!DoCheckSynthToKen()) {
			LOG_ERROR("Synth Channel init token error");
		}
	}

	return TRUE;
}

apt_bool_t CAliSynthChannel::start(const string& voice_param,const string& speakText,Ali_synth_channel_t * engineCh)
{
	if (NULL == engineCh || 0 == speakText.length())
		return FALSE;
	m_ParamVoiceName = voice_param;

	lock();

	m_engineCh = engineCh;
	m_Text = speakText;

	m_Start = TRUE;
	m_Play = TRUE;
	m_Runing = TRUE;
	m_cursors = 0;
	m_Voice.clear();

    if (m_VoiceFrame.frame.buffer) {
        delete[] m_VoiceFrame.frame.buffer;
    }
	m_VoiceFrame.cur = 0;
    m_VoiceFrame.frame.buffer = NULL;
	m_VoiceFrame.frame.size = 0;
	m_VoiceFrame.item = 0;

    while (0 != apr_queue_size(m_VoiceQueue)) { 
        Frame * frame = NULL;
        apr_queue_pop(m_VoiceQueue, (void**)&frame);
        if (NULL != frame) {
            delete[] frame->buffer;
            delete frame;
            frame = NULL;
        }
    }

	if (m_record) {

		char szFileName[255] = { 0 };
		string FileName = "";
		apr_snprintf(szFileName, sizeof(szFileName), "[AliRecog]-[Channel%s]-[%sHz]-[Date%s].pcm",
			m_engineCh->channel->id.buf, m_sample.c_str(), CUtil::TimeToStr(apr_time_now(), "%Y-%m-%d %H-%M-%S").c_str());

		if (m_recordPath[m_recordPath.length() - 1] == PATHDIA) {
			FileName = m_recordPath.append(szFileName);
		}
		else {
			FileName = m_recordPath + PATHDIA + szFileName;
		}

		recordStart(FileName);
	}

	unlock();

	return TRUE;
}

apt_bool_t CAliSynthChannel::stop()
{
	lock();
	m_Start = FALSE;
	m_Play = FALSE;
	m_Runing = FALSE;
	unlock();
	return TRUE;
}

apt_bool_t CAliSynthChannel::uninit()
{
	m_engineCh = NULL;
	if (NULL != m_file)
		apr_file_close(m_file);

	if (NULL != m_VoiceQueue)
		apr_queue_term(m_VoiceQueue);

	if (NULL != m_lock)
		apr_thread_mutex_destroy(m_lock);


	return TRUE;
}

apt_bool_t CAliSynthChannel::is_Synth()const
{
	return m_Start;
}

apt_bool_t	CAliSynthChannel::is_Play()const {
	return m_Play;
}

void *	CAliSynthChannel::synthMain(apr_thread_t* tid, void * arg)
{
	CAliSynthChannel * pCh = (CAliSynthChannel *)arg;
	if (NULL != pCh)
		pCh->synthMain();

	return NULL;
}

void CAliSynthChannel::synthMain()
{
	do {
		if (!DoCreateSynthRequest())
			break;
		if (!DoSynthRequestInit())
			break;

		while (m_Start) {
			lock();
			if (0 != m_Voice.size()) {
				unsigned int i = m_cursors;
				for (; i < m_Voice.size() && m_Start; i++) {
					Frame* frame = new Frame;
					frame->buffer = new unsigned char[m_Voice[i].size()];
					memset(frame->buffer, 0, m_Voice[i].size());
					memcpy(frame->buffer, &m_Voice[i][0], m_Voice[i].size());
					frame->size = m_Voice[i].size();
					//LOG_INFO("synthMain send data :%d frame :%x cur :%d size :%d", m_Voice[i].size(), frame, i, frame->size);
					apr_queue_push(m_VoiceQueue, (void*)frame);
					recordMain((unsigned char*)& m_Voice[i][0], m_Voice[i].size());
				}
				m_cursors = i; //记录已经发送了多少数据
			}
			unlock();
		}

	} while (FALSE);

	m_Voice.clear();
	m_cursors = 0;
	//m_VoiceFrame.cur = 0;
	DoDestroySynthRequest();
	recordClose();
	return;
}

void CAliSynthChannel::readVoiceFrameData(unsigned char* in_ptr, const apr_size_t size)
{
	if (m_VoiceFrame.frame.size > size && m_VoiceFrame.frame.buffer) {
		memcpy(in_ptr, m_VoiceFrame.frame.buffer + m_VoiceFrame.cur, size);
		if (0 == m_VoiceFrame.cur) {
			m_VoiceFrame.cur = size;
		}
		else {
			m_VoiceFrame.cur += size;
		}
		m_VoiceFrame.frame.size -= size;
	}
	else {
		memcpy(in_ptr, m_VoiceFrame.frame.buffer + m_VoiceFrame.cur, m_VoiceFrame.frame.size);
		memset(in_ptr + m_VoiceFrame.frame.size, 0, size - m_VoiceFrame.frame.size);
		m_VoiceFrame.cur = 0;
		m_VoiceFrame.frame.size = 0;
		delete[] m_VoiceFrame.frame.buffer;
		m_VoiceFrame.frame.buffer = NULL;
		if (1 == m_VoiceFrame.item && 0 == apr_queue_size(m_VoiceQueue)) {
			lock();
			m_Play = FALSE; //停止播放
			unlock();
		}
	}
}

void CAliSynthChannel::readVoiceData(unsigned char *in_ptr,const apr_size_t size)
{
    // 每次只读160字节的原始数据
    if (m_Play) {
        if (0 != m_VoiceFrame.frame.size) {
			readVoiceFrameData(in_ptr, size);
        }
        else {
            Frame * frame = NULL;
            if (0 != apr_queue_size(m_VoiceQueue)) {
                m_VoiceFrame.item = apr_queue_size(m_VoiceQueue);
                apr_queue_pop(m_VoiceQueue, (void**)&frame);
                if (NULL != frame) {
                    m_VoiceFrame.frame.buffer = frame->buffer;
                    m_VoiceFrame.frame.size = frame->size;
                    m_VoiceFrame.cur = 0;
                    delete frame;
					readVoiceFrameData(in_ptr, size);
                }
            }
            else {
                memset(in_ptr, 0, size);
            }
        }
    }
    else{
        memset(in_ptr, 0, size);
    }
	return;
}

void CAliSynthChannel::recordMain(unsigned char * vioce_data,const apr_size_t& size)
{
	if (NULL == vioce_data || 0 == size)
		return;
	apr_size_t nsize = size;
	if (m_record && m_file) {
		if (apr_file_write(m_file, vioce_data, &nsize) != APR_SUCCESS) {
			LOG_WARN("record write data failed Channel id :%d", m_id);
		}
	}

	return;
}

void CAliSynthChannel::recordStart(const string& FileName)
{
	if (m_record) {
		apr_status_t stu = apr_file_open(&m_file, FileName.c_str(),
			APR_FOPEN_CREATE | APR_FOPEN_WRITE | APR_FOPEN_BINARY, APR_FPROT_OS_DEFAULT,
			m_pool);

		if (APR_SUCCESS != stu) {
			LOG_WARN("Start record filed error Msg :%s status:%d", CUtil::aprErrorToStr(stu).c_str(), stu);
			recordClose();
		}
	}
}

void CAliSynthChannel::recordClose()
{
	if (m_record && m_file) {
		apr_file_close(m_file);
		m_file = NULL;
	}
	return;
}

int CAliSynthChannel::DoGetAliToken()
{
	NlsToken nlsTokenRequest;
	nlsTokenRequest.setAccessKeyId(m_AccessKey);
	nlsTokenRequest.setKeySecret(m_AccessSecret);
	if (-1 == nlsTokenRequest.applyNlsToken()) {
		LOG_ERROR("Synth Get Ali Token Failed:%s", nlsTokenRequest.getErrorMsg());
		return -1;
	}
	m_token = nlsTokenRequest.getToken();
	m_expireTime = nlsTokenRequest.getExpireTime();	
	LOG_INFO("Synth GetAliToKen: %s", m_token.c_str());
	return 0;
}

bool CAliSynthChannel::DoCheckSynthToKen()
{
	std::time_t curTime = 0;
	curTime = std::time(0);
	if ((m_expireTime - curTime < 120) || m_token.empty()) {
		if (-1 == DoGetAliToken()) {
			return FALSE;
		}
	}
	if (m_SynthReq) {
		m_SynthReq->setToken(m_token.c_str());
	}

	return TRUE;
}

bool CAliSynthChannel::DoSynthRequestInit()
{
	if (m_SynthReq && (0 != m_Text.length())){

		if (-1 == DoCheckSynthToKen()) {
			return FALSE;
		}

		// 设置AppKey, 必填参数, 请参照官网申请
		m_SynthReq->setAppKey(m_appKey.c_str()); 

		//string Text = CUtil::GBKToUTF8(m_Text);
		// 设置待合成文本, 必填参数. 文本内容必须为UTF-8编码
		m_SynthReq->setText(m_Text.c_str());

		// 发音人
		if (m_ParamVoiceName != "") {
			m_SynthReq->setVoice(m_ParamVoiceName.c_str());
		}
		else if (0 != m_VoiceName.length()) {
			m_SynthReq->setVoice(m_VoiceName.c_str());
		}

		// 音量, 范围是0~100, 可选参数, 默认50
		if (0 != m_volume.length()) {
			//LOG_INFO("m_volume :%d", atoi(m_volume.c_str()));
			m_SynthReq->setVolume(atoi(m_volume.c_str()));
		}

		//音频编码格式, 可选参数, 默认是wav. 支持的格式pcm, wav, mp3 .偷懒，只支持pcm，不支持其他格式  
		m_SynthReq->setFormat("pcm"); 

		// 音频采样率, 包含8000, 16000. 可选参数, 默认是16000
		if (0 != m_sample.length()) {
			//LOG_INFO("m_sample :%d", atoi(m_sample.c_str()));
			m_SynthReq->setSampleRate(atoi(m_sample.c_str())); 
		}
		
		// 语速, 范围是-500~500, 可选参数, 默认是0
		if (0 != m_speech.length()) {
			//LOG_INFO("m_speech :%d", atoi(m_speech.c_str()));
			m_SynthReq->setSpeechRate(atoi(m_speech.c_str())); 
		}

		// 语调, 范围是-500~500, 可选参数, 默认是0
		if (0 != m_pitch.length()) {
			//LOG_INFO("m_speech :%d", atoi(m_pitch.c_str()));
			m_SynthReq->setPitchRate(atoi(m_pitch.c_str())); 
		}	

		if (-1 == m_SynthReq->start()) {
			LOG_ERROR("Synth Request Start Failed id :%d", m_id);
			DoDestroySynthRequest();
			return FALSE;
		}
	}
	else {
		LOG_ERROR("Synth Request Init Failed id :%d", m_id);
		return FALSE;
	}

	return TRUE;
}

bool CAliSynthChannel::DoCreateSynthRequest()
{
	if (NULL != m_SynthReq) {
		DoDestroySynthRequest();
	}
	if (NULL == m_SynthReq) {
		m_SynthReq = NlsClient::getInstance()->createSynthesizerRequest();
		m_SynthReq->setOnMetaInfo(CAliSynthChannel::OnMetaInfo, this);
		m_SynthReq->setOnSynthesisCompleted(CAliSynthChannel::OnSynthesisCompleted, this);
		m_SynthReq->setOnBinaryDataReceived(CAliSynthChannel::OnBinaryDataReceived, this);
		m_SynthReq->setOnChannelClosed(CAliSynthChannel::OnChannelClosed, this);
		m_SynthReq->setOnTaskFailed(CAliSynthChannel::OnTaskFailed, this);
		if (NULL == m_SynthReq) {
			LOG_ERROR("Create Synth Request failed Ch id :%d",m_id);
			return FALSE;
		}
	}
	else {
		LOG_ERROR("CreateSynthRequest exist");
	}

	return TRUE;
}

void CAliSynthChannel::DoDestroySynthRequest()
{
	if (m_SynthReq) {
		m_SynthReq->stop();
		NlsClient::getInstance()->releaseSynthesizerRequest(m_SynthReq);
	}
	m_SynthReq = NULL;
	return;
}

void CAliSynthChannel::OnMetaInfo(NlsEvent* ev)
{

}

void CAliSynthChannel::OnSynthesisCompleted(NlsEvent* ev)
{
	if (NULL == m_engineCh)
		return;
	lock();
	m_Runing = FALSE;
	unlock();

	return;
}

void CAliSynthChannel::OnTaskFailed(NlsEvent* ev)
{
	LOG_ERROR("Synth Channel[%d] TaskFailed Error Msg :%s statusCode:%d",m_id, ev->getErrorMessage(), ev->getStatusCode())
	lock();
	m_Start = FALSE;
	m_Runing = FALSE;
	m_Play = FALSE;
	unlock();
	return;
}

void CAliSynthChannel::OnChannelClosed(NlsEvent* ev)
{
	DoDestroySynthRequest();
}

void CAliSynthChannel::OnBinaryDataReceived(NlsEvent* ev)
{
	lock();
	vector<unsigned char> data = ev->getBinaryData();
	if (0 != data.size()) { // 一块数据的最大的大小目前是8000
		m_Voice.push_back(data);
		//recordMain(&data[0], data.size());
	}

	unlock();
}

void CAliSynthChannel::lock()
{
	if (m_lock)
		apr_thread_mutex_lock(m_lock);
	return;
}

void CAliSynthChannel::unlock()
{
	if (m_lock)
		apr_thread_mutex_unlock(m_lock);
	return;
}

void CAliSynthChannel::OnMetaInfo(NlsEvent* ev, void * arg)
{
	CAliSynthChannel * This = static_cast<CAliSynthChannel*>(arg);
	if (This)
		This->OnMetaInfo(ev);
	return;
}

void CAliSynthChannel::OnSynthesisCompleted(NlsEvent* ev, void * arg)
{
	CAliSynthChannel * This = static_cast<CAliSynthChannel*>(arg);
	if (This)
		This->OnSynthesisCompleted(ev);
	return;
}

void CAliSynthChannel::OnTaskFailed(NlsEvent* ev, void * arg)
{
	CAliSynthChannel * This = static_cast<CAliSynthChannel*>(arg);
	if (This)
		This->OnTaskFailed(ev);
	return;
}

void CAliSynthChannel::OnChannelClosed(NlsEvent* ev, void * arg)
{
	CAliSynthChannel * This = static_cast<CAliSynthChannel*>(arg);
	if (This)
		This->OnChannelClosed(ev);
	return;
}

void CAliSynthChannel::OnBinaryDataReceived(NlsEvent* ev, void * arg)
{
	CAliSynthChannel * This = static_cast<CAliSynthChannel*>(arg);
	if (This)
		This->OnBinaryDataReceived(ev);
	return;
}

