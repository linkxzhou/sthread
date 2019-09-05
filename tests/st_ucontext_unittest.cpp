#include "../include/ucontext/st_ucontext.h"

ST_NAMESPACE_USING

struct Task
{
	char	name[256];	// offset known to acid
	char	state[256];
	Task	*next;
	Task	*prev;
	Task	*allnext;
	Task	*allprev;
	struct Context	context;
	uvlong	alarmtime;
	uint	id;
	uchar	*stk;
	uint	stksize;
};

Context	taskschedcontext;
Stack *t1 = NULL, *t2 = NULL;

static void taskstart(uint y, uint x)
{
    static int c = 0;
    
    ++c;
	Stack *t;
	ulong z;

	z = x<<16;	/* hide undefined 32-bit shift from 32-bit compilers */
	z <<= 16;
	z |= y;
	t = (Stack*)z;

    LOG_TRACE("taskstart %p, id : %ld, c : %d", t, t->m_id_, c);

    while (t->m_id_ == 1)
    {
        LOG_TRACE("id = 1 -------------");
        context_switch(&t1->m_context_, &t2->m_context_);
        LOG_TRACE("id = 1 ... begin : %ld, c : %d", t->m_id_, c);
        char buf[1024] = {0};
        if (++c >= 10)
        {
            LOG_TRACE("c : %d", c);
            context_exit();
        }
        LOG_TRACE("id = 1 ... end : %ld", t->m_id_);
    }

    LOG_TRACE("out ...");

    while (t->m_id_ == 2)
    {
        LOG_TRACE("id = 2 -------------");
        context_switch(&t2->m_context_, &t1->m_context_);
        LOG_TRACE("id = 2 ... begin : %ld, c : %d", t->m_id_, c);
        if (++c >= 10)
        {
            LOG_TRACE("c : %d", c);
            context_exit();
        }
        LOG_TRACE("id = 2 ... end : %ld", t->m_id_);
    }

    context_exit();
}

TEST(StStatus, ucontext)
{
    int size = 8192;
    uint x, y;
	ulong z;

    // t1 --------
    t1 = (Stack *)malloc(sizeof *t1+size);
	sigset_t zero1;
	memset(t1, 0, sizeof *t1);
	t1->m_vaddr_ = (uchar*)(t1+1);
	t1->m_vaddr_size_ = size;
	t1->m_id_ = 1;
	memset(&t1->m_context_.uc, 0, sizeof t1->m_context_.uc);
	sigemptyset(&zero1);
	sigprocmask(SIG_BLOCK, &zero1, &t1->m_context_.uc.uc_sigmask);

    t1->m_context_.uc.uc_stack.ss_sp = t1->m_vaddr_+8;
	t1->m_context_.uc.uc_stack.ss_size = t1->m_vaddr_size_-64;
    z = (ulong)t1;
	y = z;
	z >>= 16;
	x = z>>16;
    int r = getcontext(&t1->m_context_.uc);
    LOG_TRACE("r : %ld", r);
	if (r < 0)
    {
		LOG_TRACE("getcontext error");
		return ;
	}
	makecontext(&t1->m_context_.uc, (void(*)())taskstart, 2, y, x);

    // t2 --------
    t2 = (Stack *)malloc(sizeof *t2+size);
	sigset_t zero2;
	memset(t2, 0, sizeof *t2);
	t2->m_vaddr_ = (uchar*)(t2+1);
	t2->m_vaddr_size_ = size;
	t2->m_id_ = 2;
	memset(&t2->m_context_.uc, 0, sizeof t2->m_context_.uc);
	sigemptyset(&zero2);
	sigprocmask(SIG_BLOCK, &zero2, &t2->m_context_.uc.uc_sigmask);

    t2->m_context_.uc.uc_stack.ss_sp = t2->m_vaddr_+8;
	t2->m_context_.uc.uc_stack.ss_size = t2->m_vaddr_size_-64;
    z = (ulong)t2;
	y = z;
	z >>= 16;
	x = z>>16;
	if (getcontext(&t2->m_context_.uc) < 0)
    {
		LOG_TRACE("getcontext error");
		return ;
	}
	makecontext(&t2->m_context_.uc, (void(*)())taskstart, 2, y, x);

    LOG_TRACE("t1 : %p, t2 : %p, t : %p, t1 context : %p", 
        t1, t2, &taskschedcontext, &(t1->m_context_));
    context_switch(&taskschedcontext, &(t1->m_context_));
    LOG_TRACE("end ...");
}

// 测试所有的功能
int main(int argc, char* argv[])
{
    return RUN_ALL_TESTS();
}