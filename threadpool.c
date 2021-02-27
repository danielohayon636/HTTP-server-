#include <unistd.h>
#include "stdio.h"
#include "pthread.h"
#include "stdlib.h"
#include "threadpool.h"

threadpool *create_threadpool(int num_of_threads)
{
    if (num_of_threads <= 0 || num_of_threads > MAXT_IN_POOL)
    {
        printf("Command line usage: threadpool <pool-size> <max-number-of-jobs>\n");
        return NULL;
    }
    threadpool *My_threadpool = (threadpool *)calloc(1, sizeof(threadpool));
    if (!My_threadpool) //memory allocation check
    {
        perror("memory allocation failed.\n ");
        return NULL;
    }
    My_threadpool->threads = (pthread_t *)calloc(num_of_threads, sizeof(pthread_t));
    if (!(My_threadpool->threads)) //memory allocation check
    {
        perror("memory allocation failed.\n ");
        free(My_threadpool);
        return NULL;
    }
    int not_eampty_status = 0;
    int eampty_status = 0;

    not_eampty_status = pthread_cond_init(&My_threadpool->q_not_empty, NULL);
    eampty_status = pthread_cond_init(&My_threadpool->q_empty, NULL);
    if (not_eampty_status == -1 || eampty_status == -1)
    {
        perror("condition initilaize failed.\n");
        free(My_threadpool->threads);
        free(My_threadpool);
        return NULL;
    }
    else
    {
        //set head and tail to NULL
        My_threadpool->qhead = NULL;
        My_threadpool->qtail = NULL;
        // ----
        My_threadpool->shutdown = 0;                                       //init
        My_threadpool->qsize = 0;                                          //init
        My_threadpool->dont_accept = 0;                                    //init
        My_threadpool->num_threads = num_of_threads;                       //init
        My_threadpool->qlock = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER; //init
    }
    //printf("%d :: \n", My_threadpool->qsize);
    int status_for_create = 0; //init
    int index = 0;
    while (index < num_of_threads)
    {
        status_for_create = pthread_create(&My_threadpool->threads[index], NULL, do_work, My_threadpool);
        if (status_for_create != 0)
        {
            free(My_threadpool->threads);
            free(My_threadpool);
            return NULL;
        }
        index++;
    }
    return My_threadpool;
}
void destroy_threadpool(threadpool *destroyme)
{
    pthread_mutex_lock(&destroyme->qlock);
    destroyme->dont_accept = 1;
    if (destroyme->qsize > 0)
    {
        pthread_cond_wait(&destroyme->q_empty, &destroyme->qlock);
    }
    destroyme->shutdown = 1;
    //unlock lock
    pthread_mutex_unlock(&destroyme->qlock);
    //broadcast
    pthread_cond_broadcast(&destroyme->q_not_empty);
    pthread_cond_broadcast(&destroyme->q_empty);
    int index = 0;
    //end all threads.
    while (index < destroyme->num_threads)
    {
        pthread_join(destroyme->threads[index], NULL);
        index++;
    }
    //free memory:
    free(destroyme->threads);
    free(destroyme);
    return;
}

void *do_work(void *p)
{
    threadpool *t_pool = (threadpool *)p;
    while (1)
    {
        pthread_mutex_lock(&(t_pool->qlock));
        if (t_pool->shutdown) //cant accept new work.
        {
            pthread_mutex_unlock(&(t_pool->qlock));
            return NULL;
        }
        //when no mission in line, run in this loop and wait for missions
        while (t_pool->qsize == 0)
        {
            pthread_cond_wait(&(t_pool->q_not_empty), &(t_pool->qlock));
            // if shut down flag up, return.
            if (t_pool->shutdown)
            {
                pthread_mutex_unlock(&(t_pool->qlock));
                return NULL;
            }
        }

        //MISSIONS IN LINE::
        //printf("OK\n");
        work_t *current = t_pool->qhead;
        t_pool->qsize--;
        if (t_pool->qhead->next)
            t_pool->qhead = t_pool->qhead->next;
        else
        {
            t_pool->qhead = NULL;
            t_pool->qtail = NULL;
            if (t_pool->dont_accept)
                pthread_cond_signal(&(t_pool->q_empty));
        }
        pthread_mutex_unlock(&(t_pool->qlock));
        current->routine(current->arg);
        free(current);
    }
}
void dispatch(threadpool *from_me, dispatch_fn dispatch_to_here, void *arg)
{
    if (dispatch_to_here == NULL || from_me == NULL || arg == NULL)
    {
        perror("Dispatch: invalid arguments.\n");
        return;
    }
    int status = 0;
    work_t *new_work = (work_t *)calloc(1, sizeof(work_t));
    if (!new_work) //memory allocation check
    {
        perror("memory allocation failed.\n");
        return;
    }
    new_work->routine = NULL;
    new_work->next = NULL;
    new_work->arg = NULL;
    new_work->arg = arg;
    new_work->routine = dispatch_to_here;
    status = pthread_mutex_lock(&from_me->qlock);
    if (status != 0)
    {
        perror("Mutex lock failed.\n ");
        free(new_work);
        return;
    }
    if (from_me->dont_accept == 0)
    {
        if (from_me->qsize == 0)
        {
            from_me->qhead = new_work; //head init

            from_me->qtail = new_work; //tail init
        }
        if (from_me->qsize > 0)
        {
            from_me->qtail->next = new_work;
            from_me->qtail = new_work;
        }
        from_me->qsize++;
        status = pthread_mutex_unlock(&from_me->qlock);
        if (status != 0)
        {
            perror("Mutex unlock failed.\n ");
            free(new_work);
            return;
        }
        pthread_cond_signal(&from_me->q_not_empty);
    }
    else
    {
        perror("MAX NUM OF TASKS REACHED\n");
        return;
    }
}
