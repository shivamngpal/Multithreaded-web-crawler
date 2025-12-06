#ifndef SAFE_QUEUE_H
#define SAFE_QUEUE_H

#include<queue>
#include<mutex>
#include<condition_variable>

// thread safe queue
// using mutex,locks,cv

// generic template of data type
// as this queue can have any type of data
template<typename T>
class SafeQueue{
    private:
        std::queue<T> q;
        std::mutex mtx;
        std::condition_variable cv;

    public:
        SafeQueue(){}

        void push(T value){
            std::lock_guard<std::mutex> lock(mtx);
            q.push(value);
            cv.notify_one();
        }
        bool pop(T& result){
            std::unique_lock<std::mutex> lock(mtx);
            // wait until queue has elements
            cv.wait(lock, [this]{return !q.empty();});
            result = q.front();
            q.pop();
            return true;
        }
        
        bool empty(){
            std::lock_guard<std::mutex> lock(mtx);
            return q.empty();
        }
};

#endif