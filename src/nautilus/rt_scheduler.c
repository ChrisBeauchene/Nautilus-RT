//
//  rt_scheduler.c
//  rt_scheduler_test
//
//  Created by Chris Beauchene on 2/15/16.
//  Copyright © 2016 EECS 395/495 Kernel Development. All rights reserved.
//

// #include <nautilus/nautilus.h>
// #include <nautilus/thread.h>
// #include <nautilus/rt_scheduler.h>
// #include <nautilus/irq.h>

#include <nautilus/rt_scheduler.h>
#include <nautilus/thread.h>


#define DEBUG 1
#define DEBUG_ADMIT 1

//#define INFO(fmt, args...) printk("RT SCHED: " fmt, ##args)
//#define RT_SCHED_PRINT(fmt, args...) printk("RT SCHED: " fmt, ##args)
//#if DEBUG
//#define RT_SCHED_DEBUG(fmt, args...) DEBUG_PRINT("RT SCHED DEBUG: " fmt, ##args)
//#else
//#define RT_SCHED_DEBUG(fmt, args...)
//#endif
//#define RT_SCHED_ERROR(fmt, args...) printk("RT SCHED ERROR: " fmt, ##args)


#define parent(i) ((i) ? (((i) - 1) >> 1) : 0)
#define left_child(i) (((i) << 1) + 1)
#define right_child(i) (((i) << 1) + 2)

// Thread definitions
// Types
#define APERIODIC 0
#define SPORADIC 1
#define PERIODIC 2

// Statuses
#define CREATED 0
#define ADMITTED 1
#define RUNNING 2

// Queue types
#define RUNNABLE_QUEUE 0
#define PENDING_QUEUE 1
#define APERIODIC_QUEUE 2
#define MAX_QUEUE 256

// Timing definition
#define NAUT_CONFIG_HZ 10

// Switching thread functions
static inline void nk_rt_update_exit(nk_rt_t *n);
static inline void nk_rt_update_enter(nk_rt_t *n);
static inline void check_deadlines(nk_rt_t *n);
static inline void update_periodic(nk_rt_t *n);

// Get time functions
static inline uint64_t rdtsc();

// Admission Control Functions

static inline double average_period(rt_queue_t *runnable, rt_queue_t *pending);
static inline double periodic_utilization_factor(rt_queue_t *runnable, rt_queue_t *pending);
static inline double sporadic_utilization_factor(rt_queue_t *runnable);


/********** Function definitions *************/

// SCHEDULE FUNCTIONS

// New job, arrival queues, run queues, current job all need to be taken into account when we do the utilization factors
// Fire the timer min(arrival queue min, completion of the current job,


/*
 
 three queues - both min priority queues
 
 Run queue - ordered by deadline time
 Pending queue - ordered by arrival
 Aperiodic queue - queue of aperiodic jobs to be run, sorted by order sent in?
 
 TIMER:
 
 Real-Time:
 Set timer() {
 timeout = min(PQ[0], now + job->work - job->run_time)
 }
 Non real-time:
 Set timer() {
 timeout = min(dequeue(PQ), now + quantum)
 }
 
 Events:
 Functions:
    Admit_job();
 Set timer
 
 Yield()
 Take current thread off the CPU and enqueue onto the RQ
 Dequeue RQ (Update new thread to be run)
 Set timer
 
 Timer interrupts:
 Job Arrived (job->arrival <= cur_time()):
 Dequeue PQ
 Enqueue in RQ
 If jobs[0]->deadline < job->deadline:
 We want to switch the job
 Set timer
 
 Periodic Job Completed(job->run_time >= job->slice):
 Update job with new arrival and deadline
 Enqueue into pending queue
 Change the state of the current thread
 Get next runnable job {
 If there is a job to switch to:
 Dequeue the run queue to the current thread
 Else:
 Switch to aperiodic job
 }
 Set timer
 
 Sporadic Job Completed:
 Take job off of CPU
 Get next runnable job
 Set timer
 
 Aperiodic Job 'Completed':
 (Acquires on a fire from the timer signifying that the job has used
 up its alloted 'quantum' time.)
 Take job off of CPU
 Enqueue onto APERIODIC queue
 Get next runnable job
 Set timer
 */


