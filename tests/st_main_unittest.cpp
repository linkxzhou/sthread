#include "../include/st_singleton.h"
#include "../include/st_util.h"
#include <stdio.h>

inline int LEFT(int i) {
  return 2 * i; //左子树的根节点的下标
}
inline int RIGHT(int i) {
  return 2 * i + 1; //右子树的根节点的下标
}

void heap_sort(int a[], int length);              //堆排序函数
void build_max_heap(int a[], int length);         //创建大根堆函数
void max_heapify(int a[], int start, int length); //大根堆化函数

class A {};

void *thread_callback(void *args) {
  fprintf(stdout, "##### NULL1\n");
  fprintf(stdout, "----- %p\n", sthread::Instance<A>());
  fprintf(stdout, "##### NULL2\n");

  fprintf(stdout, "----- %u\n", sthread::Util::GetUniqid());

  pthread_exit(NULL);
}

int main() {
  // int a[1000] = { 0 }, n = 0, length = 0, i = 0;
  // printf("请输入要排序的数组规模：");
  // scanf("%d", &n);
  // length = n;
  // while (n--)
  // 	scanf("%d", &a[i++]);

  // heap_sort(a, length - 1);

  // i = 0;
  // while (length--)
  // 	printf("%d,", a[i++]);

  pthread_t tids[10];
  for (int i = 0; i < 10; i++) {
    int r = pthread_create(&tids[i], NULL, thread_callback, NULL);
    fprintf(stdout, "r : %d\n", r);
  }

  sleep(5);

  return 0;
}

void heap_sort(int a[], int length) {
  int i = 0;
  build_max_heap(a, length);

  for (i = length; i > 0; i--) {
    a[i] += a[0];
    a[0] = a[i] - a[0];
    a[i] -= a[0];

    max_heapify(a, 0, i - 1);
  }
}

void build_max_heap(int a[], int length) {
  for (int i = length / 2; i >= 0; i--)
    max_heapify(a, i, length);
}

void max_heapify(int a[], int start, int length) {
  int l = LEFT(start), r = RIGHT(start), largest;

  if (l <= length && a[l] > a[start])
    largest = l;
  else
    largest = start;

  if (r <= length && a[r] > a[largest])
    largest = r;

  if (largest != start) {
    a[largest] += a[start];
    a[start] = a[largest] - a[start];
    a[largest] -= a[start];
    max_heapify(a, largest, length);
  }
}