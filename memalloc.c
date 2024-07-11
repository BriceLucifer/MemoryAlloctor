#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>

// 定义16比特的内存对齐
typedef char ALIGN[16];

// header union 内存对齐
union header {
        struct {
                size_t size;
                unsigned is_free;
                union header *next;
        } s;
        // 也是ALIGN 内存对齐
        ALIGN stub;
};
// 将union 定义为一个header_t 类型
typedef union header header_t;

// 定义两个指针 head 和 tail
header_t *head = NULL, *tail = NULL;
// 线程锁类型 sbrk() 不是线程安全 所以需要一个线程锁 保护数据
pthread_mutex_t global_malloc_lock;

// 获取空闲空间
header_t *get_free_block(size_t size)
{
        header_t *curr = head;
        // 暂时的指针 用来循环指向下一个地址
        while(curr) {
               // 查看是否有空余空间去申请
               // 如果is_free 是true 并且 size >= size 才可以进行
                if (curr->s.is_free && curr->s.size >= size)
                        return curr;
                curr = curr->s.next;
        }
        return NULL;
}

// 释放内存
void free(void *block)
{
        header_t *header, *tmp;
        /* program break is the end of the process's data segment */
        void *programbreak;

        // 如果block为NULL 直接返回
        if (!block)
                return;
        pthread_mutex_lock(&global_malloc_lock);
        header = (header_t*)block - 1;
        // sbrk(0) 直接返回当前位置
        programbreak = sbrk(0);

        /*
            Check if the block to be freed is the last one in the
            linked list. If it is, then we could shrink the size of the
            heap and release memory to OS. Else, we will keep the block
            but mark it as free.
         */
        
        if ((char*)block + header->s.size == programbreak) {
                if (head == tail) {
                        head = tail = NULL;
                } else {
                        tmp = head;
                        while (tmp) {
                                if(tmp->s.next == tail) {
                                        tmp->s.next = NULL;
                                        tail = tmp;
                                }
                                tmp = tmp->s.next;
                        }
                }
                /*
                    sbrk() with a negative argument decrements the program break.
                    So memory is released by the program to OS.
                */
                sbrk(0 - header->s.size - sizeof(header_t));
                /*  Note: This lock does not really assure thread
                    safety, because sbrk() itself is not really
                    thread safe. Suppose there occurs a foregin sbrk(N)
                    after we find the program break and before we decrement
                    it, then we end up realeasing the memory obtained by
                    the foreign sbrk().
                */
                pthread_mutex_unlock(&global_malloc_lock);
                return;
        }
        // 标志这块内存是free
        header->s.is_free = 1;
        pthread_mutex_unlock(&global_malloc_lock);
}

void *malloc(size_t size)
{
        size_t total_size;
        void *block;
        header_t *header;
        if (!size)
                return NULL;
        pthread_mutex_lock(&global_malloc_lock);
        header = get_free_block(size);
        if (header) {
                /* Woah, found a free block to accomodate requested memory. */
                header->s.is_free = 0;
                pthread_mutex_unlock(&global_malloc_lock);
                return (void*)(header + 1);
        }
        /* We need to get memory to fit in the requested block and header from OS. */
        total_size = sizeof(header_t) + size;
        block = sbrk(total_size);
        if (block == (void*) -1) {
                pthread_mutex_unlock(&global_malloc_lock);
                return NULL;
        }
        header = block;
        header->s.size = size;
        header->s.is_free = 0;
        header->s.next = NULL;
        if (!head)
                head = header;
        if (tail)
                tail->s.next = header;
        tail = header;
        pthread_mutex_unlock(&global_malloc_lock);
        return (void*)(header + 1);
}

// 用malloc去实现calloc 然后利用memset去把内存设定0填充
void *calloc(size_t num, size_t nsize)
{
        size_t size;
        void *block;
        if (!num || !nsize)
                return NULL;
        size = num * nsize;
        /* check mul overflow */
        if (nsize != size / num)
                return NULL;
        block = malloc(size);
        if (!block)
                return NULL;
        memset(block, 0, size);
        return block;
}

void *realloc(void *block, size_t size)
{
        header_t *header;
        void *ret;
        if (!block || !size)
                return malloc(size);
        header = (header_t*)block - 1;
        if (header->s.size >= size)
                return block;
        ret = malloc(size);
        if (ret) {
                /* Relocate contents to the new bigger block */
                memcpy(ret, block, header->s.size);
                /* Free the old memory block */
                free(block);
        }
        return ret;
}

/* A debug function to print the entire link list */
// 目的是答应出 mem分布图片 有点类似于我之前写的链表打印
void print_mem_list()
{
        header_t *curr = head;
        printf("head = %p, tail = %p \n", (void*)head, (void*)tail);
        while(curr) {
                printf("addr = %p, size = %zu, is_free=%u, next=%p\n",
                        (void*)curr, curr->s.size, curr->s.is_free, (void*)curr->s.next);
                curr = curr->s.next;
        }
}