// Initialize the real-time thread
// We will use the constraaints accordingly to the the type that we have

nk_rt_t * nk_rt_init(int type,
                     rt_constraints_t *constraints,
                     uint64_t deadline,
                     nk_thread_t *thread
                     )
{
    nk_rt_t *rt_thread = (nk_rt_t *)malloc(sizeof(nk_rt_t));
    rt_thread->type = type;
    rt_thread->status = CREATED;
    rt_thread->constraints = constraints;
    rt_thread->start_time = 0;
    rt_thread->run_time = 0;
    // rt_thread->deadline = deadline;
    if (type == PERIODIC)
    {
        rt_thread->deadline = cur_time() + constraints->periodic.period;
    } else if (type == SPORADIC)
    {
        rt_thread->deadline = cur_time() + deadline;
    }
    
    thread->rt_thread = rt_thread;
    rt_thread->thread = thread;
    return rt_thread;
}

// Called inside of nk_sched_init if NAUT_CONFIG_USE_RT_SCHEDULER is set to true
rt_scheduler_t* rt_scheduler_init()
{
    // Disable the interrupts and save the flags
    // Do we need to do this because we are being called inside of nk_sched_init() which always disables the interrupts?
    // int flags;
    // flags = irq_disable_save();
    
    // RT_SCHED_PRINT("Initializing rt scheduler\n");
    
    // Allocate space for the scheduler and the three queues associated with it
        // May not need necessarily 3 times as much space maybe 2.25 (why need max queue size for aperiodic?)
    rt_scheduler_t* scheduler = (rt_scheduler_t *)malloc(sizeof(rt_scheduler_t));
    rt_queue_t *runnable = (rt_queue_t *)malloc(sizeof(rt_queue_t) + MAX_QUEUE * sizeof(nk_rt_t *));
    rt_queue_t *pending = (rt_queue_t *)malloc(sizeof(rt_queue_t) + MAX_QUEUE * sizeof(nk_rt_t *));
    rt_queue_t *aperiodic = (rt_queue_t *)malloc(sizeof(rt_queue_t) + MAX_QUEUE * sizeof(nk_rt_t *));
    
    if (!scheduler || !runnable || ! pending || !aperiodic) {
        printf("COULDN'T ALLOCATE SCHEDULER");
        // RT_SCHED_ERROR("Could not allocate rt scheduler\n");
        return NULL;
    } else {
        runnable->type = RUNNABLE_QUEUE;
        runnable->size = 0;
        scheduler->runnable = runnable;
        
        pending->type = PENDING_QUEUE;
        pending->size = 0;
        scheduler->pending = pending;
        
        aperiodic->type = APERIODIC_QUEUE;
        aperiodic->size = 0;
        scheduler->aperiodic = aperiodic;
    }
    // irq_enable_restore(flags);
    return scheduler;
}


