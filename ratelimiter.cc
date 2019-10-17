#include "ratelimiter.h"
#include <ts/ts.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include "ink_inet.h"

int RateLimiter::addCounter(float max_rate, uint64_t milliseconds) {
    LimiterEntry * entry = new LimiterEntry(max_rate, milliseconds);
    counters_.push_back(entry);
    return counters_.size() -1;
}

LimiterEntry * RateLimiter::getCounter(int index) {
    return counters_[index];
}

float * RateLimiter::getCounterArray() {
    size_t size = counters_.size() * sizeof(float);
    float *a = (float *) malloc(size);
    memset(a, 0, size);
    for (size_t i = 0; i < counters_.size(); i++) {
        a[i] = counters_[i]->max_rate();
    }
    return a;
}

timeval * RateLimiter::getTimevalArray(timeval time) {
    size_t size = counters_.size() * sizeof(timeval);
    timeval *a = (timeval *)malloc(size);
    memset(a,0,size);
    for (size_t i = 0; i < counters_.size(); i++) {
        a[i] = time;
    }
    return a;
}

LimiterState * RateLimiter::registerLimiter() {
    timeval now;
    gettimeofday(&now, NULL);
    LimiterState * state = new LimiterState(getCounterArray(),getTimevalArray(now));
    return state;
}

uint64_t RateLimiter::getMaxUnits(uint64_t amount, LimiterState * state) {
    timeval timev;
    gettimeofday(&timev, NULL);
    time_t t = time(NULL);
    struct tm *p = localtime(&t);
    int counter_index = p->tm_hour;
    LimiterEntry * limiter_entry = counters_[counter_index];

    if (!state) {
        return limiter_entry->max_rate();
    }

    TSMutexLock(update_mutex_);
    timeval elapsed;   
    timeval stime = state->time(counter_index);
    timersub(&timev, &stime, &elapsed); //当前时间 - 上次发送数据时间(copy)
    //场景: 配置中配的是1MB 1000ms  意思是1000ms内必须发送1MB
    float elapsed_ms = (elapsed.tv_sec * 1000.0f) + (elapsed.tv_usec / 1000.0f);
    float rate_timeslice = 1.0f - (limiter_entry->milliseconds() - elapsed_ms) / limiter_entry->milliseconds(); //感性认识: 当前时间跟上次发送时间差值越大 则这个值越大
    if (rate_timeslice  < 0 )
        rate_timeslice = 0;

    float replenishment = rate_timeslice * limiter_entry->max_rate(); //算出本次发送量 eg:时间间隔为500ms 这里的值为0.5MB

    float newallowance = state->allowance(counter_index) + replenishment;  //https://blog.csdn.net/sinat_24820331/article/details/75567615

    newallowance = newallowance > limiter_entry->max_rate() ? limiter_entry->max_rate() : newallowance;
    int rv = amount;

    if (amount > newallowance) {
        amount = rv = newallowance;
    }

    newallowance -= amount;  //如果buf里只有200K 那么剩下的300K得加到下次发送量上

    if (newallowance >= 0.0f) {
        state->set_allowance(counter_index, newallowance);
        state->set_time(counter_index, timev);
    }
    TSMutexUnlock(update_mutex_);
    return rv;
}

//感觉限速也不是很难做
//1记录每次发送的时间  算出本次发送的标准量
//2非常正常的场景就是本次要发送的理想值 肯定肯定肯定 不等于从os读得的值 
//3所以要记录这个"差值"
