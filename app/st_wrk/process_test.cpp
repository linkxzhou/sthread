#include "process.h"


void callback(void *data)
{
    wrk::Process *p = (wrk::Process*)data;
    fprintf(stdout, "[1]p : %p\n", p);
    sleep(2);
    char buf[] = "hello, parent process";
    p->ChildProcessSend(buf, strlen(buf));

    p->flag = 100;
    fprintf(stdout, "[2]p : %p, flag : %d\n", p, p->flag);
}

int main(int argc, char *argv[])
{
    wrk::Process p;
    int ret = p.Create(10, callback);
    char buf[1024];
    unsigned int buf_len = 0;
    while (p.ParentProcessRecv(buf, buf_len) >= 0) ;
    p.Wait();
    fprintf(stdout, "ret : %d\n", ret);

    return 0;
}