void enqueue_thread(rt_queue_t *queue, nk_rt_t *thread)
{
    // printf("The current cycle count is: %llu\n", cur_time());
    // CHECK IF JOB IS PERIODIC OR SPORADIC AND DO ADMISSION CONTROL HERE
        // IF THE ADMISSION CONTROL RETURNS FALSE THEN WE WANT JUST RETURN
        // OUT OF THIS FUNCTION
    /******************************
     *******************************
     
     Admission Control Goes Here
     
     *******************************
     ******************************/
    
    
    // 1. RUN QUEUE  - DEADLINE DETERMINED POSITION IN QUEUE
    // 2. PENDING QUEUE - ARRIVAL DETERMINED POSITION IN QUEUE
    // 3. APERIODIC QUEUE - PRIORITY POSITION IN QUEUE
    
    // 1. RUN QUEUE
    if (queue->type == RUNNABLE_QUEUE)
    {
        if (queue->size == MAX_QUEUE)
        {
            printf("RUN QUEUE IS FULL");
            // RT_SCHED_DEBUG("RUN QUEUE IS FULL!");
            return;
        }
        
        uint64_t pos = queue->size++;
        queue->threads[pos] = thread;
        while (queue->threads[parent(pos)]->deadline > thread->deadline && pos != parent(pos))
        {
            // RT_SCHED_DEBUG("pos is %llu\t\t parent pos is %llu\n", pos, parent(pos));
            queue->threads[pos] = queue->threads[parent(pos)];
            pos = parent(pos);
        }
        queue->threads[pos] = thread;
        
    }
    
    // 2. PENDING QUEUE
    //      - NEED TO UPDATE BEFORE WE TRY TO ENQUEUE
    //      - THIS IS CRITICAL. OTHERWISE WE WILL BE DEQUEUEING FROM THE RUN QUEUE
    //        AND THEN WITHOUT UPDATING THE DEADLINE WE WILL INEVITABLE FAIL TO MEET
    //        SOME HARD TIME CONTRAINTS WITHOUT UPDATING.
    //      - CALL FUNCTION
    
    else if (queue->type == PENDING_QUEUE)
    {
        if (queue->size == MAX_QUEUE)
        {
            printf("PENDING QUEUE IS FULL");
            // RT_SCHED_DEBUG("PENDING QUEUE IS FULL!");
            return;
        }
        
        uint64_t pos = queue->size++;
        queue->threads[pos] = thread;
        while (queue->threads[parent(pos)]->deadline > thread->deadline && pos != parent(pos))
        {
            // RT_SCHED_DEBUG("pos is %llu\t\t parent pos is %llu\n", pos, parent(pos));
            queue->threads[pos] = queue->threads[parent(pos)];
            pos = parent(pos);
        }
        queue->threads[pos] = thread;
        
    } else if (queue->type == APERIODIC_QUEUE)
    {
        if (queue->size == MAX_QUEUE)
        {
            printf("APERIODIC QUEUE FULL!");
            // RT_SCHED_DEBUG("APERIODIC QUEUE IS FULL!");
            return;
        }
        
        uint64_t pos = queue->size++;
        queue->threads[pos] = thread;
        while (queue->threads[parent(pos)]->constraints->aperiodic.priority > thread->constraints->aperiodic.priority && pos != parent(pos))
        {
            // RT_SCHED_DEBUG("pos is %llu\t\t parent pos is %llu\n", pos, parent(pos));
            queue->threads[pos] = queue->threads[parent(pos)];
            pos = parent(pos);
        }
        queue->threads[pos] = thread;
    }
}

