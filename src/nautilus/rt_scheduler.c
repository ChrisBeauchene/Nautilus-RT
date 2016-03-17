//
//  rt_scheduler.c
//  rt_scheduler_test
//
//  Created by Chris Beauchene on 2/15/16.
//  Copyright © 2016 EECS 395/495 Kernel Development. All rights reserved.
//

#include <nautilus/nautilus.h>
#include <nautilus/thread.h>
#include <nautilus/rt_scheduler.h>
#include <nautilus/irq.h>
#include <nautilus/cpu.h>
#include <dev/apic.h>
#include <dev/timer.h>


#define INFO(fmt, args...) printk("RT SCHED: " fmt, ##args)
#define RT_SCHED_PRINT(fmt, args...) printk("RT SCHED: " fmt, ##args)
#define RT_SCHED_ERROR(fmt, args...) printk("RT SCHED ERROR: " fmt, ##args)

#define RT_SCHED_DEBUG(fmt, args...)
#ifdef NAUT_CONFIG_DEBUG_RT_SCHEDULER
#undef RT_SCHED_DEBUG
#define RT_SCHED_DEBUG(fmt, args...) RT_DEBUG_PRINT("SCHED: " fmt, ##args)
#endif

#define parent(i) ((i) ? (((i) - 1) >> 1) : 0)
#define left_child(i) (((i) << 1) + 1)
#define right_child(i) (((i) << 1) + 2)
#ifndef MIN
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#endif

// Thread definitions
// Types
#define APERIODIC 0
#define SPORADIC 1
#define PERIODIC 2
#define SCHEDULER 3

// UTILIZATION FACTOR LIMITS
#define PERIODIC_UTIL 0.7
#define SPORADIC_UTIL 0.2
#define APERIODIC_UTIL 0.1

// Statuses
#define CREATED 0
#define ADMITTED 1
#define RUNNING 2

// Queue types
#define RUNNABLE_QUEUE 0
#define PENDING_QUEUE 1
#define APERIODIC_QUEUE 2
#define MAX_QUEUE 256

static void set_timer(rt_scheduler *scheduler, rt_thread *thread);
static void set_timer_quantum();

// Switching thread functions
static inline void update_exit(rt_thread *t);
static inline void update_enter(rt_thread *t);
static inline void check_deadlines(rt_thread *t);
static inline void update_periodic(rt_thread *t);

// Get time functions
// static inline uint64_t rdtsc();

// Admission Control Functions

static inline double get_avg_per(rt_queue *runnable, rt_queue *pending);
static inline double get_per_util(rt_queue *runnable, rt_queue *pending);
static inline double get_spor_util(rt_queue *runnable);


/********** Function definitions *************/

// SCHEDULE FUNCTIONS

rt_thread* rt_thread_init(int type,
                     rt_constraints *constraints,
                     uint64_t deadline,
                     struct nk_thread *thread
                     )
{
    rt_thread *t = (rt_thread *)malloc(sizeof(rt_thread));
    t->type = type;
    t->status = CREATED;
    t->constraints = constraints;
    t->start_time = 0;
    t->run_time = 0;
    // rt_thread->deadline = deadline;
    if (type == PERIODIC)
    {
	RT_SCHED_DEBUG("CURRENT CLOCK CYCLE IS %llu\n", cur_time());
        t->deadline = cur_time() + constraints->periodic.period;
	RT_SCHED_DEBUG("Deadline: %llu\n", t->deadline);
    } else if (type == SPORADIC)
    {
        t->deadline = cur_time() + deadline;
    }
    
    thread->rt_thread = t;
    t->thread = thread;
    return t;
}

