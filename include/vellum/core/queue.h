#ifndef VELLUM_CORE_QUEUE_H
#define VELLUM_CORE_QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Fixed-size payload so messages can cross threads without owning memory.
 * Workers that need to transfer ownership do so through `ptr`; the receiver
 * is responsible for releasing it. */
typedef struct {
    uint32_t type;
    uint32_t flags;
    uintptr_t a;
    uintptr_t b;
    void *ptr;
} VellumMessage;

typedef struct VellumQueue VellumQueue;

VellumQueue *vellum_queue_create(size_t capacity);
bool vellum_queue_try_push(VellumQueue *queue, const VellumMessage *message);
bool vellum_queue_push(VellumQueue *queue, const VellumMessage *message);
bool vellum_queue_try_pop(VellumQueue *queue, VellumMessage *message);
bool vellum_queue_pop(VellumQueue *queue, VellumMessage *message);
void vellum_queue_shutdown(VellumQueue *queue);
size_t vellum_queue_size(const VellumQueue *queue);
bool vellum_queue_is_shutdown(const VellumQueue *queue);
void vellum_queue_destroy(VellumQueue *queue);

#ifdef __cplusplus
}
#endif

#endif /* VELLUM_CORE_QUEUE_H */