nk_rt_t* dequeue_thread(rt_queue_t *queue)
{
    // Do some check here to make sure tha
    
    // 1. RUN QUEUE
    //      - Use deadline as metric
    
    if (queue->type == RUNNABLE_QUEUE)
    {
        if (queue->size < 1)
        {
            printf("RUN QUEUE EMPTY");
            // RT_SCHED_DEBUG("RUNNABLE QUEUE EMPTY! CAN'T DEQUEUE!\n");
            return NULL;
        }

        nk_rt_t *min, *last;
        int now, child;
        
        min = queue->threads[0];
        last = queue->threads[--queue->size];
        
        for (now = 0; left_child(now) < queue->size; now = child)
        {
            child = left_child(now);
            if (child < queue->size && queue->threads[right_child(now)]->deadline < queue->threads[left_child(now)]->deadline)
            {
                child = right_child(now);
            }
            
            if (last->deadline > queue->threads[child]->deadline)
            {
                queue->threads[now] = queue->threads[child];
            } else {
                break;
            }
        }
        
        queue->threads[now] = last;
        
        return min;
    }
    
    // 2. PENDING QUEUE
    //      - DEADLINE IS THE METRIC
    else if (queue->type == PENDING_QUEUE)
    {
        if (queue->size < 1)
        {
            printf("PENDING QUEUE EMPTY");
            // RT_SCHED_DEBUG("PENDING QUEUE EMPTY! CAN'T DEQUEUE!\n");
            return NULL;
        }
        nk_rt_t *min, *last;
        int now, child;
        
        min = queue->threads[0];
        last = queue->threads[--queue->size];
        
        for (now = 0; left_child(now) < queue->size; now = child)
        {
            child = left_child(now);
            if (child < queue->size && queue->threads[right_child(now)]->deadline < queue->threads[left_child(now)]->deadline)
            {
                child = right_child(now);
            }
            
            if (last->deadline > queue->threads[child]->deadline)
            {
                queue->threads[now] = queue->threads[child];
            } else {
                break;
            }
        }
        
        queue->threads[now] = last;
        
        return min;
    }
    
    else if (queue->type == APERIODIC_QUEUE)
    {
        if (queue->size < 1)
        {
            printf("APERIODIC QUEUE EMPTY");
            // RT_SCHED_DEBUG("APERIODIC QUEUE EMPTY! CAN'T DEQUEUE!\n");
            return NULL;
        }
        nk_rt_t *min, *last;
        int now, child;
        
        min = queue->threads[0];
        last = queue->threads[--queue->size];
        
        for (now = 0; left_child(now) < queue->size; now = child)
        {
            child = left_child(now);
            if (child < queue->size && queue->threads[right_child(now)]->constraints->aperiodic.priority < queue->threads[left_child(now)]->constraints->aperiodic.priority)
            {
                child = right_child(now);
            }
            
            if (last->constraints->aperiodic.priority > queue->threads[child]->constraints->aperiodic.priority)
            {
                queue->threads[now] = queue->threads[child];
            } else {
                break;
            }
        }
        
        queue->threads[now] = last;
        
        return min;
    }
    return NULL;
}

void print_rt(nk_rt_t *thread)
{
    
    // printf("Deadline: %llu\t\t", thread->deadline);
    if (thread->type == PERIODIC)
    {
        printf("Slice: %llu\t\t Period: %llu\t\t\n", thread->constraints->periodic.slice, thread->constraints->periodic.period);
        printf("Utilization contribution: %lfu\n\n", (double)thread->constraints->periodic.slice / thread->constraints->periodic.period);
    } else if (thread->type == SPORADIC)
    {
        printf("Work: %llu\t\t", thread->constraints->sporadic.work);
    }
    // RT_SCHED_DEBUG("Thread deadline: %llu\n", thread->deadline);
}

void print_nk(nk_rt_t *thread)
{
    //printf("Thread priority: %llu\n", thread->constraints->aperiodic.priority);
}


// NAUTILUS TIMER INFORMATION AVAILABLE AT
    // nautilus/src/dev/timer.c & apic.c
    // function call looks like
        // api_timer_oneshot(apic, 1000000 / NAUT_CONFIG_HZ);


static void set_timer(rt_scheduler_t *scheduler, nk_rt_t *current_thread)
{
    // struct sys_info *sys = per_cpu_get(system);
    // struct apic_dev *apic = sys->cpus[my_cpu_id()]->apic;
    if (scheduler->pending->size > 0 && current_thread) {
        // nk_rt_t *thread = scheduler->pending->threads[0];
        // uint64_t completion_time = 0;
        if (current_thread->type == PERIODIC)
        {
            // apic_set(min(thread->deadline, cur_time() + (current_thread->constraints->periodic.slice - current_thread->run_time)));
        } else if (current_thread->type == SPORADIC)
        {
            // apic_set(min(thread->deadline, cur_time() + (current_thread->constraints->sporadic.work - current_thread->run_time)));
        } else
        {
            // apic_set(min(thread->deadline, cur_time() + 1000000 / NAUT_CONFIG_HZ));
        }
    } else {
        
    }
        
}

