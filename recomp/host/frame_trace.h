/* Optional, bounded in-memory trace. Never writes files on the frame path. */
#ifndef DS_FRAME_TRACE_H
#define DS_FRAME_TRACE_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
typedef struct {
    uint64_t ready, present, done;
    uint32_t width, height;
} DSFrameSample;
typedef struct {
    DSFrameSample *samples; size_t count, capacity; const char *path;
    pthread_mutex_t lock, save_lock; /* callbacks share data; competing quit requests wait for the export */
} DSFrameTrace;
static void ds_trace_init(DSFrameTrace *t) {
    pthread_mutex_init(&t->lock, NULL);
    pthread_mutex_init(&t->save_lock, NULL);
    t->path=getenv("DS_FRAME_TRACE");
    if(t->path && *t->path) {
        t->capacity=262144; t->samples=calloc(t->capacity,sizeof *t->samples);
        if(!t->samples) t->capacity=0;
    }
}
static size_t ds_trace_add(DSFrameTrace *t,uint64_t ready,uint64_t present,uint64_t done,uint32_t w,uint32_t h) {
    if(!t->path || !*t->path) return SIZE_MAX;
    pthread_mutex_lock(&t->lock);
    size_t index=SIZE_MAX;
    if(t->samples && t->count<t->capacity) {
        index=t->count++; t->samples[index]=(DSFrameSample){ready,present,done,w,h};
    }
    pthread_mutex_unlock(&t->lock);
    return index;
}
static void ds_trace_complete(DSFrameTrace *t,size_t index,uint64_t done) {
    if(index==SIZE_MAX) return;
    pthread_mutex_lock(&t->lock);
    if(t->samples && index<t->count) t->samples[index].done=done;
    pthread_mutex_unlock(&t->lock);
}
static void ds_trace_save(DSFrameTrace *t) {
    if(!t->path || !*t->path) return;
    pthread_mutex_lock(&t->save_lock);
    /* Detach once under the same lock used by GPU callbacks. Late completions and a second shutdown request
     * become no-ops; neither can access the detached allocation while it is written or freed. */
    pthread_mutex_lock(&t->lock);
    DSFrameSample *samples=t->samples; size_t count=t->count;
    t->samples=NULL; t->count=0;
    pthread_mutex_unlock(&t->lock);
    if(!samples) { pthread_mutex_unlock(&t->save_lock); return; }
    FILE *f=fopen(t->path,"w");
    if(f) {
        fputs("frame,time_s,interval_ms,submit_to_present_ms,swap_ms,width,height\n",f);
        uint64_t first=0, previous=0; size_t written=0;
        for(size_t i=0;i<count;i++) {
            DSFrameSample *s=&samples[i];
            /* A drawable may never arrive, or the last command buffer may still be in flight at quit. Do not
             * turn an unfinished timestamp or reordered callback into an unsigned multi-century interval. */
            if(!s->done || s->present<s->ready || s->done<s->present || s->done<previous) continue;
            if(!written) first=previous=s->done;
            fprintf(f,"%zu,%.9f,%.6f,%.6f,%.6f,%u,%u\n",i+1,(s->done-first)/1e9,
                    (s->done-previous)/1e6,s->ready?(s->present-s->ready)/1e6:0,
                    (s->done-s->present)/1e6,s->width,s->height);
            previous=s->done; written++;
        }
        fclose(f); fprintf(stderr,"host: saved %zu completed frame timings to %s\n",written,t->path);
    } else perror(t->path);
    free(samples);
    pthread_mutex_unlock(&t->save_lock);
}
#endif
