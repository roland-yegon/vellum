/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include "vellum/text/command.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "vellum/core/array.h"

typedef struct {
    size_t offset;
    size_t delete_length;
    char *insert_text;
    size_t insert_length;
    char *delete_text;
} VellumOp;

typedef struct {
    VellumArray ops;
} VellumEntry;

struct VellumCommandStack {
    VellumBuffer *buffer;
    VellumLineIndex *lines;
    VellumArray undo;
    VellumArray redo;
    VellumEntry pending;
    bool in_transaction;
    bool can_coalesce_insert;
    size_t memory_limit;
    size_t memory_used;
};

static void op_destroy(VellumOp *op)
{
    if (op == NULL) {
        return;
    }
    free(op->insert_text);
    free(op->delete_text);
    op->insert_text = NULL;
    op->delete_text = NULL;
}

static size_t op_payload_memory(const VellumOp *op)
{
    return op->insert_length + op->delete_length;
}

static size_t op_memory(const VellumOp *op)
{
    return sizeof(*op) + op_payload_memory(op);
}

static size_t entry_memory(const VellumEntry *entry)
{
    size_t total = sizeof(*entry) + vellum_array_byte_size(&entry->ops);
    const VellumOp *ops = vellum_array_at_const(&entry->ops, 0);
    for (size_t i = 0u; i < vellum_array_size(&entry->ops); ++i) {
        total += op_payload_memory(&ops[i]);
    }
    return total;
}

static void entry_destroy(VellumEntry *entry)
{
    if (entry == NULL) {
        return;
    }
    VellumOp *ops = vellum_array_at(&entry->ops, 0);
    for (size_t i = 0u; i < vellum_array_size(&entry->ops); ++i) {
        op_destroy(&ops[i]);
    }
    vellum_array_deinit(&entry->ops);
    memset(entry, 0, sizeof(*entry));
}

static void remove_first(VellumArray *array)
{
    if (array->length <= 1u) {
        array->length = 0u;
        return;
    }
    for (size_t i = 1u; i < array->length; ++i) {
        memcpy((unsigned char *)array->data + (i - 1u) * array->element_size,
               (unsigned char *)array->data + i * array->element_size, array->element_size);
    }
    --array->length;
}

static void clear_entries(VellumArray *entries, size_t *memory_used)
{
    VellumEntry *items = vellum_array_at(entries, 0);
    for (size_t i = 0u; i < vellum_array_size(entries); ++i) {
        *memory_used -= entry_memory(&items[i]);
        entry_destroy(&items[i]);
    }
    vellum_array_clear(entries);
}

static size_t compute_memory(const VellumCommandStack *stack)
{
    size_t total = 0u;
    const VellumEntry *undo = vellum_array_at_const(&stack->undo, 0);
    for (size_t i = 0u; i < vellum_array_size(&stack->undo); ++i) {
        total += entry_memory(&undo[i]);
    }
    const VellumEntry *redo = vellum_array_at_const(&stack->redo, 0);
    for (size_t i = 0u; i < vellum_array_size(&stack->redo); ++i) {
        total += entry_memory(&redo[i]);
    }
    if (stack->in_transaction) {
        total += entry_memory(&stack->pending);
    }
    return total;
}

static bool reserve_one(VellumArray *array, VellumErrorInfo *error)
{
    if (array->length < array->capacity) {
        return true;
    }
    size_t capacity = array->capacity == 0u ? 8u : array->capacity;
    if (capacity > SIZE_MAX / 2u) {
        vellum_error_set(error, VELLUM_ERR_CAPACITY, "command array capacity overflow");
        return false;
    }
    capacity *= 2u;
    if (!vellum_array_reserve(array, capacity)) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not grow command array to %zu",
                         capacity);
        return false;
    }
    return true;
}