nk_thread_t *nk_rt_need_resched(rt_scheduler_t *scheduler)
{

    nk_thread_t *c; // = get_cur_thread();
    nk_rt_t *rt_c = c->rt_thread;
    nk_rt_t *rt_n;
    
    // REQUIRES NAUTILUS
    // struct sys_info *sys = per_cpu_get(system);
    // rt = sys->cpus[my_cpu_id()]->rt;
    
    // First we need to check to see if any new jobs have arrived
        // while current_time > PQ[0] we dequeue from the pending queue, update the period,
        // and enqueue onto the runnable queue
    // CHECK TO SEE IF THREADS HAVE ARRIVED!!!!
    
    while (scheduler->pending->size > 0)
    {
        if (scheduler->pending->threads[0]->deadline < cur_time())
        {
            nk_rt_t *arrived_thread = dequeue_thread(scheduler->pending);
            update_periodic(arrived_thread);
            enqueue_thread(scheduler->runnable, arrived_thread);
            continue;
        } else
        {
            break;
        }
    }
    
    // FIRST WE CHECK TO SEE IF THE THREAD HAS FINISHED RUNNING
        // THEN WE WANT TO SEE IF THEIR IS A HIGHER PRIORITY RUNNABLE THREAD
        // IF THERE IS NOT WE WANT TO SEE IF WE CAN CONTINUE RUNNING
        // IF WE CAN"T WE PUT WHATEVER NEXT RUNNABLE THREAD THERE IS
        // ELSE IF WE ARE DONE AND THERE ARE NO RUNNABLE THREADS
        // RUN AN APERIODIC THREAD
    
    switch (rt_c->type) {
        case APERIODIC:
            nk_rt_update_exit(rt_c);
            rt_c->constraints->aperiodic.priority = rt_c->run_time;
            
            if (scheduler->runnable->size > 0)
            {
                enqueue_thread(scheduler->aperiodic, rt_c);
                rt_n = dequeue_thread(scheduler->runnable);
                set_timer(scheduler, rt_n);
                nk_rt_update_enter(rt_n);
                return rt_n->thread;
            }
            
            if (scheduler->aperiodic->size > 0)
            {
                rt_n = scheduler->aperiodic->threads[0];
                if (rt_c->constraints->aperiodic.priority > rt_n->constraints->aperiodic.priority)
                {
                    rt_n = dequeue_thread(scheduler->aperiodic);
                    enqueue_thread(scheduler->aperiodic, rt_c);
                    set_timer(scheduler, rt_n);
                    nk_rt_update_enter(rt_n);
                    return rt_n->thread;
                }
            }
            
            set_timer(scheduler, rt_c);
            nk_rt_update_enter(rt_c);
            return rt_c->thread;
            break;
 
        case SPORADIC:
            nk_rt_update_exit(rt_c);
            if (scheduler->runnable->size > 0)
            {
                 if (rt_c->deadline > scheduler->runnable->threads[0]->deadline)
                 {
                     rt_n = dequeue_thread(scheduler->runnable);
                     enqueue_thread(scheduler->runnable, rt_c);
                     set_timer(scheduler, rt_n);
                     nk_rt_update_enter(rt_n);
                     return rt_n->thread;
                 } else
                 {
                     if (rt_c->run_time >= rt_c->constraints->sporadic.work)
                     {
                         check_deadlines(rt_c);
                         rt_n = dequeue_thread(scheduler->runnable);
                         set_timer(scheduler, rt_n);
                         nk_rt_update_enter(rt_n);
                         return rt_n->thread;
                     }
                 }
            } else if (scheduler->aperiodic->size > 0)
            {
                if (rt_c->run_time >= rt_c->constraints->sporadic.work)
                {
                    check_deadlines(rt_c);
                    rt_n = dequeue_thread(scheduler->aperiodic);
                    set_timer(scheduler, rt_n);
                    nk_rt_update_enter(rt_n);
                    return rt_n->thread;
                }
            }
            set_timer(scheduler, rt_c);
            nk_rt_update_enter(rt_c);
            return rt_c->thread;
            break;
 
        case PERIODIC:
            nk_rt_update_exit(rt_c);
            if (rt_c->run_time >= rt_c->constraints->periodic.slice) {
                check_deadlines(rt_c);
                // If we haven't passsed the deadline then we just enqueue onto the pending queue and the
                    // old deadline becomes the new "arrival" time.
                enqueue_thread(scheduler->pending, rt_c);
                if (scheduler->runnable->size > 0) {
                    rt_n = dequeue_thread(scheduler->runnable);
                    nk_rt_update_enter(rt_n);
                    return rt_n->thread;
                } else if (scheduler->aperiodic->size > 0)
                {
                    rt_n = dequeue_thread(scheduler->aperiodic);
                    nk_rt_update_enter(rt_n);
                    return rt_n->thread;
                } else
                {
                    printf("No threads to run. Idling.\n");
                    return NULL;
                    // ERROR("No jobs to switch to!\n");
                }
            } else {
                if (scheduler->runnable->size > 0)
                {
                    // If a thread is available with an earlier deadline switch to it.
                    if (rt_c->deadline > scheduler->runnable->threads[0]->deadline) {
                        rt_n = dequeue_thread(scheduler->runnable);
                        enqueue_thread(scheduler->runnable, rt_c);
                        nk_rt_update_enter(rt_n);
                        return rt_n->thread;
                    }
                }
            }
            nk_rt_update_enter(rt_c);
            return rt_c->thread;
            break;
    }
    
}


