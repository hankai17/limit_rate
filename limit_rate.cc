#include "common.h"
#include "configuration.h"
#include "ratelimiter.h"
#include <ts/ts.h>
#include "ink_inet.h"
#include <ts/experimental.h>
#include <ts/remap.h>

#define PLUGIN_VERSION "version 1.1.0"

RateLimiter * rate_limiter = new RateLimiter();
typedef struct {
    TSVIO output_vio;
    TSIOBuffer output_buffer;
    TSIOBufferReader output_reader;
    TSHttpTxn txnp;bool ready;
    LimiterState *state;
    RateLimiter *rate_limiter;
} TransformData;

static TransformData *
my_data_alloc() {
    TransformData *data;

    data = (TransformData *) TSmalloc(sizeof(TransformData));
    data->output_vio = NULL;
    data->output_buffer = NULL;
    data->output_reader = NULL;
    data->txnp = NULL;
    data->state = NULL;
    data->rate_limiter = NULL;
    data->ready = false;
    return data;
}

static void my_data_destroy(TransformData * data) {
    TSReleaseAssert(data);
    if (data) {
        if (data->output_buffer) {
            TSIOBufferReaderFree(data->output_reader);
            TSIOBufferDestroy(data->output_buffer);
        }
        if (data->state) {
            delete data->state;
            data->state = NULL;
        }
        if (data->rate_limiter) {
            data->rate_limiter = NULL;
        }
        //DEBUG_LOG("my_data_destroy.\n");
        TSfree(data);
        data = NULL;
    }
}

static void handle_rate_limiting_transform(TSCont contp) 
{
    TSVConn output_conn;
    TSVIO input_vio;
    TransformData *data;
    int64_t towrite;
    int64_t avail;

    output_conn = TSTransformOutputVConnGet(contp);
    input_vio = TSVConnWriteVIOGet(contp);

    data = (TransformData *) TSContDataGet(contp);
    if (!data->ready) {
        data->output_buffer = TSIOBufferCreate();
        data->output_reader = TSIOBufferReaderAlloc(data->output_buffer);
        data->output_vio = TSVConnWrite(output_conn, contp, data->output_reader,
                TSVIONBytesGet(input_vio));
        data->ready = true;
    }

    if (!TSVIOBufferGet(input_vio)) {
        TSVIONBytesSet(data->output_vio, TSVIONDoneGet(input_vio));
        TSVIOReenable(data->output_vio);
        return;
    }

    towrite = TSVIONTodoGet(input_vio);

    if (towrite > 0) {
        avail = TSIOBufferReaderAvail(TSVIOReaderGet(input_vio));

        if (towrite > avail)
            towrite = avail;

        if (towrite > 0) {

            int64_t rl_max = data->rate_limiter->getMaxUnits(towrite,
                    data->state);
            towrite = rl_max;

            if (towrite) {
                TSIOBufferCopy(TSVIOBufferGet(data->output_vio),
                        TSVIOReaderGet(input_vio), towrite, 0);
                TSIOBufferReaderConsume(TSVIOReaderGet(input_vio), towrite);
                TSVIONDoneSet(input_vio, TSVIONDoneGet(input_vio) + towrite);
            } else {
                TSContSchedule(contp, 100, TS_THREAD_POOL_DEFAULT);
                return;
            }
        }
    }

    if (TSVIONTodoGet(input_vio) > 0) {
        if (towrite > 0) {
            TSVIOReenable(data->output_vio);
            TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_READY,
                    input_vio);
        }
    } else {
        TSVIONBytesSet(data->output_vio, TSVIONDoneGet(input_vio));
        TSVIOReenable(data->output_vio);
        TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_COMPLETE,
                input_vio);
    }

}

static int rate_limiting_transform(TSCont contp, TSEvent event, void *edata) 
{
    if (TSVConnClosedGet(contp)) {
        my_data_destroy((TransformData *) TSContDataGet(contp));
        TSContDestroy(contp);
        return 0;
    } 
    else {
        switch (event) {
            case TS_EVENT_ERROR: {
                                     TSVIO input_vio = TSVConnWriteVIOGet(contp);
                                     TSContCall(TSVIOContGet(input_vio), TS_EVENT_ERROR, input_vio);
                                 }
                                 break;
            case TS_EVENT_VCONN_WRITE_COMPLETE:
                                 TSVConnShutdown(TSTransformOutputVConnGet(contp), 0, 1);
                                 break;
            case TS_EVENT_VCONN_WRITE_READY:
            default:
                                 handle_rate_limiting_transform(contp);
                                 break;
        }
    }
    return 0;
}