static bool ensure_capacity(VellumCommandStack *stack, size_t additional, VellumErrorInfo *error)
{
    if (stack->memory_limit == 0u) {
        return true;
    }
    if (additional > SIZE_MAX - stack->memory_used) {
        vellum_error_set(error, VELLUM_ERR_CAPACITY, "undo memory accounting overflow");
        return false;
    }
    while (stack->memory_used + additional > stack->memory_limit &&
           vellum_array_size(&stack->undo) > 0u) {
        VellumEntry *oldest = vellum_array_at(&stack->undo, 0);
        stack->memory_used -= entry_memory(oldest);
        entry_destroy(oldest);
        remove_first(&stack->undo);
    }
    if (stack->memory_used + additional > stack->memory_limit) {
        vellum_error_set(error, VELLUM_ERR_CAPACITY,
                         "single edit exceeds the undo memory limit of %zu bytes",
                         stack->memory_limit);
        return false;
    }
    return true;
}

static bool make_op(const VellumCommandStack *stack, size_t offset, size_t delete_length,
                    const char *text, size_t insert_length, VellumOp *out, VellumErrorInfo *error)
{
    memset(out, 0, sizeof(*out));
    const size_t buffer_length = vellum_buffer_length(stack->buffer);
    if (offset > buffer_length || delete_length > buffer_length - offset ||
        (insert_length > 0u && text == NULL)) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "invalid command edit range");
        return false;
    }
    out->offset = offset;
    out->delete_length = delete_length;
    out->insert_length = insert_length;

    if (delete_length > 0u) {
        out->delete_text = malloc(delete_length);
        if (out->delete_text == NULL) {
            vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not save %zu deleted bytes",
                             delete_length);
            return false;
        }
        if (!vellum_buffer_read(stack->buffer, offset, out->delete_text, delete_length, error)) {
            op_destroy(out);
            return false;
        }
    }
    if (insert_length > 0u) {
        out->insert_text = malloc(insert_length);
        if (out->insert_text == NULL) {
            vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not save %zu inserted bytes",
                             insert_length);
            op_destroy(out);
            return false;
        }
        memcpy(out->insert_text, text, insert_length);
    }
    return true;
}

static bool notify_delete(VellumCommandStack *stack, size_t offset, size_t length,
                          VellumErrorInfo *error)
{
    if (stack->lines == NULL) {
        return true;
    }
    VellumErrorInfo local;
    if (!vellum_line_index_notify_delete(stack->lines, stack->buffer, offset, length, &local)) {
        vellum_error_set(error, VELLUM_ERR_INTERNAL, "line index delete notification failed: %s",
                         vellum_error_message(&local));
        return false;
    }
    return true;
}

static bool notify_insert(VellumCommandStack *stack, size_t offset, size_t length,
                          VellumErrorInfo *error)
{
    if (stack->lines == NULL) {
        return true;
    }
    VellumErrorInfo local;
    if (!vellum_line_index_notify_insert(stack->lines, stack->buffer, offset, length, &local)) {
        vellum_error_set(error, VELLUM_ERR_INTERNAL, "line index insert notification failed: %s",
                         vellum_error_message(&local));
        return false;
    }
    return true;
}

/* Deletes `delete_length` bytes at `offset`, then inserts `insert_length` bytes.
 * `deleted_text` is the captured content used to roll back a failed insertion. */
static bool apply_steps(VellumCommandStack *stack, size_t offset, size_t delete_length,
                        const char *deleted_text, const char *insert_text, size_t insert_length,
                        VellumErrorInfo *error)
{
    vellum_error_init(error);
    bool deleted = false;
    if (delete_length > 0u) {
        if (!vellum_buffer_delete(stack->buffer, offset, delete_length, error)) {
            return false;
        }
        deleted = true;
        if (!notify_delete(stack, offset, delete_length, error)) {
            return false;
        }
    }
    if (insert_length > 0u) {
        if (!vellum_buffer_insert(stack->buffer, offset, insert_text, insert_length, error)) {
            const VellumErrorInfo primary = *error;
            if (deleted) {
                VellumErrorInfo rollback_error;
                if (vellum_buffer_insert(stack->buffer, offset, deleted_text, delete_length,
                                         &rollback_error)) {
                    (void)notify_insert(stack, offset, delete_length, &rollback_error);
                }
            }
            vellum_error_set(error, VELLUM_ERR_INTERNAL, "buffer insert failed during edit: %s",
                             vellum_error_message(&primary));
            return false;
        }
        if (!notify_insert(stack, offset, insert_length, error)) {
            return false;
        }
    }
    return true;
}

