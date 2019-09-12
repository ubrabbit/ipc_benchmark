#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>

// #define USE_MB
// #define USE_LOCK

#define SHM_NAME_LEN 60
#define MIN(a, b) ((a) > (b) ? (b) : (a))
#define memory_barrier __sync_synchronize()

/////////////////////////////////////////////////////////////////////
// spin lock
typedef struct {
    int lock;
} spinlock_t;

static inline void spinlock_init(spinlock_t *lock) {
    lock->lock = 0;
}
static inline void spinlock_lock(spinlock_t *lock) {
    while (!__sync_bool_compare_and_swap(&lock->lock, 0, 1)) {}
}
static inline void spinlock_unlock(spinlock_t *lock) {
    __sync_lock_release(&lock->lock);
}

/////////////////////////////////////////////////////////////////////
// ring buffer
typedef struct {
    char shm_name[SHM_NAME_LEN];
    volatile uint32_t head;
    volatile uint32_t tail;
    uint32_t size;
#ifdef USE_LOCK
    spinlock_t lock;
#endif
    uint8_t *buffer;
} ringbuffer_t;

// 创建ringbuffer
ringbuffer_t *rb_create(const char *name, int sz, int master) {
    int shm_fd = shm_open(name, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) {
        perror("shm_open");
        return NULL;
    }
    size_t size = sizeof(ringbuffer_t) + sz;
    if (ftruncate(shm_fd, size) < 0) {
        perror("ftruncate");
        close(shm_fd);
        return NULL;
    }
    uint8_t *addr = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (addr == MAP_FAILED) {
        perror("mmap");
        close(shm_fd);
        return NULL;
    }
    if (close(shm_fd) == -1) {
        perror("close");
        exit(1);
    }

    ringbuffer_t *rb = (ringbuffer_t*)addr;
    rb->size = sz;
    strncpy(rb->shm_name, name, SHM_NAME_LEN-1);
    rb->head = rb->tail = 0;
    rb->buffer = addr + sizeof(ringbuffer_t);

#ifdef USE_LOCK
    if (master) {
        spinlock_init(&rb->lock);
    }
#endif
    return rb;
}

// 释放ringbuffer
void rb_free(ringbuffer_t *rb, int master) {
    size_t size = sizeof(ringbuffer_t) + rb->size;
    if (munmap(rb, size) == -1) {
        perror("munmap");
        return;
    }
    if (master) {
        if (shm_unlink(rb->shm_name) == -1) {
            perror("shm_unlink");
            return;
        }
    }
}

// 压入数据，成功返回0
int rb_push(ringbuffer_t *rb, const uint8_t *buff, int size) {
#ifdef USE_LOCK
    spinlock_lock(&rb->lock);
#endif
    uint32_t unused = rb->size - rb_used(rb);
    if (unused <= size) {
#ifdef USE_LOCK
    spinlock_unlock(&rb->lock);
#endif
        return -1;
    }

    uint32_t len = MIN(size, rb->size - rb->tail);
    memcpy(rb->buffer+rb->tail, buff, len);
    if (size > len)
        memcpy(rb->buffer, buff+len, size-len);

#ifdef USE_MB
    memory_barrier;
#endif

    rb->tail = (rb->tail + size) % rb->size;

#ifdef USE_LOCK
    spinlock_unlock(&rb->lock);
#endif

    return 0;
}

// 弹出数据，成功返回0
int rb_pop(ringbuffer_t *rb, uint8_t *buff, int size) {
#ifdef USE_LOCK
    spinlock_lock(&rb->lock);
#endif

    uint32_t used = rb_used(rb);
    if (used < size) {
#ifdef USE_LOCK
    spinlock_unlock(&rb->lock);
#endif
        return -1;
    }

    uint32_t len = MIN(size, rb->size - rb->head);
    memcpy(buff, rb->buffer + rb->head, len);
    if (size > len)
        memcpy(buff + len, rb->buffer, size - len);

#ifdef USE_MB
    memory_barrier;
#endif

    rb->head = (rb->head + size) % rb->size;

#ifdef USE_LOCK
    spinlock_unlock(&rb->lock);
#endif

    return 0;
}

// 已使用大小
inline int rb_used(ringbuffer_t *rb) {
    uint32_t head = rb->head;
    uint32_t tail = rb->tail;
    if (head <= tail)
        return tail - head;
    else
        return rb->size - (head - tail);
}


double getdetlatimeofday(struct timeval *begin, struct timeval *end)
{
    return (end->tv_sec + end->tv_usec * 1.0 / 1000000) -
           (begin->tv_sec + begin->tv_usec * 1.0 / 1000000);
}

int main(int argc, char const *argv[])
{
    int i;
    struct timeval begin, end;
    if (argc != 3) {
        printf("usage: ./shm2 <size> <count>\n");
        return 1;
    }

    const char *path = "/shm_ring_buffer";
    int size = atoi(argv[1]);
    int count = atoi(argv[2]);
    unsigned char *buf = malloc(size);
    int rbsize = size * 50 + 1;

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return -1;
    }
    else if (pid == 0) {        //parent
        ringbuffer_t *rb = rb_create(path, rbsize, 1);
        i = 0;
        while (i < count) {
            if (!rb_pop(rb, buf, size)) {
                // if (*(uint32_t*)buf != i) {
                //     printf("data: %d\n", *(uint32_t*)buf);
                // }
                ++i;
            } else {
                // printf("error\n");
                // sleep(1);
            }
        }
        rb_free(rb, 1);
    }
    else {       // child
        sleep(1);

        gettimeofday(&begin, NULL);
        ringbuffer_t *rb = rb_create(path, rbsize, 0);
        i = 0;
        while (i < count) {
            // *(uint32_t*)buf = i;
            if (!rb_push(rb, buf, size)) {
                ++i;
            } else {
                // printf("error\n");
                // sleep(1);
            }
        }

        gettimeofday(&end, NULL);
        rb_free(rb, 0);

        double tm = getdetlatimeofday(&begin, &end);
        printf("%fMB/s %fmsg/s %f\n",
            count * size * 1.0 / (tm * 1024 * 1024),
            count * 1.0 / tm, tm);
    }

    return 0;
}
