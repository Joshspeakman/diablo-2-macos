/* Trace shutdown must tolerate pending GPU callbacks and concurrent save requests.
 * clang -O1 -g -fsanitize=address,undefined recomp/tests/frame_trace_test.c -o /tmp/frame_trace_test
 * /tmp/frame_trace_test */
#include "../host/frame_trace.h"
#include <assert.h>
#include <string.h>
#include <unistd.h>

static unsigned rows(const char *path)
{
    FILE *f=fopen(path,"r"); assert(f); char line[256]; assert(fgets(line,sizeof line,f));
    unsigned count=0, w,h; size_t frame; double time,interval,submit,swap;
    while(fgets(line,sizeof line,f)) {
        assert(sscanf(line,"%zu,%lf,%lf,%lf,%lf,%u,%u",&frame,&time,&interval,&submit,&swap,&w,&h)==7);
        assert(time>=0 && time<1 && interval>=0 && interval<1000 && submit>=0 && submit<1000 && swap>=0 && swap<1000);
        assert(w==800 && h==600); count++;
    }
    fclose(f); return count;
}
static void *complete(void *arg)
{
    DSFrameTrace *t=arg;
    for(size_t i=0;i<1000;i++) ds_trace_complete(t,i,3000+i*10000);
    return NULL;
}
static void *save_trace(void *arg) { ds_trace_save(arg); return NULL; }
int main(void)
{
    char path[]="/tmp/d2-frame-trace.XXXXXX"; int fd=mkstemp(path); assert(fd>=0); close(fd);
    setenv("DS_FRAME_TRACE",path,1);
    DSFrameTrace t={0}; ds_trace_init(&t);
    size_t a=ds_trace_add(&t,1000,2000,0,800,600), b=ds_trace_add(&t,11000,12000,0,800,600);
    size_t late=ds_trace_add(&t,21000,22000,0,800,600);
    ds_trace_complete(&t,a,3000); ds_trace_complete(&t,b,13000);
    ds_trace_add(&t,31000,32000,31000,800,600); /* invalid/reordered completion */
    ds_trace_save(&t); assert(rows(path)==2);
    ds_trace_complete(&t,late,23000); ds_trace_save(&t); assert(rows(path)==2);
    assert(ds_trace_add(&t,41000,42000,0,800,600)==SIZE_MAX);
    pthread_mutex_destroy(&t.lock); pthread_mutex_destroy(&t.save_lock);
    for(int run=0;run<30;run++) {
        DSFrameTrace race={0}; ds_trace_init(&race);
        for(size_t i=0;i<1000;i++) assert(ds_trace_add(&race,1000+i*10000,2000+i*10000,0,800,600)==i);
        pthread_t gpu,quit;
        assert(!pthread_create(&gpu,NULL,complete,&race)); assert(!pthread_create(&quit,NULL,save_trace,&race));
        ds_trace_save(&race);
        pthread_join(gpu,NULL); pthread_join(quit,NULL);
        assert(rows(path)<=1000); pthread_mutex_destroy(&race.lock); pthread_mutex_destroy(&race.save_lock);
    }
    unlink(path); puts("Frame trace: unfinished frames, late callbacks and concurrent shutdown passed");
    return 0;
}