static bool apply_op(VellumCommandStack *stack, const VellumOp *op, VellumErrorInfo *error)
{
    return apply_steps(stack, op->offset, op->delete_length, op->delete_text, op->insert_text,
                       op->insert_length, error);
}

static bool apply_inverse(VellumCommandStack *stack, const VellumOp *op, VellumErrorInfo *error)
{
    return apply_steps(stack, op->offset, op->insert_length, op->insert_text, op->delete_text,
                       op->delete_length, error);
}

/* Extends the most recent standalone insert when the new bytes continue exactly at
 * its end. Returns `handled = false` when the edit should take the normal path. */
static bool try_coalesce_insert(VellumCommandStack *stack, const VellumOp *op,
                                VellumErrorInfo *error, bool *handled)
{
    *handled = false;
    if (!stack->can_coalesce_insert || stack->in_transaction ||
        vellum_array_size(&stack->undo) == 0u) {
        return true;
    }
    VellumEntry *entry = vellum_array_at(&stack->undo, vellum_array_size(&stack->undo) - 1u);
    if (vellum_array_size(&entry->ops) != 1u) {
        return true;
    }
    VellumOp *last = vellum_array_at(&entry->ops, 0u);
    if (last->delete_length != 0u || last->insert_length == 0u || last->insert_text == NULL ||
        last->offset > SIZE_MAX - last->insert_length ||
        last->offset + last->insert_length != op->offset) {
        return true;
    }
    if (op->insert_length > SIZE_MAX - last->insert_length) {
        vellum_error_set(error, VELLUM_ERR_CAPACITY, "coalesced insert length overflow");
        return false;
    }
    if (stack->memory_limit != 0u) {
        if (stack->memory_used > SIZE_MAX - op->insert_length) {
            vellum_error_set(error, VELLUM_ERR_CAPACITY, "undo memory accounting overflow");
            return false;
        }
        if (stack->memory_used + op->insert_length > stack->memory_limit) {
            /* Let the normal path evict old entries instead of growing this one. */
            return true;
        }
    }

    const size_t old_length = last->insert_length;
    const size_t new_length = old_length + op->insert_length;
    char *grown = realloc(last->insert_text, new_length);
    if (grown == NULL) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED,
                         "could not grow coalesced insert to %zu bytes", new_length);
        return false;
    }
    memcpy(grown + old_length, op->insert_text, op->insert_length);
    last->insert_text = grown;
    last->insert_length = new_length;
    stack->memory_used += op->insert_length;
    clear_entries(&stack->redo, &stack->memory_used);

    if (!apply_steps(stack, op->offset, 0u, NULL, op->insert_text, op->insert_length, error)) {
        last->insert_length = old_length;
        stack->memory_used -= op->insert_length;
        return false;
    }
    *handled = true;
    return true;
}

VellumCommandStack *vellum_command_stack_create(VellumBuffer *buffer, VellumLineIndex *lines,
                                                size_t memory_limit, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (buffer == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "command stack needs a buffer");
        return NULL;
    }
    VellumCommandStack *stack = calloc(1u, sizeof(*stack));
    if (stack == NULL) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not allocate command stack");
        return NULL;
    }
    if (!vellum_array_init(&stack->undo, sizeof(VellumEntry), 0u) ||
        !vellum_array_init(&stack->redo, sizeof(VellumEntry), 0u)) {
        free(stack);
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not initialize command stacks");
        return NULL;
    }
    stack->buffer = buffer;
    stack->lines = lines;
    stack->memory_limit = memory_limit;
    return stack;
}

