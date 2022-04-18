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
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <ctime>
#include <string>
#include <iostream>
#include <vector>
#include <fstream>
#include <sys/time.h>
#include "nlsClient.h"
#include "nlsEvent.h"
#include "speechRecognizerRequest.h"
#include "nlsCommonSdk/Token.h"

#define FRAME_SIZE 3200
#define SAMPLE_RATE 16000

using namespace AlibabaNlsCommon;
using AlibabaNls::NlsClient;
using AlibabaNls::NlsEvent;
using AlibabaNls::LogDebug;
using AlibabaNls::LogInfo;
using AlibabaNls::LogError;
using AlibabaNls::SpeechRecognizerRequest;

// 自定义线程参数
struct ParamStruct {
    std::string fileName;
    std::string appkey;
    std::string token;
};

// 自定义事件回调参数
struct ParamCallBack {
    int userId;
    char userInfo[8];
};

//全局维护一个服务鉴权token和其对应的有效期时间戳，
//每次调用服务之前，首先判断token是否已经过期，
//如果已经过期，则根据AccessKey ID和AccessKey Secret重新生成一个token，并更新这个全局的token和其有效期时间戳。
//注意：不要每次调用服务之前都重新生成新token，只需在token即将过期时重新生成即可。所有的服务并发可共用一个token。
std::string g_akId = "";
std::string g_akSecret = "";
std::string g_token = "";
long g_expireTime = -1;

struct timeval tv;
struct timeval tv1;

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

//@brief 获取sendAudio发送延时时间
//@param dataSize 待发送数据大小
//@param sampleRate 采样率 16k/8K
//@param compressRate 数据压缩率，例如压缩比为10:1的16k opus编码，此时为10；非压缩数据则为1
//@return 返回sendAudio之后需要sleep的时间
//@note 对于8k pcm 编码数据, 16位采样，建议每发送1600字节 sleep 100 ms.
// 对于16k pcm 编码数据, 16位采样，建议每发送3200字节 sleep 100 ms.
// 对于其它编码格式的数据, 用户根据压缩比, 自行估算, 比如压缩比为10:1的 16k opus,
// 需要每发送3200/10=320 sleep 100ms.
unsigned int getSendAudioSleepTime(int dataSize, int sampleRate, int compressRate) {
    // 仅支持16位采样
    const int sampleBytes = 16;
    // 仅支持单通道
    const int soundChannel = 1;

    // 当前采样率，采样位数下每秒采样数据的大小
    int bytes = (sampleRate * sampleBytes * soundChannel) / 8;
    // 当前采样率，采样位数下每毫秒采样数据的大小
    int bytesMs = bytes / 1000;
    // 待发送数据大小除以每毫秒采样数据大小，以获取sleep时间
    int sleepMs = (dataSize * compressRate) / bytesMs;
    return sleepMs;
}

//@brief 调用start(), 成功与云端建立连接, sdk内部线程上报started事件
//@param cbEvent 回调事件结构, 详见nlsEvent.h
//@param cbParam 回调自定义参数，默认为NULL, 可以根据需求自定义参数
void OnRecognitionStarted(NlsEvent* cbEvent, void* cbParam) {
	ParamCallBack* tmpParam = (ParamCallBack*)cbParam;
    // 演示如何打印/使用用户自定义参数示例
    printf("OnRecognitionStarted: %d, %s\n", tmpParam->userId, tmpParam->userInfo);
    // 获取消息的状态码，成功为0或者20000000，失败时对应失败的错误码
    // 当前任务的task id，方便定位问题，建议输出，特别提醒该taskid非常重要，是和服务端交互的唯一标识，因此建议在实际使用时建议输出该taskid
    printf("OnRecognitionStarted: status code=%d, task id=%s\n", cbEvent->getStatusCode(), cbEvent->getTaskId());
    // 获取服务端返回的全部信息
    //printf("OnRecognitionStarted: all response=%s\n", cbEvent->getAllResponse());
}