// NEED cur_time()

static inline void nk_rt_update_exit(nk_rt_t *n)
{
    // CUR TIME
    n->run_time += (cur_time() - n->start_time);
}      

static inline void nk_rt_update_enter(nk_rt_t *n)
{
    n->start_time = cur_time();
}      

static inline void check_deadlines(nk_rt_t *n)
{
    if (cur_time() > n->deadline) {
        printf("Missed deadline on task %p\n", n);
        // ERROR("Missed deadline on task %p\n",n);
    }
}

static inline void update_periodic(nk_rt_t *n)
{
    if (n->type == PERIODIC)
    {
        n->deadline = n->deadline + n->constraints->periodic.period;
        n->run_time = 0;
    }
}

uint64_t cur_time()
{
    return rdtsc();
}

static inline uint64_t rdtsc()
{
    uint64_t ret;
    __asm__ volatile ( "rdtsc" : "=A"(ret) );
    return ret;
}

int rt_admit(rt_scheduler_t *scheduler, nk_rt_t *thread)
{
    
    // This is the schedulers run time.
        // Here we want to assume the worst case scenario per scheduler call.
        // This will happen when a periodic thread finishes running in which case we need to:
            // Update the contents of the thread
            // Enqueue thread onto the pending queue
            // Worst case is there is a job to switch to
            // We then need to perform a context switch by dequeueing the run queue and placing that
            // thread onto the CPU. Else we switch to an aperiodic job.
            // We then need to account for the time it takes to set the timer and the "time it takes to switch back and forth for the scheduler itself and the overhead caused by the interrupts that took us to the scheduler in the first place.
    
    // AN ASSUMPTION. COMPLETELY INACCURATE AT THE MOMENT!!!!!!!
        // TESTS ON THE SCHEDULER TO CALCULATE THE WORST CASE ABOVE MUST BE PERFORMED
    
    uint64_t sched_run_time = 3; // I REPEAT. NOT CORRECT.
    
    // Divide by 2 to account for arrivals and deadlines!
    // ie. if average period is 47 then we can expect a job to arrive and finish each
    //      once every 47 cycles. The scheduler is then called twice in this case
    
    
    
    // GIVE 0.7 UTILIZATION TO PERIODIC JOBS + SCHEDULER
        // THIS MEANS THAT WE LEAVE THE REMAINING 0.3 ALLOCATED BETWEEN SPORADIC JOBS AND APERIODIC JOBS
            // TRY 0.2 TO SPORADIC IN THIS CASE AND 0.1 TO APERIODIC
    // CHECK rt_scheduler.c FOR DEFINITION MACROS

    
    // For now i won't worry about the scheduler
    // per_util += sched_util;
    
    
    // If the thread is periodic, then what we want to do is check to see whether or not the sum of the already allocatied
    // utilization is greater than what we allowed for periodic utilization. If is not, then we can admit the thread!
    if (thread->type == PERIODIC)
    {
        double avg_per = (average_period(scheduler->runnable, scheduler->pending) / 2.0);
        double sched_util = avg_per ? ((double)sched_run_time / avg_per) : 0.0;
        double periodic_util = periodic_utilization_factor(scheduler->runnable, scheduler->pending) + sched_util;
        printf("UTIL FACTOR =  \t%lf\n", periodic_util);
        
        
        if ((periodic_util + ((double)thread->constraints->periodic.slice / (double)thread->constraints->periodic.period)) > PERIODIC_UTIL) {
            // printf("PERIODIC: Admission denied utilization factor overflow!\n");
            return 0;
        }
        
        if ((thread->deadline - cur_time() - sched_run_time) <= thread->constraints->periodic.slice)
        {
           
            printf("PERIODIC: Time to reach first deadline is unachievable.\n");
            return 0;
        }
    } else if (thread->type == SPORADIC)
    {
        double sporadic_util = sporadic_utilization_factor(scheduler->runnable);
        if ((sporadic_util + (double)thread->constraints->sporadic.work / ((double)thread->deadline - (double)cur_time())) > SPORADIC_UTIL) {
            #if DEBUG_ADMIT
                printf("SPORADIC: Admission denied utilization factor overflow!\n");
            #endif
            return 0;
        }
        
        if ((thread->deadline - cur_time() - sched_run_time) <= thread->constraints->sporadic.work)
        {
            #if DEBUG_ADMIT
                printf("SPORADIC: Time to reach first deadline is unachievable.\n");
            #endif
            return 0;
        }
    }

    return 1;
}