void vellum_command_stack_destroy(VellumCommandStack *stack)
{
    if (stack == NULL) {
        return;
    }
    clear_entries(&stack->undo, &stack->memory_used);
    clear_entries(&stack->redo, &stack->memory_used);
    entry_destroy(&stack->pending);
    vellum_array_deinit(&stack->undo);
    vellum_array_deinit(&stack->redo);
    free(stack);
}

/* Appends an edit either to the open transaction or as a new undo entry. All
 * allocations and capacity checks happen before the buffer is mutated. */
static bool record_op(VellumCommandStack *stack, VellumOp *op, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (stack == NULL) {
        op_destroy(op);
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "record needs a command stack");
        return false;
    }

    VellumArray *target = NULL;
    VellumEntry local;
    memset(&local, 0, sizeof(local));
    bool owns_entry = false;
    bool entry_in_undo = false;

    if (stack->in_transaction) {
        target = &stack->pending.ops;
    } else {
        if (!reserve_one(&stack->undo, error)) {
            op_destroy(op);
            return false;
        }
        if (!vellum_array_init(&local.ops, sizeof(VellumOp), 1u)) {
            op_destroy(op);
            vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not initialize undo entry");
            return false;
        }
        stack->memory_used += sizeof(VellumEntry);
        owns_entry = true;
    }

    clear_entries(&stack->redo, &stack->memory_used);
    if (!ensure_capacity(stack, op_memory(op), error)) {
        goto fail;
    }
    if (owns_entry) {
        VellumEntry *stored_entry = vellum_array_push(&stack->undo);
        if (stored_entry == NULL) {
            vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not record undo entry");
            goto fail;
        }
        *stored_entry = local;
        entry_in_undo = true;
        target = &stored_entry->ops;
    }
    if (!reserve_one(target, error)) {
        goto fail;
    }
    if (!apply_op(stack, op, error)) {
        goto fail;
    }

    VellumOp *stored_op = vellum_array_push(target);
    if (stored_op == NULL) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not record edit operation");
        goto fail;
    }
    *stored_op = *op;
    stack->memory_used += op_memory(op);
    memset(op, 0, sizeof(*op));
    return true;

fail:
    if (entry_in_undo) {
        VellumEntry *entry = vellum_array_at(&stack->undo, vellum_array_size(&stack->undo) - 1u);
        stack->memory_used -= entry_memory(entry);
        entry_destroy(entry);
        vellum_array_remove_swap(&stack->undo, vellum_array_size(&stack->undo) - 1u);
    } else if (owns_entry) {
        stack->memory_used -= sizeof(VellumEntry);
        entry_destroy(&local);
    }
    op_destroy(op);
    return false;
}

bool vellum_command_insert(VellumCommandStack *stack, size_t offset, const char *text,
                           size_t length, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (stack == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "insert needs a command stack");
        return false;
    }
    VellumOp op;
    if (!make_op(stack, offset, 0u, text, length, &op, error)) {
        return false;
    }
    bool handled = false;
    if (!try_coalesce_insert(stack, &op, error, &handled)) {
        op_destroy(&op);
        return false;
    }
    if (handled) {
        op_destroy(&op);
        return true;
    }
    const bool ok = record_op(stack, &op, error);
    stack->can_coalesce_insert = ok && !stack->in_transaction;
    return ok;
}

bool vellum_command_delete(VellumCommandStack *stack, size_t offset, size_t length,
                           VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (stack == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "delete needs a command stack");
        return false;
    }
    VellumOp op;
    if (!make_op(stack, offset, length, NULL, 0u, &op, error)) {
        return false;
    }
    const bool ok = record_op(stack, &op, error);
    stack->can_coalesce_insert = false;
    return ok;
}

