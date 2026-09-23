#pragma once
#ifdef _XBOX
#include <xtl.h>
#else
#include <windows.h>
#endif
#include <string>

namespace ShipyardSpeech {
struct Job { std::string text,language;LONG generation;Job():generation(0){} };
class Queue {
    CRITICAL_SECTION lock_;HANDLE changed_;Job pending_;std::string defaultLanguage_;bool hasPending_,stopping_;volatile LONG generation_;
    Queue(const Queue&);Queue& operator=(const Queue&);
public:
    explicit Queue(const char* defaultLanguage):changed_(NULL),defaultLanguage_(defaultLanguage?defaultLanguage:""),hasPending_(false),stopping_(false),generation_(0){InitializeCriticalSection(&lock_);changed_=CreateEvent(NULL,FALSE,FALSE,NULL);}
    ~Queue(){if(changed_)CloseHandle(changed_);DeleteCriticalSection(&lock_);}
    bool Valid() const{return changed_!=NULL;}
    void Submit(const char* text,const char* language){
        Job next;next.text=text?text:"";next.language=language?std::string(language):defaultLanguage_;
        EnterCriticalSection(&lock_);
        if(!stopping_){pending_.text.swap(next.text);pending_.language.swap(next.language);pending_.generation=InterlockedIncrement(&generation_);hasPending_=true;SetEvent(changed_);}
        LeaveCriticalSection(&lock_);
    }
    bool Take(Job& job){
        for(;;){EnterCriticalSection(&lock_);
            if(stopping_){LeaveCriticalSection(&lock_);return false;}
            if(hasPending_){job.text.swap(pending_.text);job.language.swap(pending_.language);job.generation=pending_.generation;hasPending_=false;LeaveCriticalSection(&lock_);return true;}
            LeaveCriticalSection(&lock_);if(WaitForSingleObject(changed_,INFINITE)!=WAIT_OBJECT_0)return false;
        }
    }
    bool Current(LONG generation){return InterlockedCompareExchange(&generation_,0,0)==generation;}
    void Stop(){EnterCriticalSection(&lock_);stopping_=true;hasPending_=false;InterlockedIncrement(&generation_);if(changed_)SetEvent(changed_);LeaveCriticalSection(&lock_);}
};
}
