#ifndef PLUGINS_LIMIT_RATE_RATELIMITER_H_
#define PLUGINS_LIMIT_RATE_RATELIMITER_H_

#include "common.h"
#include <time.h>
#include <vector>
#include <sys/time.h>
#include <pthread.h>
#include <ts/ts.h>
//#include <ts/ink_atomic.h>
#include "ink_atomic.h"

class LimiterEntry {
    public:
        LimiterEntry(uint64_t max_rate, uint64_t milliseconds):
            max_rate_(max_rate), milliseconds_(milliseconds) {
            }
        uint64_t max_rate(){
            return max_rate_;
        }
        uint64_t milliseconds(){
            return milliseconds_;
        }
    public:
        uint64_t max_rate_;
        uint64_t milliseconds_;
        DISALLOW_COPY_AND_ASSIGN(LimiterEntry);
};

class LimiterState {
    public:
        explicit LimiterState(float allowances[], timeval times[]):
        //explicit LimiterState(float* allowances, timeval* times):
            allowances_(allowances), times_(times), taken_(NULL) {
                //taken_ = (float *) malloc(sizeof(allowances));
                taken_ = (float *) malloc(sizeof(float) * 12);
                memset(taken_, 0, sizeof(float) * 12);
            }	
        float allowance(int index) {
            return allowances_[index];
        }
        void set_allowance(int index, float x) {
            allowances_[index] = x;
        }

        timeval time(int index) {
            return times_[index];
        }
        void set_time(int index, timeval x) {
            times_[index] = x;
        }

        float taken(int index) {
            return taken_[index];
        }
        void set_taken(int index, float amount) {
            taken_[index] += amount;
        }

        ~LimiterState() {
            free(allowances_);
            free(times_);
            free(taken_);
        }
    public:
        float * allowances_;
        timeval * times_;
        float * taken_;
        DISALLOW_COPY_AND_ASSIGN(LimiterState);
};


class RateLimiter {
    public:
        explicit RateLimiter():
            enable_limit(false), _ref_count(0), update_mutex_(TSMutexCreate()) {
                int rc = pthread_rwlock_init(&rwlock_keymap_, NULL);
                TSReleaseAssert(!rc);
            }
        ~RateLimiter() {
            for(size_t i = 0; i < counters_.size(); i++) {
                delete counters_[i];
                counters_[i] = NULL;
            }
            pthread_rwlock_destroy(&rwlock_keymap_);
        }

        int addCounter(float max_rate, uint64_t milliseconds);
        uint64_t getMaxUnits(uint64_t amount, LimiterState * state);
        LimiterState * registerLimiter();
        LimiterEntry * getCounter(int index);

        bool getEnableLimit() { return enable_limit; }
        void setEnableLimit(bool onlimit) { enable_limit = onlimit; }

    public:
        float * getCounterArray();
        timeval * getTimevalArray(timeval tv);
        bool enable_limit;

        std::vector<LimiterEntry *> counters_;
        pthread_rwlock_t rwlock_keymap_;
        volatile int _ref_count;
        TSMutex update_mutex_;
        DISALLOW_COPY_AND_ASSIGN(RateLimiter);
};

#endif