bool vellum_command_replace(VellumCommandStack *stack, size_t offset, size_t delete_length,
                            const char *text, size_t insert_length, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (stack == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "replace needs a command stack");
        return false;
    }
    VellumOp op;
    if (!make_op(stack, offset, delete_length, text, insert_length, &op, error)) {
        return false;
    }
    const bool ok = record_op(stack, &op, error);
    stack->can_coalesce_insert = false;
    return ok;
}

bool vellum_command_begin(VellumCommandStack *stack, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (stack == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "begin needs a command stack");
        return false;
    }
    if (stack->in_transaction) {
        vellum_error_set(error, VELLUM_ERR_UNSUPPORTED, "transactions cannot be nested");
        return false;
    }
    if (!vellum_array_init(&stack->pending.ops, sizeof(VellumOp), 0u)) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not initialize transaction");
        return false;
    }
    stack->in_transaction = true;
    stack->can_coalesce_insert = false;
    stack->memory_used += sizeof(VellumEntry);
    return true;
}

bool vellum_command_commit(VellumCommandStack *stack, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (stack == NULL || !stack->in_transaction) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "commit needs an open transaction");
        return false;
    }
    if (vellum_array_size(&stack->pending.ops) == 0u) {
        stack->memory_used -= entry_memory(&stack->pending);
        entry_destroy(&stack->pending);
        stack->in_transaction = false;
        stack->can_coalesce_insert = false;
        return true;
    }
    if (!reserve_one(&stack->undo, error)) {
        return false;
    }
    VellumEntry *entry = vellum_array_push(&stack->undo);
    if (entry == NULL) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not record transaction");
        return false;
    }
    *entry = stack->pending;
    memset(&stack->pending, 0, sizeof(stack->pending));
    stack->in_transaction = false;
    stack->can_coalesce_insert = false;
    return true;
}

bool vellum_command_abort(VellumCommandStack *stack, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (stack == NULL || !stack->in_transaction) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "abort needs an open transaction");
        return false;
    }
    const VellumOp *ops = vellum_array_at_const(&stack->pending.ops, 0);
    for (size_t i = vellum_array_size(&stack->pending.ops); i > 0u; --i) {
        if (!apply_inverse(stack, &ops[i - 1u], error)) {
            return false;
        }
    }
    stack->memory_used -= entry_memory(&stack->pending);
    entry_destroy(&stack->pending);
    stack->in_transaction = false;
    stack->can_coalesce_insert = false;
    return true;
}

static bool move_entry(VellumArray *from, VellumArray *to, VellumErrorInfo *error)
{
    if (!reserve_one(to, error)) {
        return false;
    }
    VellumEntry *source = vellum_array_at(from, vellum_array_size(from) - 1u);
    VellumEntry *destination = vellum_array_push(to);
    if (source == NULL || destination == NULL) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not move undo entry");
        return false;
    }
    *destination = *source;
    vellum_array_remove_swap(from, vellum_array_size(from) - 1u);
    return true;
}

bool vellum_command_undo(VellumCommandStack *stack, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (stack == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "undo needs a command stack");
        return false;
    }
    stack->can_coalesce_insert = false;
    if (stack->in_transaction) {
        vellum_error_set(error, VELLUM_ERR_UNSUPPORTED, "cannot undo inside a transaction");
        return false;
    }
    if (vellum_array_size(&stack->undo) == 0u) {
        vellum_error_set(error, VELLUM_ERR_NOT_FOUND, "nothing to undo");
        return false;
    }
    if (!reserve_one(&stack->redo, error)) {
        return false;
    }
    const VellumEntry *entry =
        vellum_array_at_const(&stack->undo, vellum_array_size(&stack->undo) - 1u);
    const VellumOp *ops = vellum_array_at_const(&entry->ops, 0);
    for (size_t i = vellum_array_size(&entry->ops); i > 0u; --i) {
        if (!apply_inverse(stack, &ops[i - 1u], error)) {
            return false;
        }
    }
    return move_entry(&stack->undo, &stack->redo, error);
}

