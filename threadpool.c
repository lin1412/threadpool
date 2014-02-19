#include "threadpool.h"
#include "list.h"
#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#include <stdio.h>

struct future{

	thread_pool_callable_func_t callable;
	void * data;
	void * result;
	struct list_elem elem;
	sem_t semaphore;
};

struct thread_pool{
	struct list futures;
	pthread_t * threads;
	int numThreads;
	int stopFlag;
	pthread_mutex_t mutex;
	pthread_cond_t condition;
};

/*execute a new future */
static void * start_routine(void * p){
	struct thread_pool * pool = p;

	//optimize code
	pthread_mutex_t * op_mutex = &(pool->mutex);
	struct list * op_futures = &(pool->futures);

	//if the pool is not in shutdown mode
	while((pool->stopFlag) == 0){
		pthread_mutex_lock(op_mutex); //locks 1
		//realease mutex and block if condition is true
		pthread_cond_wait(&(pool->condition), op_mutex); 

		if(!list_empty(op_futures)){

			while (!list_empty(op_futures)){
				struct future * newFuture = list_entry(list_pop_front(op_futures), struct future, elem);
				pthread_mutex_unlock(op_mutex); // unlocks 1 or 2
				newFuture->result = newFuture->callable(newFuture->data);
				
				sem_post(&newFuture->semaphore); //unlock semaphore
				sched_yield(); //yield the process
				pthread_mutex_lock(op_mutex); //locks 2
			}
			
		} 
		else{
			//the list is empty, unlock and done
			pthread_mutex_unlock(op_mutex); //unlocks 1
			sched_yield();	//yield the process
			return NULL;
			
			
		}
		pthread_mutex_unlock(op_mutex); //unlocks 1 or 2
	}
	return NULL;
}

/* Create a new thread pool with n threads. */
struct thread_pool * thread_pool_new(int nthreads){

	//create a new thread pool and initialize it
	struct thread_pool * pool = malloc(sizeof(struct thread_pool) * nthreads);
	list_init(&(pool->futures));
	pool->threads = malloc(sizeof(int) * nthreads);
	pool->numThreads = nthreads;
	pool->stopFlag = 0;
	pthread_mutex_init(&(pool->mutex), NULL);
	pthread_cond_init(&(pool->condition), NULL);

	//create new thread to execute new future
	int i;
	for (i = 0; i < nthreads; i++) {
		pthread_create(&(pool->threads[i]), (const pthread_attr_t*)NULL, 
			start_routine, (void *) pool);
	}
	return pool;

};

/* Shutdown this thread pool.  May or may not execute already queued tasks. */
void thread_pool_shutdown(struct thread_pool * pool){

	pthread_mutex_lock(&(pool->mutex));
	
	pool->stopFlag = 1;
	pthread_cond_broadcast(&(pool->condition));
	
	pthread_mutex_unlock(&(pool->mutex));

	int i;
	//put thread into suspendtion until the target terminates
	for (i = 0; i < pool->numThreads; i++){
		pthread_join(pool->threads[i], NULL);
	}

}

/* Submit a callable to thread pool and return future.
 * The returned future can be used in future_get() and future_free()
 */
struct future * thread_pool_submit(
    struct thread_pool * pool, 
    thread_pool_callable_func_t callable, 
    void * callable_data){

	//create a new future and initialize it
	struct future * beyond = malloc(sizeof(struct future));
	beyond->callable = callable;
	beyond->data = callable_data;
	sem_init(&(beyond->semaphore), 0, 0);


	//lock, act then unlock
	pthread_mutex_lock(&(pool->mutex));
	list_push_back(&(pool->futures), &(beyond->elem));
	pthread_cond_signal(&(pool->condition));
	pthread_mutex_unlock(&(pool->mutex));

	return beyond;
}

/* Make sure that thread pool has completed executing this callable,
 * then return result. */
void * future_get(struct future *fut){
	sem_wait(&(fut->semaphore)); // lock semaphore
	return fut->result;

}

/* Deallocate this future.  Must be called after future_get() */
void future_free(struct future *fut){
	free(fut);
}