// Called inside of nk_sched_init if NAUT_CONFIG_USE_RT_SCHEDULER is set to tru
rt_scheduler* rt_scheduler_init(rt_thread *main_thread)
{
    rt_scheduler* scheduler = (rt_scheduler *)malloc(sizeof(rt_scheduler));
    rt_queue *runnable = (rt_queue *)malloc(sizeof(rt_queue) + MAX_QUEUE * sizeof(rt_thread *));
    rt_queue *pending = (rt_queue *)malloc(sizeof(rt_queue) + MAX_QUEUE * sizeof(rt_thread *));
    rt_queue *aperiodic = (rt_queue *)malloc(sizeof(rt_queue) + MAX_QUEUE * sizeof(rt_thread *));
    scheduler->main_thread = main_thread;
    scheduler->run_time = 0;
    if (!scheduler || !runnable || ! pending || !aperiodic) {
        RT_SCHED_ERROR("Could not allocate rt scheduler\n");
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
    return scheduler;
}


void enqueue_thread(rt_queue *queue, rt_thread *thread)
{
    // CHECK IF JOB IS PERIODIC OR SPORADIC AND DO ADMISSION CONTROL HERE
        // IF THE ADMISSION CONTROL RETURNS FALSE THEN WE WANT JUST RETURN
        // OUT OF THIS FUNCTION
    
    // 1. RUN QUEUE  - DEADLINE DETERMINED POSITION IN QUEUE
    // 2. PENDING QUEUE - ARRIVAL DETERMINED POSITION IN QUEUE
    // 3. APERIODIC QUEUE - PRIORITY POSITION IN QUEUE
    
    // 1. RUN QUEUE
    if (queue->type == RUNNABLE_QUEUE)
    {
        if (queue->size == MAX_QUEUE)
        {
            RT_SCHED_DEBUG("RUN QUEUE IS FULL!");
            return;
        }
        
        uint64_t pos = queue->size++;
        queue->threads[pos] = thread;
        while (queue->threads[parent(pos)]->deadline > thread->deadline && pos != parent(pos))
        {
            RT_SCHED_DEBUG("pos is %llu\t\t parent pos is %llu\n", pos, parent(pos));
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
            RT_SCHED_DEBUG("PENDING QUEUE IS FULL!");
            return;
        }
        
        uint64_t pos = queue->size++;
        queue->threads[pos] = thread;
        while (queue->threads[parent(pos)]->deadline > thread->deadline && pos != parent(pos))
        {
            RT_SCHED_DEBUG("pos is %llu\t\t parent pos is %llu\n", pos, parent(pos));
            queue->threads[pos] = queue->threads[parent(pos)];
            pos = parent(pos);
        }
        queue->threads[pos] = thread;
        
    } else if (queue->type == APERIODIC_QUEUE)
    {
        if (queue->size == MAX_QUEUE)
        {
            RT_SCHED_DEBUG("APERIODIC QUEUE IS FULL!");
            return;
        }
        
        uint64_t pos = queue->size++;
        queue->threads[pos] = thread;
        while (queue->threads[parent(pos)]->constraints->aperiodic.priority > thread->constraints->aperiodic.priority && pos != parent(pos))
        {
            RT_SCHED_DEBUG("pos is %llu\t\t parent pos is %llu\n", pos, parent(pos));
            queue->threads[pos] = queue->threads[parent(pos)];
            pos = parent(pos);
        }
        queue->threads[pos] = thread;
    }
}

rt_thread* dequeue_thread(rt_queue *queue)
{
    // Do some check here to make sure tha
    
    // 1. RUN QUEUE
    //      - Use deadline as metric
    
    if (queue->type == RUNNABLE_QUEUE)
    {
        if (queue->size < 1)
        {
            RT_SCHED_DEBUG("RUNNABLE QUEUE EMPTY! CAN'T DEQUEUE!\n");
            return NULL;
        }

        rt_thread *min, *last;
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
            RT_SCHED_DEBUG("PENDING QUEUE EMPTY! CAN'T DEQUEUE!\n");
            return NULL;
        }
        rt_thread *min, *last;
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
            RT_SCHED_DEBUG("APERIODIC QUEUE EMPTY! CAN'T DEQUEUE!\n");
            return NULL;
        }
        rt_thread *min, *last;
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

void rt_thread_dump(rt_thread *thread)
{
    
    if (thread->type == PERIODIC)
    {
        RT_SCHED_DEBUG("Slice: %llu\t\t Period: %llu\t\t\n", thread->constraints->periodic.slice, thread->constraints->periodic.period);
        RT_SCHED_DEBUG("Utilization contribution: %lfu\n\n", (double)thread->constraints->periodic.slice / thread->constraints->periodic.period);
    } else if (thread->type == SPORADIC)
    {
        RT_SCHED_DEBUG("Work: %llu\t\t", thread->constraints->sporadic.work);
    }
}


// NAUTILUS TIMER INFORMATION AVAILABLE AT
    // nautilus/src/dev/timer.c & apic.c
    // function call looks like
        // api_timer_oneshot(apic, 1000000 / NAUT_CONFIG_HZ);


static void set_timer(rt_scheduler *scheduler, rt_thread *current_thread)
{
    struct sys_info *sys = per_cpu_get(system);
    struct apic_dev *apic = sys->cpus[my_cpu_id()]->apic;
    if (scheduler->pending->size > 0 && current_thread) {
        rt_thread *thread = scheduler->pending->threads[0];
        uint64_t completion_time = 0;
        if (current_thread->type == PERIODIC)
        {
            apic_deadline_write(apic, MIN(thread->deadline - cur_time(), (current_thread->constraints->periodic.slice - current_thread->run_time)));
        } else if (current_thread->type == SPORADIC)
        {
            apic_deadline_write(apic, MIN(thread->deadline - cur_time(), (current_thread->constraints->sporadic.work - current_thread->run_time)));
        } else
        {
            apic_deadline_write(apic, MIN(thread->deadline - cur_time(), 10000000));
        }
    } else
   {
	set_timer_quantum();
   }
}

static void set_timer_quantum()
{
	struct sys_info *sys = per_cpu_get(system);
	struct apic_dev *apic = sys->cpus[my_cpu_id()]->apic;
	apic_deadline_write(apic, 10000000);
}

struct nk_thread *rt_need_resched()
{
    struct sys_info *sys = per_cpu_get(system);
    rt_scheduler *scheduler = sys->cpus[my_cpu_id()]->rt_sched;
    struct nk_thread *c = get_cur_thread();
    rt_thread *rt_c = c->rt_thread;
    rt_thread *rt_n;
    // RT_SCHED_DEBUG("INSIDE OF RT_NEED_RESCHED()\n"); 
    // RT_SCHED_DEBUG("CURRENT THREAD ID: %d\n", c->tid);
    // REQUIRES NAUTILUS
    
    // First we need to check to see if any new jobs have arrived
        // while current_time > PQ[0] we dequeue from the pending queue, update the period,
        // and enqueue onto the runnable queue
    // CHECK TO SEE IF THREADS HAVE ARRIVED!!!!

    
    
    while (scheduler->pending->size > 0)
    {
        if (scheduler->pending->threads[0]->deadline < cur_time())
        {
            rt_thread *arrived_thread = dequeue_thread(scheduler->pending);
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
            update_exit(rt_c);
            rt_c->constraints->aperiodic.priority = rt_c->run_time;
            
            if (scheduler->runnable->size > 0)
            {
                enqueue_thread(scheduler->aperiodic, rt_c);
                rt_n = dequeue_thread(scheduler->runnable);
                set_timer(scheduler, rt_n);
                update_enter(rt_n);
                return rt_n->thread;
            }
	    
            set_timer_quantum();
            return scheduler->main_thread->thread;
            break;
 
        case SPORADIC:
            update_exit(rt_c);
            if (scheduler->runnable->size > 0)
            {
                 if (rt_c->deadline > scheduler->runnable->threads[0]->deadline)
                 {
                     rt_n = dequeue_thread(scheduler->runnable);
                     enqueue_thread(scheduler->runnable, rt_c);
                     set_timer(scheduler, rt_n);
                     update_enter(rt_n);
                     return rt_n->thread;
                 } else
                 {
                     if (rt_c->run_time >= rt_c->constraints->sporadic.work)
                     {
                         check_deadlines(rt_c);
                         rt_n = dequeue_thread(scheduler->runnable);
                         set_timer(scheduler, rt_n);
                         update_enter(rt_n);
                         return rt_n->thread;
                     }
                 }
            } else if (rt_c->run_time <= rt_c->constraints->sporadic.work)
	    {
		set_timer(scheduler, rt_c);
		update_enter(rt_c);
	    }
            set_timer_quantum();
            return scheduler->main_thread->thread;
            break;
 
        case PERIODIC:
            update_exit(rt_c);
            if (rt_c->run_time >= rt_c->constraints->periodic.slice) {
                check_deadlines(rt_c);
                // If we haven't passsed the deadline then we just enqueue onto the pending queue and the
                    // old deadline becomes the new "arrival" time.
                enqueue_thread(scheduler->pending, rt_c);
                if (scheduler->runnable->size > 0) {
                    rt_n = dequeue_thread(scheduler->runnable);
                    update_enter(rt_n);
		    set_timer(scheduler, rt_n);
                    return rt_n->thread;
                }
		    
		set_timer_quantum();
                return scheduler->main_thread->thread; 
            } else {
                if (scheduler->runnable->size > 0)
                {
                    // If a thread is available with an earlier deadline switch to it.
                    if (rt_c->deadline > scheduler->runnable->threads[0]->deadline) {
                        rt_n = dequeue_thread(scheduler->runnable);
                        enqueue_thread(scheduler->runnable, rt_c);
                        update_enter(rt_n);
			set_timer(scheduler, rt_n);
                        return rt_n->thread;
                    }
                }
            }
            update_enter(rt_c);
	    set_timer(scheduler, rt_c);
            return rt_c->thread;
            break;
	case SCHEDULER:
            if (scheduler->runnable->size > 0)
            {
                rt_n = dequeue_thread(scheduler->runnable);
                set_timer(scheduler, rt_n);
                update_enter(rt_n);
		RT_SCHED_DEBUG("NEXT THREAD TO ENTER HAS DEADLINE OF %llu\n", rt_n->deadline);
                return rt_n->thread;
            }
            if (scheduler->aperiodic->size > 0)
            {
                rt_n = scheduler->aperiodic->threads[0];
                rt_n = dequeue_thread(scheduler->aperiodic);
                set_timer(scheduler, rt_n);
                update_enter(rt_n);
                return rt_n->thread;
            }

	RT_SCHED_DEBUG("SCHEDULER, BUT NO THREAD TO SWITCH TO.\n");
            
	default:
		set_timer_quantum();
		// RT_SCHED_DEBUG("NO REAL_TIME THREAD ATTACHED TO CURRENT THREAD!!\n");
		return c;
    }
    
}


// NEED cur_time()

static inline void update_exit(rt_thread *t)
{
    // CUR TIME
    t->run_time += (cur_time() - t->start_time);
}      

static inline void update_enter(rt_thread *t)
{
    t->start_time = cur_time();
}      

static inline void check_deadlines(rt_thread *t)
{
    if (cur_time() > t->deadline) {
        // RT_SCHED_ERROR("Missed deadline on task %p\n", t);
    }
}

static inline void update_periodic(rt_thread *t)
{
    if (t->type == PERIODIC)
    {
        t->deadline += t->constraints->periodic.period;
        t->run_time = 0;
    }
}

uint64_t cur_time()
{
    return rdtsc();
}

/* static inline uint64_t rdtsc()
{
    uint64_t ret;
    __asm__ volatile ( "rdtsc" : "=A"(ret) );
    return ret;
}*/

int rt_admit(rt_scheduler *scheduler, rt_thread *thread)
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
        double avg_per = (get_avg_per(scheduler->runnable, scheduler->pending) / 2.0);
        double sched_util = avg_per ? ((double)scheduler->run_time / avg_per) : 0.0;
        double per_util = get_per_util(scheduler->runnable, scheduler->pending) + sched_util;
        RT_SCHED_DEBUG("UTIL FACTOR =  \t%lf\n", per_util);
        
        
        if ((per_util + ((double)thread->constraints->periodic.slice / (double)thread->constraints->periodic.period)) > PERIODIC_UTIL) {
            RT_SCHED_DEBUG("PERIODIC: Admission denied utilization factor overflow!\n");
            return 0;
        }
        
        if ((thread->deadline - cur_time() - sched_run_time) <= thread->constraints->periodic.slice)
        {
           
            RT_SCHED_DEBUG("PERIODIC: Time to reach first deadline is unachievable.\n");
            return 0;
        }
    } else if (thread->type == SPORADIC)
    {
        double spor_util = get_spor_util(scheduler->runnable);
        if ((spor_util + (double)thread->constraints->sporadic.work / ((double)thread->deadline - (double)cur_time())) > SPORADIC_UTIL) {
            RT_SCHED_DEBUG("SPORADIC: Admission denied utilization factor overflow!\n");
            return 0;
        }
        
        if ((thread->deadline - cur_time() - sched_run_time) <= thread->constraints->sporadic.work)
        {
            RT_SCHED_DEBUG("SPORADIC: Time to reach first deadline is unachievable.\n");
            return 0;
        }
    }

    RT_SCHED_DEBUG("Slice of thread is %f\n", (double)thread->constraints->periodic.slice);
    return 1;
}

