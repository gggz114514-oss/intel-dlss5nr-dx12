#include <windows.h>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <string>
#include <chrono>
#include <cstdio>
struct AsyncStatusWriter {
    std::string path, pending;
    std::mutex mutex;
    std::condition_variable changed;
    bool dirty=false, stop=false;
    std::thread worker;
    explicit AsyncStatusWriter(const char* p):path(p),worker([this]{run();}){}
    void publish(std::string text){
        {std::lock_guard<std::mutex> lock(mutex);pending=std::move(text);dirty=true;}
        changed.notify_one();
    }
    void run(){
        std::unique_lock<std::mutex> lock(mutex);
        for(;;){
            changed.wait(lock,[this]{return dirty||stop;});
            if(!dirty&&stop)break;
            auto text=std::move(pending);dirty=false;
            lock.unlock();
            FILE* f=nullptr;
            if(!fopen_s(&f,path.c_str(),"w")&&f){fwrite(text.data(),1,text.size(),f);fclose(f);}
            lock.lock();
            changed.wait_for(lock,std::chrono::milliseconds(250),[this]{return stop;});
        }
    }
    ~AsyncStatusWriter(){
        {std::lock_guard<std::mutex> lock(mutex);stop=true;}
        changed.notify_one();worker.join();
    }
};

int main(){SetErrorMode(SEM_NOGPFAULTERRORBOX);{AsyncStatusWriter writer("full-crt-status-writer.txt");writer.publish("compatibility pass\n");}puts("FULL STATUS WRITER OLD-CRT PASS");return 0;}