//@brief 设置允许返回中间结果参数, sdk在接收到云端返回到中间结果时, sdk内部线程上报ResultChanged事件
//@param cbEvent 回调事件结构, 详见nlsEvent.h
//@param cbParam 回调自定义参数，默认为NULL, 可以根据需求自定义参数
void OnRecognitionResultChanged(NlsEvent* cbEvent, void* cbParam) {
	ParamCallBack* tmpParam = (ParamCallBack*)cbParam;
    // 演示如何打印/使用用户自定义参数示例
    printf("OnRecognitionResultChanged: %d, %s\n", tmpParam->userId, tmpParam->userInfo);
    // 当前任务的task id，方便定位问题，建议输出，特别提醒该taskid非常重要，是和服务端交互的唯一标识，因此建议在实际使用时建议输出该taskid
    printf("OnRecognitionResultChanged: status code=%d, task id=%s, result=%s\n", cbEvent->getStatusCode(), cbEvent->getTaskId(), cbEvent->getResult());
    // 获取服务端返回的全部信息
    //printf("OnRecognitionResultChanged: response=%s\n", cbEvent->getAllResponse());
}

//@brief sdk在接收到云端返回识别结束消息时, sdk内部线程上报Completed事件
//@note 上报Completed事件之后, SDK内部会关闭识别连接通道. 此时调用sendAudio会返回-1, 请停止发送.
//@param cbEvent 回调事件结构, 详见nlsEvent.h
//@param cbParam 回调自定义参数，默认为NULL, 可以根据需求自定义参数
void OnRecognitionCompleted(NlsEvent* cbEvent, void* cbParam) {
	ParamCallBack* tmpParam = (ParamCallBack*)cbParam;
    // 演示如何打印/使用用户自定义参数示例
    printf("OnRecognitionCompleted: %d, %s\n", tmpParam->userId, tmpParam->userInfo);

    // 当前任务的task id，方便定位问题，建议输出，特别提醒该taskid非常重要，是和服务端交互的唯一标识，因此建议在实际使用时建议输出该taskid  
    printf("OnRecognitionCompleted: status code=%d, task id=%s, result=%s\n", cbEvent->getStatusCode(), cbEvent->getTaskId(), cbEvent->getResult());
    // 获取服务端返回的全部信息
    //printf("OnRecognitionCompleted: response=%s\n", cbEvent->getAllResponse());
}

//@brief 识别过程发生异常时, sdk内部线程上报TaskFailed事件
//@note 上报TaskFailed事件之后, SDK内部会关闭识别连接通道. 此时调用sendAudio会返回-1, 请停止发送.
//@param cbEvent 回调事件结构, 详见nlsEvent.h
//@param cbParam 回调自定义参数，默认为NULL, 可以根据需求自定义参数
//@return
void OnRecognitionTaskFailed(NlsEvent* cbEvent, void* cbParam) {
	ParamCallBack* tmpParam = (ParamCallBack*)cbParam;
    // 演示如何打印/使用用户自定义参数示例
    printf("OnRecognitionTaskFailed: %d, %s\n", tmpParam->userId, tmpParam->userInfo);

    // 当前任务的task id，方便定位问题，建议输出，特别提醒该taskid非常重要，是和服务端交互的唯一标识，因此建议在实际使用时建议输出该taskid  
    printf("OnRecognitionTaskFailed: status code=%d, task id=%s, error message=%s\n", cbEvent->getStatusCode(), cbEvent->getTaskId(), cbEvent->getErrorMessage());
    // 获取服务端返回的全部信息
    //printf("OnRecognitionTaskFailed: response=%s\n", cbEvent->getAllResponse());
}

//@brief 识别结束或发生异常时，会关闭连接通道, sdk内部线程上报ChannelCloseed事件
//@param cbEvent 回调事件结构, 详见nlsEvent.h
//@param cbParam 回调自定义参数，默认为NULL, 可以根据需求自定义参数
void OnRecognitionChannelClosed(NlsEvent* cbEvent, void* cbParam) {
	ParamCallBack* tmpParam = (ParamCallBack*)cbParam;
    // 演示如何打印/使用用户自定义参数示例
    printf("OnRecognitionChannelClosed: %d, %s\n", tmpParam->userId, tmpParam->userInfo);
    // 获取服务端返回的全部信息
    printf("OnRecognitionChannelClosed: response=%s\n", cbEvent->getAllResponse());
    delete tmpParam; //识别流程结束,释放回调参数
}