static inline double average_period(rt_queue_t *runnable, rt_queue_t *pending)
{
    double sum_period = 0;
    uint64_t num_periodic = 0;
    int i;
    for (i = 0; i < runnable->size; i++)
    {
        nk_rt_t *thread = runnable->threads[i];
        if (thread->type == PERIODIC) {
            sum_period += (double)thread->constraints->periodic.period;
            num_periodic++;
        }
    }
    
    for (i = 0; i < pending->size; i++)
    {
        nk_rt_t *thread = pending->threads[i];
        if (thread->type == PERIODIC) {
            sum_period += (double)thread->constraints->periodic.period;
            num_periodic++;
        }
    }
    return num_periodic ? (sum_period / num_periodic) : 0.0;
}

static inline double periodic_utilization_factor(rt_queue_t *runnable, rt_queue_t *pending)
{
    double util = 0.0;
    
    int i;
    for (i = 0; i < runnable->size; i++)
    {
        nk_rt_t *thread = runnable->threads[i];
        if (thread->type == PERIODIC) {
            util += ((double)thread->constraints->periodic.slice / (double)thread->constraints->periodic.period);
        }
    }
    
    for (i = 0; i < pending->size; i++)
    {
        nk_rt_t *thread = pending->threads[i];
        if (thread->type == PERIODIC) {
            util += ((double)thread->constraints->periodic.slice / (double)thread->constraints->periodic.period);
        }
    }
    
    return util;
}

static inline double sporadic_utilization_factor(rt_queue_t *runnable)
{
    double util = 0.0;
    
    int i;
    for (i = 0; i < runnable->size; i++)
    {
        nk_rt_t *thread = runnable->threads[i];
        if (thread->type == SPORADIC)
        {
            util += ((double)thread->constraints->sporadic.work / ((double)thread->deadline - (double)cur_time()));
        }
    }
    return util;
}


