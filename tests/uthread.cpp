#include "uthread.h"
#include <assert.h>
#include <stdlib.h>
#include <time.h>

ucontext_t Scheduler::ucontext;
char Scheduler::stack[4096];
u_thread *Scheduler::threads[128];
int Scheduler::total_count = 0;
int Scheduler::current = -1;
std::list<u_thread*> Scheduler::activeList;
std::list<std::pair<u_thread*,time_t> > Scheduler::sleepList;

void u_thread::sleep(uthread_id utid,int t)
{
    Scheduler::sleep(utid,t);
}
void u_thread::star_routine()
{
    u_thread *current_uthread = Scheduler::GetCurrentUThread();
    assert(current_uthread);
    
    //回到Scheduler::uthread_create
    current_uthread->SetStatus(ACTIVED);
    ucontext_t &cur_context = current_uthread->GetContext();
    swapcontext(&cur_context,&Scheduler::ucontext);
    
    current_uthread->rable->main_routine();
    current_uthread->SetStatus(DIE);
}
void Scheduler::scheduler_init()
{
    for(int i = 0; i < MAX_UTHREAD; ++i)
        threads[i] = 0;
    getcontext(&ucontext);
    ucontext.uc_stack.ss_sp = stack;
    ucontext.uc_stack.ss_size = sizeof(stack);
    ucontext.uc_link = NULL;
    makecontext(&ucontext,schedule, 0);
}

void Scheduler::schedule()
{
    while(total_count > 0)
    {
    
        //首先执行active列表中的uthread
        std::list<u_thread*>::iterator it = activeList.begin(); 
        std::list<u_thread*>::iterator end = activeList.end();
        for( ; it != end; ++it)
        {
            if(*it && (*it)->GetStatus() == ACTIVED)
            {
                ucontext_t &cur_context = (*it)->GetContext();
                swapcontext(&ucontext,&cur_context);
                uthread_id uid = (*it)->GetUid();
                if((*it)->GetStatus() == DIE)
                {
                    printf("%d die/n",uid);
                    delete threads[uid];
                    threads[uid] = 0;
                    --total_count;
                    activeList.erase(it);
                    break;
                }
                else if((*it)->GetStatus() == SLEEP)
                {
                    printf("%d sleep/n",uid);
                    activeList.erase(it);
                    break;
                }
            }
        }
        //看看Sleep列表中是否有uthread该醒来了
        std::list<std::pair<u_thread*,time_t> >::iterator its = sleepList.begin();
        std::list<std::pair<u_thread*,time_t> >::iterator ends = sleepList.end();
        time_t now = time(NULL);
        for( ; its != ends; ++its)
        {
            //可以醒来了
            if(now >= its->second)
            {    
                u_thread *uthread = its->first;
                uthread->SetStatus(ACTIVED);
                activeList.push_back(uthread);
                sleepList.erase(its);
                break;
            }
        }
    }

    printf("scheduler end/n");
}
uthread_id Scheduler::uthread_create(runnable *rable,unsigned int stacksize)
{
    if(total_count >= MAX_UTHREAD)
        return INVAID_ID;
    int i = 0;
    for( ; i < MAX_UTHREAD; ++i)
    {
        if(threads[i] == 0)
        {
            threads[i] = new u_thread(rable,stacksize,i);
            ++total_count;
            current = i;
            ucontext_t &cur_context = threads[i]->GetContext();
            cur_context.uc_link = &ucontext;
            makecontext(&cur_context,u_thread::star_routine, 0);
            swapcontext(&ucontext, &cur_context);
            current = -1;
            activeList.push_back(threads[i]);
            return i;
        }
    }
}
void Scheduler::sleep(uthread_id utid,int t)
{
    if(utid == INVAID_ID)
        return;
    assert(threads[utid]);
    time_t now = time(NULL);
    now += t;
    printf("wake up time %u/n",now);
    //插入到sleep列表中
    sleepList.push_back(std::make_pair(threads[utid],now));
    
    //保存当前上下文切换回scheduler
    threads[utid]->SetStatus(SLEEP);
    ucontext_t &cur_context = threads[utid]->GetContext();
    swapcontext(&cur_context,&Scheduler::ucontext);
}