static inline double get_avg_per(rt_queue *runnable, rt_queue *pending)
{
    double sum_period = 0;
    uint64_t num_periodic = 0;
    int i;
    for (i = 0; i < runnable->size; i++)
    {
        rt_thread *thread = runnable->threads[i];
        if (thread->type == PERIODIC) {
            sum_period += (double)thread->constraints->periodic.period;
            num_periodic++;
        }
    }
    
    for (i = 0; i < pending->size; i++)
    {
        rt_thread *thread = pending->threads[i];
        if (thread->type == PERIODIC) {
            sum_period += (double)thread->constraints->periodic.period;
            num_periodic++;
        }
    }
    return num_periodic ? (sum_period / num_periodic) : 0.0;
}

static inline double get_per_util(rt_queue *runnable, rt_queue *pending)
{
    double util = 0.0;
    
    int i;
    for (i = 0; i < runnable->size; i++)
    {
        rt_thread *thread = runnable->threads[i];
        if (thread->type == PERIODIC) {
            util += ((double)thread->constraints->periodic.slice / (double)thread->constraints->periodic.period);
        }
    }
    
    for (i = 0; i < pending->size; i++)
    {
        rt_thread *thread = pending->threads[i];
        if (thread->type == PERIODIC) {
            util += ((double)thread->constraints->periodic.slice / (double)thread->constraints->periodic.period);
        }
    }
    
    return util;
}

