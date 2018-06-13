#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "queue.h"

typedef struct element_s 
{
    int data;
    TAILQ_ENTRY(element_s) entries;
} element_t;

int main()
{
    element_t *p = NULL;
    element_t *q = NULL;
    element_t *curr = NULL;
    // 定义队列
    TAILQ_HEAD(queue_s, element_s) queueHead;
    // 队列初始化
    TAILQ_INIT(&queueHead);
    // 尾部插入，入队
    p = (element_t *)malloc(sizeof(element_t));
    p->data = 1;
    TAILQ_INSERT_TAIL(&queueHead, p, entries); 

    q = (element_t *)malloc(sizeof(element_t));
    q->data = 2;
    TAILQ_INSERT_TAIL(&queueHead, q, entries);

    // 遍历队列，输出数据
    TAILQ_FOREACH(curr, &queueHead, entries) {
        fprintf(stdout, "%d\n", curr->data);
    }

    // 从头部删除，出队
    p = TAILQ_FIRST(&queueHead);
    TAILQ_REMOVE(&queueHead, p, entries);

    // 输出队列尾部元素
    p = TAILQ_LAST(&queueHead, queue_s);
    fprintf(stdout, "%d\n", p->data);

    return 0;
}