void* pthreadFunction(void* arg) {
    int sleepMs = 0;
    ParamCallBack *cbParam = NULL;

    //初始化自定义回调参数, 以下两变量仅作为示例表示参数传递, 在demo中不起任何作用
    //回调参数在堆中分配之后, SDK在销毁requesr对象时会一并销毁, 外界无需在释放
    cbParam = new ParamCallBack;
    cbParam->userId = rand() % 100;
    strcpy(cbParam->userInfo, "User.");

    // 0: 从自定义线程参数中获取token, 配置文件等参数.
    ParamStruct *tst = (ParamStruct *) arg;
    if (tst == NULL) {
        printf("arg is not valid\n");
        return NULL;
    }

    // 打开音频文件, 获取数据
    std::ifstream fs;
    fs.open(tst->fileName.c_str(), std::ios::binary | std::ios::in);
    if (!fs) {
        printf("%s isn't exist..\n", tst->fileName.c_str());
        return NULL;
    }

    //1: 创建一句话识别SpeechRecognizerRequest对象
    SpeechRecognizerRequest *request = NlsClient::getInstance()->createRecognizerRequest();
    if (request == NULL) {
        printf("createRecognizerRequest failed\n");
        return NULL;
    }

    request->setOnRecognitionStarted(OnRecognitionStarted, cbParam);        // 设置start()成功回调函数
    request->setOnTaskFailed(OnRecognitionTaskFailed, cbParam);             // 设置异常识别回调函数
    request->setOnChannelClosed(OnRecognitionChannelClosed, cbParam);       // 设置识别通道关闭回调函数
    request->setOnRecognitionResultChanged(OnRecognitionResultChanged, cbParam); // 设置中间结果回调函数
    request->setOnRecognitionCompleted(OnRecognitionCompleted, cbParam);    // 设置识别结束回调函数

    request->setAppKey(tst->appkey.c_str());        // 设置AppKey, 必填参数, 请参照官网申请
    request->setFormat("pcm");                      // 设置音频数据编码格式, 可选参数, 目前支持pcm, opus. 默认是pcm
    request->setSampleRate(SAMPLE_RATE);            // 设置音频数据采样率, 可选参数, 目前支持16000, 8000. 默认是16000
    request->setIntermediateResult(true);           // 设置是否返回中间识别结果, 可选参数. 默认false
    request->setPunctuationPrediction(true);        // 设置是否在后处理中添加标点, 可选参数. 默认false
    request->setInverseTextNormalization(true);     // 设置是否在后处理中执行ITN, 可选参数. 默认false
    //request->setEnableVoiceDetection(true);       //是否启动语音检测, 可选, 默认是False
    //允许的最大开始静音, 可选, 单位是毫秒, 超出后服务端将会发送RecognitionCompleted事件, 结束本次识别. 注意: 需要先设置enable_voice_detection为true
    //request->setMaxStartSilence(5000);
    //允许的最大结束静音, 可选, 单位是毫秒, 超出后服务端将会发送RecognitionCompleted事件, 结束本次识别. 注意: 需要先设置enable_voice_detection为true
    //request->setMaxEndSilence(800);
    //request->setCustomizationId("TestId_123"); //定制语言模型id, 可选.
    //request->setVocabularyId("TestId_456"); //定制泛热词id, 可选.
    // 用于传递某些定制化、高级参数设置，参数格式为json格式： {"key": "value"}
    //request->setPayloadParam("{\"vad_model\": \"farfield\"}");

    request->setToken(tst->token.c_str()); // 设置账号校验token, 必填参数

    // 2: start()为异步操作。成功返回started事件。失败返回TaskFailed事件。
    if (request->start() < 0) {
        printf("start() failed. may be can not connect server. please check network or firewalld\n");
        NlsClient::getInstance()->releaseRecognizerRequest(request); // start()失败，释放request对象
        return NULL;
    }

    while (!fs.eof()) {
        uint8_t data[FRAME_SIZE] = {0};
        fs.read((char *) data, sizeof(uint8_t) * FRAME_SIZE);
        size_t nlen = fs.gcount();
        if (nlen <= 0) {
            continue;
        }

        // 3: 发送音频数据. sendAudio为异步操作, 返回-1表示发送失败, 需要停止发送.
        int ret = request->sendAudio(data, nlen);
        if (ret < 0) {
            // 发送失败, 退出循环数据发送
            printf("send data fail.\n");
            break;
        }

        // 语音数据发送控制：
        // 语音数据是实时的, 不用sleep控制速率, 直接发送即可.
        // 语音数据来自文件（也即本示例代码模拟的语音流发送机制）, 发送时需要控制速率, 使单位时间内发送的数据大小接近单位时间原始语音数据存储的大小.
        sleepMs = getSendAudioSleepTime(nlen, SAMPLE_RATE, 1); // 根据 发送数据大小，采样率，数据压缩比 来获取sleep时间
        // 4: 语音数据发送延时控制
        usleep(sleepMs * 1000);
    }
    printf("sendAudio done.\n");

    //5: 关闭音频文件
    fs.close();

    //6: 通知云端数据发送结束.
    //stop()为异步操作.失败返回TaskFailed事件。
    request->stop();

    //7: 通知SDK释放request.
    NlsClient::getInstance()->releaseRecognizerRequest(request);
	return NULL;
}