static inline double get_spor_util(rt_queue *runnable)
{
    double util = 0.0;
    
    int i;
    for (i = 0; i < runnable->size; i++)
    {
        rt_thread *thread = runnable->threads[i];
        if (thread->type == SPORADIC)
        {
            util += ((double)thread->constraints->sporadic.work / ((double)thread->deadline - (double)cur_time()));
        }
    }
    return util;
}

static void test_thread(void *in)
{
	nk_thread_id_t tid = nk_get_tid();
	printk("(%lx) Hello from thread... \n", tid);
	struct naut_info * naut = &nautilus_info;	
	apic_oneshot_write(naut->sys.cpus[my_cpu_id()]->apic, 510000);	
	udelay(1000000);	
	RT_DEBUG_PRINT("Apic reads current clock time is %d initial clock time is %d\n", apic_oneshot_read(naut->sys.cpus[my_cpu_id()]->apic), apic_read(naut->sys.cpus[my_cpu_id()]->apic, APIC_REG_TMICT));
	printk("(%lx) woke up!\n", tid);
}

static void test_real_time(void *in)
{
	while (1)
	{	
		// udelay(10000000);
		// printk("IN TID: %d\n", *(int *)in);
	}	
}

void nk_rt_test()
{
	nk_thread_id_t r;
	nk_thread_id_t s;
	nk_thread_id_t t;
	rt_constraints *constraints_first = (rt_constraints *)malloc(sizeof(rt_constraints));
	struct periodic_constraints per_constr_first = {(100000000), (10000000), 0, 40};
	constraints_first->periodic = per_constr_first;

	rt_constraints *constraints_second = (rt_constraints *)malloc(sizeof(rt_constraints));
	struct periodic_constraints per_constr_second = {(100000000), (5000000), 0, 40};
	constraints_second->periodic = per_constr_second;

	rt_constraints *constraints_third = (rt_constraints *)malloc(sizeof(rt_constraints));
	struct periodic_constraints per_constr_third = {(100000000), (2500000), 0, 40};
	constraints_third->periodic = per_constr_third;

	RT_DEBUG_PRINT("ABOUT TO START TEST.\n");
	int first = 1, second = 2, third = 3;
	int *first_ptr = &first;
	int *second_ptr = &second;
	int *third_ptr = &third;		
	nk_thread_start((nk_thread_fun_t)test_real_time, (void *)first_ptr, NULL, 0, 0, &r, my_cpu_id(), PERIODIC, constraints_first, 0);
	nk_thread_start((nk_thread_fun_t)test_real_time, (void *)second_ptr, NULL, 0, 0, &s, my_cpu_id(), PERIODIC, constraints_second, 0);
	nk_thread_start((nk_thread_fun_t)test_real_time, (void *)third_ptr, NULL, 0, 0, &t, my_cpu_id(), PERIODIC, constraints_third, 0);
	nk_join(r, NULL);
	nk_join(s, NULL);
	nk_join(t, NULL);
}
