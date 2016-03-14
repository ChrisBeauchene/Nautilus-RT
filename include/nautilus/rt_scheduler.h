//
//  rt_scheduler.h
//  rt_scheduler_test
//
//  Created by Chris Beauchene on 2/15/16.
//  Copyright © 2016 EECS 395/495 Kernel Development. All rights reserved.
//

#ifndef rt_scheduler_h
#define rt_scheduler_h

#include <nautilus/thread.h>

/******************************************************************
            REAL TIME THREAD
            THREE TYPES (all have their own constraints)
                - PERIODIC
                - SPORADIC
                - APERIODIC
 
            THREE STATUSES
                - CREATED
                - ADMITTED
                - RUNNING
 
******************************************************************/

struct periodic_constraints {
    uint64_t period, slice, phase;  //phase = time of first arrival
    uint64_t slack;
};

struct sporadic_constraints {
    // arrived at time current_arrival;
    // do work seconds of work before deadline
    uint64_t work;
    uint64_t slack;
};

struct aperiodic_constraints {
    // priority within the aperiodic class
    uint64_t priority;
};

typedef union rt_constraints {
    struct periodic_constraints     periodic;
    struct sporadic_constraints     sporadic;
    struct aperiodic_constraints    aperiodic;
} rt_constraints;

typedef struct rt_thread {
    enum { APERIODIC = 0, SPORADIC = 1, PERIODIC = 2} type;
    enum { CREATED = 0, ADMITTED = 1, RUNNING = 2} status;
    rt_constraints *constraints;
    uint64_t start_time; // when we last started this thread
    uint64_t run_time;   // how  long it's been running in its current period
    uint64_t deadline;   // deadline for current period
    struct nk_thread *thread;
} rt_thread;

rt_thread* rt_thread_init(int type,
                     rt_constraints *constraints,
                     uint64_t deadline,
                     struct nk_thread *thread
                     );


/******************************************************************
            REAL TIME SCHEDULER
            CONTAINS THREE QUEUES
                - RUN QUEUE
                - PENDING QUEUE
                - APERIODIC QUEUE
 ******************************************************************/
typedef enum {RUNNABLE_QUEUE = 0, PENDING_QUEUE = 1, APERIODIC_QUEUE = 2} queue_type;

typedef struct rt_queue {
    queue_type type;
    uint64_t size;
    rt_thread *threads[0];
} rt_queue ;

typedef struct rt_scheduler {
    
    // RUN QUEUE
        // The queue of runnable threads
        // Priority min queue
    rt_queue *runnable;
    
    // PENDING QUEUE
        // The queue of threads waiting for their arrival time
    rt_queue *pending;
    
    // APERIODIC QUEUE
    // The queue of threads that are aperiodic (least important)
    rt_queue *aperiodic;

} rt_scheduler;

rt_scheduler* rt_scheduler_init();
struct nk_thread* rt_need_resched();

void enqueue_thread(rt_queue *queue, rt_thread *thread);
rt_thread* dequeue_thread(rt_queue *queue);

void rt_thread_dump(rt_thread *thread);

// Time
uint64_t cur_time();

/*
nk_thread_t * nk_rt_need_resched();
*/

/* ADMISSION CONTROL */

int rt_admit(rt_scheduler *scheduler, rt_thread *thread);

#endif /* rt_scheduler_h */