//线程循环识别
//需要调整count值和每次要识别的文件，Demo中默认每次识别一个文件
void* multiRecognize(void* arg) {
	int count = 2;
	while (count > 0) {
        pthreadFunction(arg);
		count--;
	}
	return NULL;
}

// 识别单个音频数据
int speechRecognizerFile(const char* appkey) {
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
    pa.fileName = "test0.wav";

    pthread_t pthreadId;
    // 启动一个工作线程, 用于单次识别
    pthread_create(&pthreadId, NULL, &pthreadFunction, (void *)&pa);
	// 启动一个工作线程, 用于循环识别
	// pthread_create(&pthreadId, NULL, &multiRecognize, (void *)&pa);
    pthread_join(pthreadId, NULL);
	return 0;
}

 //识别多个音频数据;
 //sdk多线程指一个音频数据源对应一个线程, 非一个音频数据对应多个线程.
 //示例代码为同时开启2个线程识别2个文件;
 //免费用户并发连接不能超过2个;
#define AUDIO_FILE_NUMS 2
#define AUDIO_FILE_NAME_LENGTH 32
int speechRecognizerMultFile(const char* appkey) {
    //获取当前系统时间戳，判断token是否过期
    std::time_t curTime = std::time(0);
    if (g_expireTime - curTime < 10) {
		printf("the token will be expired, please generate new token by AccessKey-ID and AccessKey-Secret.\n");
        if (-1 == generateToken(g_akId, g_akSecret, &g_token, &g_expireTime)) {
            return -1;
        }
    }

    char audioFileNames[AUDIO_FILE_NUMS][AUDIO_FILE_NAME_LENGTH] = {"test0.wav", "test1.wav"};
    ParamStruct pa[AUDIO_FILE_NUMS];

    for (int i = 0; i < AUDIO_FILE_NUMS; i ++) {
        pa[i].token = g_token;
        pa[i].appkey = appkey;
        pa[i].fileName = audioFileNames[i];
    }

    std::vector<pthread_t> pthreadId(AUDIO_FILE_NUMS);
    // 启动四个工作线程, 同时识别四个音频文件
    for (int j = 0; j < AUDIO_FILE_NUMS; j++) {
        pthread_create(&pthreadId[j], NULL, &pthreadFunction, (void *)&(pa[j]));
    }
    for (int j = 0; j < AUDIO_FILE_NUMS; j++) {
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

    // 根据需要设置SDK输出日志, 可选. 此处表示SDK日志输出至log-recognizer.txt， LogDebug表示输出所有级别日志，支持LogInfo、LogWarning、LogError， 400表示单个文件400MB
    int ret = NlsClient::getInstance()->setLogConfig("log-recognizer", LogDebug, 400);
    if (-1 == ret) {
        printf("set log failed.\n");
        return -1;
    }

    //启动工作线程
    NlsClient::getInstance()->startWorkThread(4);

    // 识别单个音频数据
    speechRecognizerFile(appkey.c_str());

    // 并发识别多个音频数据
    //speechRecognizerMultFile(appkey.c_str());

    // 所有工作完成，进程退出前，释放nlsClient. 请注意, releaseInstance()非线程安全.
    NlsClient::releaseInstance();
	return 0;
}
