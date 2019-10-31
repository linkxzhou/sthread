/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef __PROCESS_H___
#define __PROCESS_H___

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

namespace wrk
{

typedef void (*childprocess_callback)(void *args);

class Process
{
    #define PROCESS_SIZE 1000
public:
    int Create(int nums, childprocess_callback call, void *data = NULL)
    {
        m_nums_ = nums;
        memset(m_pidnums_, 0, sizeof(pid_t) * PROCESS_SIZE);
        m_parentpid_ = 0;
        m_piddata_ = data;

        if (pipe(m_pipefd_) < 0)
        {
            perror("pipe failed!");
            return -1;
        }

        for (int i = 0; i < nums; i++)
        {
            int ret = fork();
            if (ret == 0) // 子进程
            {
                // 写数据到m_pipefd_[1]
                close(m_pipefd_[0]);
                if (call != NULL)
                {
                    call(this);
                }
                // 执行子进程方法
                exit(0);
            }
            else if (ret > 0)
            {
                m_pidnums_[i] = ret;
            }
            else
            {
                return -2;
            }
        }

        // 读数据到m_pipefd_[0]
        close(m_pipefd_[1]);
        return 0;
    }

    pid_t* GetPids()
    {
        return m_pidnums_;
    }

    int* GetStatus()
    {
        return m_pidstatus_;
    }

    // 等待进程结束
    void Wait()
    {
        for (int i = 0; i < m_nums_; i++)
        {
            if (m_pidnums_[i] != 0)
            {
                waitpid(m_pidnums_[i], m_pidstatus_+i, 0);
            }
        }
    }

    // 发送信号给各个进程
    void Signal(int signal)
    {
        for (int i = 0; i < m_nums_; i++)
        {
            if (m_pidnums_[i] != 0)
            {
                kill(m_pidnums_[i], signal);
            }
        }

        return;
    }

    // 发送数据给各个进程
    void ChildProcessSend(void *data, unsigned int data_len)
    {
        void* tmp = (char *)malloc(data_len + sizeof(unsigned int));
        memcpy(tmp, &data_len, sizeof(unsigned int));
        // fprintf(stdout, "[SEND]len : %d, data : %s, char: [%c,%c,%c,%c]\n", 
        //     *tmp, (char *)data, tmp[0], tmp[1], tmp[2], tmp[3]);
        memcpy((char *)tmp + sizeof(unsigned int), data, data_len);
        // fprintf(stdout, "[SEND]data: %s\n", tmp + sizeof(unsigned int));
        write(m_pipefd_[1], tmp, data_len + sizeof(unsigned int));
        free(tmp);
        
        return;
    }

    // 父进程接收数据
    int ParentProcessRecv(void *data, unsigned int &data_len)
    {
        int n = read(m_pipefd_[0], &data_len, sizeof(unsigned int));
        // fprintf(stdout, "[RECV]len : %d, n : %d, char: [%c,%c,%c,%c]\n", 
        //     data_len, n, buf[0], buf[1], buf[2], buf[3]);
        if (n > 0)
        {
            read(m_pipefd_[0], data, data_len);
        }
        else
        {
            return -1;
        }

        // debug
        // fprintf(stdout, "[RECV]len : %d, data : %s\n", data_len, (char *)data);

        return 0;
    }

private:
    pid_t m_pidnums_[PROCESS_SIZE];
    int m_pidstatus_[PROCESS_SIZE];
    void *m_piddata_;
    int m_nums_;
    int m_pipefd_[2];
    pid_t m_parentpid_;
};

}

#endif