void transform_add(TSHttpTxn txnp, RateLimiter *rate_limiter) 
{
    TSMBuffer bufp;
    TSMLoc hdr_loc;

    if (TSHttpTxnServerRespGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
        return;
    }
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);

    TSVConn contp = TSTransformCreate(rate_limiting_transform, txnp);
    TransformData * data = my_data_alloc();
    //DEBUG_LOG("my_data_alloc.\n");
    data->rate_limiter = rate_limiter;
    data->state = rate_limiter->registerLimiter();

    data->txnp = txnp;
    TSContDataSet(contp, data);
    TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, contp);
}

TSReturnCode txn_handler(TSCont contp, TSEvent event, void *edata) 
{
    TSHttpTxn txnp = (TSHttpTxn) edata;
    RateLimiter * rate_limiter =
        static_cast<RateLimiter *>(TSContDataGet(contp));
    switch (event) {
        case TS_EVENT_HTTP_READ_RESPONSE_HDR:
            //DEBUG_LOG("TS_EVENT_HTTP_READ_RESPONSE_HDR!\n");
            transform_add(txnp, rate_limiter);
            break;
        case TS_EVENT_ERROR: 
            {
                TSHttpTxnReenable(txnp, TS_EVENT_ERROR);
                return TS_SUCCESS;
            }
        default:
            break;
    }
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return TS_SUCCESS;
}

void read_config(const char* path)
{
    std::map<const char *, uint64_t>::iterator it;
    uint64_t ssize = 0;
    int i;
    char key[100];
    Configuration * conf = new Configuration();

    if (!conf->Parse(path)) {
        goto done;
    }
    for (i = 0; i < 24; i++) {
        memset(key, '\0', sizeof(key));
        sprintf(key, "%d%s", i, "hour");
        it = conf->limitconf.find(key);
        if (it != conf->limitconf.end()) {
            ssize = it->second;
            rate_limiter->addCounter(ssize, 1000);
        } else {
            rate_limiter->addCounter(1048576, 1000);
        }
    }
    rate_limiter->setEnableLimit(true);
    DEBUG_LOG("getEnableLimit %d.\n", rate_limiter->getEnableLimit());
    if (rate_limiter == NULL or !rate_limiter->getEnableLimit()) {
        return;
    }

done:
    delete conf;
    conf = NULL;
    return;
}

static int management_update(TSCont contp, TSEvent event, void* )
{
    TSReleaseAssert(event == TS_EVENT_MGMT_UPDATE);
    DEBUG_LOG("management updata event received.\n");
   
    char* path1 = (char*) TSContDataGet(contp);
    const std::string path(path1);
    Configuration* new_conf = new Configuration();
    if(!new_conf->Parse(path)) {
        delete new_conf;
        return 0;
    }
    std::map<const char *, uint64_t>::iterator it;
    it = (new_conf->limitconf).begin();
    int i = 0;
    char key[8];
    //while(it != (new_conf->limitconf).end()) {
    while(i != 24) {
        memset(key, '\0', sizeof(key));
        sprintf(key, "%d%s", i, "hour"); 
        it = new_conf->limitconf.find(key);
        if(it->second != (rate_limiter->counters_)[i]->max_rate_) {
            (rate_limiter->counters_)[i]->max_rate_ = it->second;
        }
        it++; 
        i++;
    }
    delete new_conf;
    return 0;
}

void TSPluginInit(int argc, const char *argv[])
{
    TSPluginRegistrationInfo info;
    TSCont contp;

    info.plugin_name = (char *)PLUGIN_NAME;
    info.vendor_name = (char *)"Apache Software Foundation";
    info.support_email = (char *)"ts-api-support@MyCompany.com";

    if(argc != 2)
    {
        ERROR_LOG("no path.\n");
        return;
    }

    #if TS_VERSION_MAJOR >= 6 
        if (TSPluginRegister(&info) != TS_SUCCESS) {
    #else
        if (TSPluginRegister(TS_SDK_VERSION_3_0, &info) != TS_SUCCESS) {
    #endif
        ERROR_LOG("Plugin registration failed.\n");
        return;
    }

    TSCont management_contp = TSContCreate(management_update, NULL);

    std::string config_path = std::string(argv[1]);
    char* p = (char*)TSmalloc(config_path.size() + 1);
    strcpy(p, config_path.c_str());

    TSContDataSet(management_contp, p);
    TSMgmtUpdateRegister(management_contp, PLUGIN_NAME);

    read_config(argv[1]);
    contp = TSContCreate((TSEventFunc) txn_handler, NULL);
    TSContDataSet(contp, rate_limiter);
    TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, contp);
    TSHttpHookAdd(TS_HTTP_TXN_CLOSE_HOOK, contp);
    
    //在traffic.out中打印,便于线上看到动态库的版本号
    DEBUG_LOG("Cache range plugin started, [%s]", PLUGIN_VERSION);
}

