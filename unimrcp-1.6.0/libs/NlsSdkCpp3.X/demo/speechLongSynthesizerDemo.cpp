/*
 * Copyright 2015 Alibaba Group Holding Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>
#include <ctime>
#include <string>
#include <vector>
#include <fstream>
#include "nlsClient.h"
#include "nlsEvent.h"
#include "speechSynthesizerRequest.h"
#include "nlsCommonSdk/Token.h"
using namespace AlibabaNlsCommon;
using AlibabaNls::NlsClient;
using AlibabaNls::NlsEvent;
using AlibabaNls::LogDebug;
using AlibabaNls::LogInfo;
using AlibabaNls::SpeechSynthesizerRequest;

// 自定义线程参数
struct ParamStruct {
	std::string text;
	std::string token;
	std::string appkey;
	std::string audioFile;
};

// 自定义事件回调参数
struct ParamCallBack {
	std::string binAudioFile;
	std::ofstream audioFile;
	uint64_t startMs;
};

//全局维护一个服务鉴权token和其对应的有效期时间戳，
//每次调用服务之前，首先判断token是否已经过期，
//如果已经过期，则根据AccessKey ID和AccessKey Secret重新生成一个token，并更新这个全局的token和其有效期时间戳。
//注意：不要每次调用服务之前都重新生成新token，只需在token即将过期时重新生成即可。所有的服务并发可共用一个token。
std::string g_akId = "";
std::string g_akSecret = "";
std::string g_token = "";
long g_expireTime = -1;

uint64_t getNow() {
	struct timeval now;
	gettimeofday(&now, NULL);
	return now.tv_sec * 1000 * 1000 + now.tv_usec;
}

// 根据AccessKey ID和AccessKey Secret重新生成一个token，并获取其有效期时间戳
// token使用规则：在有效期到期前可以一直使用，且可以多个进程/多个线程/多个应用使用均可，建议在有效期快到期时提起申请新的token
int generateToken(std::string akId, std::string akSecret, std::string* token, long* expireTime) {
    NlsToken nlsTokenRequest;
    nlsTokenRequest.setAccessKeyId(akId);
    nlsTokenRequest.setKeySecret(akSecret);

    if (-1 == nlsTokenRequest.applyNlsToken()) {
        // 获取失败原因
        printf("generateToken Failed: %s\n", nlsTokenRequest.getErrorMsg());
        return -1;
    }

    *token = nlsTokenRequest.getToken();
    *expireTime = nlsTokenRequest.getExpireTime();
    return 0;
}

//@brief sdk在接收到云端返回合成结束消息时, sdk内部线程上报Completed事件
//@note 上报Completed事件之后，SDK内部会关闭识别连接通道.
//@param cbEvent 回调事件结构, 详见nlsEvent.h
//@param cbParam 回调自定义参数，默认为NULL, 可以根据需求自定义参数
void OnSynthesisCompleted(NlsEvent* cbEvent, void* cbParam) {
    ParamCallBack* tmpParam = (ParamCallBack*)cbParam;
    // 演示如何打印/使用用户自定义参数示例
    printf("OnSynthesisCompleted: %s\n", tmpParam->binAudioFile.c_str());
    // 获取消息的状态码，成功为0或者20000000，失败时对应失败的错误码
    // 当前任务的task id，方便定位问题，建议输出，特别提醒该taskid非常重要，是和服务端交互的唯一标识，因此建议在实际使用时建议输出该taskid
    printf("OnSynthesisCompleted: status code=%d, task id=%s\n", cbEvent->getStatusCode(), cbEvent->getTaskId());
    // 获取服务端返回的全部信息
    //printf("OnSynthesisCompleted: all response=%s\n", cbEvent->getAllResponse());
}

//@brief 合成过程发生异常时, sdk内部线程上报TaskFailed事件
//@note 上报TaskFailed事件之后，SDK内部会关闭识别连接通道.
//@param cbEvent 回调事件结构, 详见nlsEvent.h
//@param cbParam 回调自定义参数，默认为NULL, 可以根据需求自定义参数
void OnSynthesisTaskFailed(NlsEvent* cbEvent, void* cbParam) {
	ParamCallBack* tmpParam = (ParamCallBack*)cbParam;
    // 演示如何打印/使用用户自定义参数示例
    printf("OnSynthesisTaskFailed: %s\n", tmpParam->binAudioFile.c_str());
    // 当前任务的task id，方便定位问题，建议输出，特别提醒该taskid非常重要，是和服务端交互的唯一标识，因此建议在实际使用时建议输出该taskid
    printf("OnSynthesisTaskFailed: status code=%d, task id=%s, error message=%s\n", cbEvent->getStatusCode(), cbEvent->getTaskId(), cbEvent->getErrorMessage());
}

//@brief 识别结束或发生异常时，会关闭连接通道, sdk内部线程上报ChannelCloseed事件
//@param cbEvent 回调事件结构, 详见nlsEvent.h
//@param cbParam 回调自定义参数，默认为NULL, 可以根据需求自定义参数
void OnSynthesisChannelClosed(NlsEvent* cbEvent, void* cbParam) {
	ParamCallBack* tmpParam = (ParamCallBack*)cbParam;
    // 演示如何打印/使用用户自定义参数示例
    printf("OnSynthesisChannelClosed: %s\n", tmpParam->binAudioFile.c_str());
	printf("OnSynthesisChannelClosed: %s\n", cbEvent->getAllResponse());
	tmpParam->audioFile.close();
	delete tmpParam; //识别流程结束,释放回调参数
}

//@brief 文本上报服务端之后, 收到服务端返回的二进制音频数据, SDK内部线程通过BinaryDataRecved事件上报给用户
//@param cbEvent 回调事件结构, 详见nlsEvent.h
//@param cbParam 回调自定义参数，默认为NULL, 可以根据需求自定义参数
void OnBinaryDataRecved(NlsEvent* cbEvent, void* cbParam) {
	ParamCallBack* tmpParam = (ParamCallBack*)cbParam;
	if(tmpParam->startMs > 0 ) {
		// 重要提示： 一旦能够获取到语音流，比如第一次从服务端返回合成语音流，即可以开始使用了，比如播放或者其他处理，这里简单示例为保存到本地文件
		// 第一次收到语音流数据，计算TTS合成首包延迟。 另外此处计算首包延迟时也包括了start操作(即本程序连接公共云服务端的时间)，而该时间受不同网络因素影响可能有较大差异
		uint64_t now = getNow();
		printf("first latency = %lld ms, task id = %s\n", (now - tmpParam->startMs) / 1000, cbEvent->getTaskId());
		tmpParam->startMs = 0;
	}
	// 演示如何打印/使用用户自定义参数示例
    printf("OnBinaryDataRecved: %s\n", tmpParam->binAudioFile.c_str());
	const std::vector<unsigned char>& data = cbEvent->getBinaryData(); // getBinaryData() 获取文本合成的二进制音频数据
    printf("OnBinaryDataRecved: status code=%d, task id=%s, data size=%d\n", cbEvent->getStatusCode(), cbEvent->getTaskId(), data.size());
    // 以追加形式将二进制音频数据写入文件
	if (data.size() > 0) {
		tmpParam->audioFile.write((char*)&data[0], data.size());
	}
}

//@brief 返回 tts 文本对应的日志信息，增量返回对应的字幕信息
//@param cbEvent 回调事件结构, 详见nlsEvent.h
//@param cbParam 回调自定义参数，默认为NULL, 可以根据需求自定义参数
void OnMetaInfo(NlsEvent* cbEvent, void* cbParam) {
	ParamCallBack* tmpParam = (ParamCallBack*)cbParam;
	// 演示如何打印/使用用户自定义参数示例
    printf("OnBinaryDataRecved: %s\n", tmpParam->binAudioFile.c_str());
    printf("OnMetaInfo: task id=%s, respose=%s\n", cbEvent->getTaskId(), cbEvent->getAllResponse());
}

// 工作线程
void* pthreadFunc(void* arg) {
	// 0: 从自定义线程参数中获取token, 配置文件等参数.
	ParamStruct* tst = (ParamStruct*)arg;
	if (tst == NULL) {
		printf("arg is not valid\n");
		return NULL;
	}

	// 1: 初始化自定义回调参数
	ParamCallBack* cbParam = new ParamCallBack;
	cbParam->binAudioFile = tst->audioFile;
	cbParam->audioFile.open(cbParam->binAudioFile.c_str(), std::ios::binary | std::ios::out);

	// 2: 创建语音识别SpeechSynthesizerRequest对象
	SpeechSynthesizerRequest* request = NlsClient::getInstance()->createSynthesizerRequest(AlibabaNls::LongTts);
	if (request == NULL) {
		printf("createSynthesizerRequest failed.\n");
		cbParam->audioFile.close();
		return NULL;
	}

	request->setOnSynthesisCompleted(OnSynthesisCompleted, cbParam); // 设置音频合成结束回调函数
	request->setOnChannelClosed(OnSynthesisChannelClosed, cbParam); // 设置音频合成通道关闭回调函数
	request->setOnTaskFailed(OnSynthesisTaskFailed, cbParam); // 设置异常失败回调函数
	request->setOnBinaryDataReceived(OnBinaryDataRecved, cbParam); // 设置文本音频数据接收回调函数
	request->setOnMetaInfo(OnMetaInfo, cbParam); // 设置字幕信息

	request->setAppKey(tst->appkey.c_str());
	request->setText(tst->text.c_str()); // 设置待合成文本, 必填参数. 文本内容必须为UTF-8编码
    request->setVoice("siqi"); 			 // 发音人, 包含"xiaoyun", "ruoxi", "xiaogang"等. 可选参数, 默认是xiaoyun，具体可用发音人可以参考文档介绍
    request->setVolume(50); 			 // 音量, 范围是0~100, 可选参数, 默认50
    request->setFormat("wav");			 // 音频编码格式, 可选参数, 默认是wav. 支持的格式pcm, wav, mp3
    request->setSampleRate(8000); 		 // 音频采样率, 包含8000, 16000. 可选参数, 默认是16000
    request->setSpeechRate(0); 			 // 语速, 范围是-500~500, 可选参数, 默认是0
    request->setPitchRate(0); 			 // 语调, 范围是-500~500, 可选参数, 默认是0
	//request->setEnableSubtitle(true); 	 //是否开启字幕，非必须，需要注意的是并不是所有发音人都支持字幕功能
	request->setToken(tst->token.c_str()); // 设置账号校验token, 必填参数

	cbParam->startMs = getNow();
	// 3: start()为异步操作。成功返回BinaryRecv事件。失败返回TaskFailed事件。
	if (request->start() < 0) {
		printf("start() failed. may be can not connect server. please check network or firewalld\n");
		NlsClient::getInstance()->releaseSynthesizerRequest(request); // start()失败，释放request对象
		cbParam->audioFile.close();
		return NULL;
	}

	//6: 通知云端数据发送结束.
	//stop()为异步操作.失败返回TaskFailed事件。
	request->stop();

	// 7: 识别结束, 释放request对象
	NlsClient::getInstance()->releaseSynthesizerRequest(request);
	return NULL;
}

// 合成单个文本数据
int speechLongSynthesizerFile(const char* appkey) {
	//获取当前系统时间戳，判断token是否过期
    std::time_t curTime = std::time(0);
    if (g_expireTime - curTime < 10) {
		printf("the token will be expired, please generate new token by AccessKey-ID and AccessKey-Secret.\n");
        if (-1 == generateToken(g_akId, g_akSecret, &g_token, &g_expireTime)) {
            return -1;
        }
    }

	ParamStruct pa;
	pa.token = g_token;
    pa.appkey = appkey;

	// 注意: Windows平台下，合成文本中如果包含中文，请将本CPP文件设置为带签名的UTF-8编码或者GB2312编码
	pa.text = "今天天气很棒，适合去户外旅行.";
    pa.audioFile = "syAudio.wav";

	pthread_t pthreadId;
	// 启动一个工作线程, 用于识别
	pthread_create(&pthreadId, NULL, &pthreadFunc, (void *)&pa);
	pthread_join(pthreadId, NULL);
	return 0;
}

// 合成多个文本数据;
// sdk多线程指一个文本数据对应一个线程, 非一个文本数据对应多个线程.
// 示例代码为同时开启2个线程合成2个文件;
#define AUDIO_TEXT_NUMS 2
#define AUDIO_TEXT_LENGTH 64
#define AUDIO_FILE_NAME_LENGTH 32
int speechLongSynthesizerMultFile(const char* appkey) {
	//获取当前系统时间戳，判断token是否过期
    std::time_t curTime = std::time(0);
    if (g_expireTime - curTime < 10) {
		printf("the token will be expired, please generate new token by AccessKey-ID and AccessKey-Secret.\n");
        if (-1 == generateToken(g_akId, g_akSecret, &g_token, &g_expireTime)) {
            return -1;
        }
    }

    const char syAudioFiles[AUDIO_TEXT_NUMS][AUDIO_FILE_NAME_LENGTH] = {"syAudio0.wav", "syAudio1.wav"};
	const char texts[AUDIO_TEXT_NUMS][AUDIO_TEXT_LENGTH] = {"今日天气真不错，我想去操作踢足球.", "明天有大暴雨，还是宅在家里看电影吧."};
	ParamStruct pa[AUDIO_TEXT_NUMS];

	for (int i = 0; i < AUDIO_TEXT_NUMS; i ++) {
		pa[i].token = g_token;
        pa[i].appkey = appkey;
		pa[i].text = texts[i];
        pa[i].audioFile = syAudioFiles[i];
	}

	std::vector<pthread_t> pthreadId(AUDIO_TEXT_NUMS);
	// 启动工作线程, 同时识别音频文件
	for (int j = 0; j < AUDIO_TEXT_NUMS; j++) {
		pthread_create(&pthreadId[j], NULL, &pthreadFunc, (void *)&(pa[j]));
	}
	for (int j = 0; j < AUDIO_TEXT_NUMS; j++) {
		pthread_join(pthreadId[j], NULL);
	}
	return 0;
}

int main(int arc, char* argv[]) {
    if (arc < 4) {
		printf("params is not valid. Usage: ./demo <your appkey> <your AccessKey ID> <your AccessKey Secret>\n");
        return -1;
    }

    std::string appkey = argv[1];
    g_akId = argv[2];
    g_akSecret = argv[3];

	// 根据需要设置SDK输出日志, 可选. 此处表示SDK日志输出至log-Synthesizer.txt， LogDebug表示输出所有级别日志
	int ret = NlsClient::getInstance()->setLogConfig("log-longsynthesizer", LogDebug);
	if (-1 == ret) {
		printf("set log failed\n");
		return -1;
	}

	//启动工作线程
	NlsClient::getInstance()->startWorkThread(4);

	/// 重要提醒： 长文本语音合成目前没有免费试用版，必须开通商业版长文本语音合成之后才能进行测试
	// 合成单个文本
	speechLongSynthesizerFile(appkey.c_str());

	// 合成多个文本
	// speechLongSynthesizerMultFile(appkey.c_str());

	// 所有工作完成，进程退出前，释放nlsClient. 请注意, releaseInstance()非线程安全.
	NlsClient::releaseInstance();
	return 0;
}