bool vellum_command_redo(VellumCommandStack *stack, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (stack == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "redo needs a command stack");
        return false;
    }
    stack->can_coalesce_insert = false;
    if (stack->in_transaction) {
        vellum_error_set(error, VELLUM_ERR_UNSUPPORTED, "cannot redo inside a transaction");
        return false;
    }
    if (vellum_array_size(&stack->redo) == 0u) {
        vellum_error_set(error, VELLUM_ERR_NOT_FOUND, "nothing to redo");
        return false;
    }
    if (!reserve_one(&stack->undo, error)) {
        return false;
    }
    const VellumEntry *entry =
        vellum_array_at_const(&stack->redo, vellum_array_size(&stack->redo) - 1u);
    const VellumOp *ops = vellum_array_at_const(&entry->ops, 0);
    for (size_t i = 0u; i < vellum_array_size(&entry->ops); ++i) {
        if (!apply_op(stack, &ops[i], error)) {
            return false;
        }
    }
    return move_entry(&stack->redo, &stack->undo, error);
}

bool vellum_command_can_undo(const VellumCommandStack *stack)
{
    return stack != NULL && !stack->in_transaction && vellum_array_size(&stack->undo) > 0u;
}

bool vellum_command_can_redo(const VellumCommandStack *stack)
{
    return stack != NULL && !stack->in_transaction && vellum_array_size(&stack->redo) > 0u;
}

bool vellum_command_in_transaction(const VellumCommandStack *stack)
{
    return stack != NULL && stack->in_transaction;
}

size_t vellum_command_undo_depth(const VellumCommandStack *stack)
{
    return stack == NULL ? 0u : vellum_array_size(&stack->undo);
}

size_t vellum_command_redo_depth(const VellumCommandStack *stack)
{
    return stack == NULL ? 0u : vellum_array_size(&stack->redo);
}

size_t vellum_command_memory_usage(const VellumCommandStack *stack)
{
    return stack == NULL ? 0u : stack->memory_used;
}

static bool check_ops(const VellumArray *ops, VellumErrorInfo *error)
{
    const VellumOp *items = vellum_array_at_const(ops, 0);
    for (size_t i = 0u; i < vellum_array_size(ops); ++i) {
        if ((items[i].delete_length > 0u && items[i].delete_text == NULL) ||
            (items[i].insert_length > 0u && items[i].insert_text == NULL)) {
            vellum_error_set(error, VELLUM_ERR_INTERNAL, "command op is missing payload bytes");
            return false;
        }
    }
    return true;
}

bool vellum_command_validate(const VellumCommandStack *stack, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (stack == NULL || !vellum_buffer_validate(stack->buffer)) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "command stack has an invalid buffer");
        return false;
    }
    if (stack->lines != NULL && !vellum_line_index_validate(stack->lines, stack->buffer)) {
        vellum_error_set(error, VELLUM_ERR_INTERNAL, "command stack has a stale line index");
        return false;
    }
    const VellumArray *stacks[] = {&stack->undo, &stack->redo};
    for (size_t s = 0u; s < sizeof(stacks) / sizeof(stacks[0u]); ++s) {
        const VellumEntry *entries = vellum_array_at_const(stacks[s], 0);
        for (size_t e = 0u; e < vellum_array_size(stacks[s]); ++e) {
            if (!check_ops(&entries[e].ops, error)) {
                return false;
            }
        }
    }
    if (stack->in_transaction && !check_ops(&stack->pending.ops, error)) {
        return false;
    }
    if (compute_memory(stack) != stack->memory_used) {
        vellum_error_set(error, VELLUM_ERR_INTERNAL, "undo memory accounting is inconsistent");
        return false;
    }
    return true;
}
