//
//  rt_scheduler.h
//  rt_scheduler_test
//
//  Created by Chris Beauchene on 2/15/16.
//  Copyright © 2016 EECS 395/495 Kernel Development. All rights reserved.
//

#ifndef rt_scheduler_h
#define rt_scheduler_h



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

typedef union {
    struct periodic_constraints     periodic;
    struct sporadic_constraints     sporadic;
    struct aperiodic_constraints    aperiodic;
} rt_constraints_t;

typedef struct nk_rt_t {
    enum { APERIODIC = 0, SPORADIC = 1, PERIODIC = 2} type;
    enum { CREATED = 0, ADMITTED = 1, RUNNING = 2} status;
    rt_constraints_t *constraints;
    uint64_t start_time; // when we last started this thread
    uint64_t run_time;   // how  long it's been running in its current period
    uint64_t deadline;   // deadline for current period
    nk_thread_t *thread;
} nk_rt_t;

nk_rt_t * nk_rt_init(int type,
                     rt_constraints_t *constraints,
                     uint64_t deadline,
                     nk_thread_t *thread
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
    nk_rt_t *threads[0];
} rt_queue_t ;

typedef struct rt_scheduler {
    
    // RUN QUEUE
        // The queue of runnable threads
        // Priority min queue
    rt_queue_t *runnable;
    
    // PENDING QUEUE
        // The queue of threads waiting for their arrival time
    rt_queue_t *pending;
    
    // APERIODIC QUEUE
    // The queue of threads that are aperiodic (least important)
    rt_queue_t *aperiodic;

} rt_scheduler_t;

rt_scheduler_t* rt_scheduler_init();


void enqueue_thread(rt_queue_t *queue, nk_rt_t *thread);
nk_rt_t* dequeue_thread(rt_queue_t *queue);

void print_rt(nk_rt_t *thread);
void print_nk(nk_rt_t *thread);

// Time
uint64_t cur_time();

/*
nk_thread_t * nk_rt_need_resched();
*/

/* ADMISSION CONTROL */

int rt_admit(rt_scheduler_t *scheduler, nk_rt_t *thread);

#endif /* rt_scheduler_h */
