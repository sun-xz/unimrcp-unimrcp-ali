#ifndef __RECOGENGINE_H__
#define __RECOGENGINE_H__

//#include"Ali_define.h"
#include"mrcp_recog_engine.h"
//#include"apr.h"
//#include"apt.h"

template<class Channel>
class CRecogEngine {

public:
	CRecogEngine() {};
	virtual ~CRecogEngine() {};
private:
	CRecogEngine(CRecogEngine&);
	CRecogEngine& operator=(CRecogEngine&);
public:
	bool virtual	EngineInit(mrcp_engine_t* engine)=0;
	void virtual	EngineUinit() = 0;
	bool virtual	EngineRecogStart(mrcp_message_t* request, Channel * pCh) = 0;
	int  virtual	EngineWriteFrame(Channel * pCh,const mpf_frame_t *frame) = 0;
	bool virtual	EngineRecogStop(Channel * pCh) = 0;
public:
	static CRecogEngine<Channel>* recogEngine;
};
template<class Channel>
CRecogEngine<Channel>* CRecogEngine<Channel>::recogEngine = NULL;

#endif // !__RECOGENGINE_H__

