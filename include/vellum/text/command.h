/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */
#ifndef VELLUM_TEXT_COMMAND_H
#define VELLUM_TEXT_COMMAND_H

#include <stdbool.h>
#include <stddef.h>

#include "vellum/core/error.h"
#include "vellum/text/buffer.h"
#include "vellum/text/line_index.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Undoable edit stack over a buffer and its optional line index. The buffer and
 * line index are borrowed and must outlive the stack; pass NULL for `lines` when
 * the caller does not maintain one. All offsets and lengths are bytes.
 *
 * `memory_limit` bounds the bytes retained for undo/redo records (0 means
 * unlimited); reaching it discards the oldest undo entries. Transactions group
 * several edits into one undo entry and cannot be nested. Adjacent standalone
 * inserts coalesce into one undo entry, so a run of typing is undone at once. */
typedef struct VellumCommandStack VellumCommandStack;

VellumCommandStack *vellum_command_stack_create(VellumBuffer *buffer, VellumLineIndex *lines,
                                                size_t memory_limit, VellumErrorInfo *error);
void vellum_command_stack_destroy(VellumCommandStack *stack);

/* Apply and record an edit. Each call is one undo entry unless it is made inside
 * a transaction. */
bool vellum_command_insert(VellumCommandStack *stack, size_t offset, const char *text,
                           size_t length, VellumErrorInfo *error);
bool vellum_command_delete(VellumCommandStack *stack, size_t offset, size_t length,
                           VellumErrorInfo *error);
bool vellum_command_replace(VellumCommandStack *stack, size_t offset, size_t delete_length,
                            const char *text, size_t insert_length, VellumErrorInfo *error);

/* Groups subsequent edits until commit. Abort restores the pre-transaction state
 * by applying inverse operations in reverse order. */
bool vellum_command_begin(VellumCommandStack *stack, VellumErrorInfo *error);
bool vellum_command_commit(VellumCommandStack *stack, VellumErrorInfo *error);
bool vellum_command_abort(VellumCommandStack *stack, VellumErrorInfo *error);

bool vellum_command_undo(VellumCommandStack *stack, VellumErrorInfo *error);
bool vellum_command_redo(VellumCommandStack *stack, VellumErrorInfo *error);

bool vellum_command_can_undo(const VellumCommandStack *stack);
bool vellum_command_can_redo(const VellumCommandStack *stack);
bool vellum_command_in_transaction(const VellumCommandStack *stack);
size_t vellum_command_undo_depth(const VellumCommandStack *stack);
size_t vellum_command_redo_depth(const VellumCommandStack *stack);
size_t vellum_command_memory_usage(const VellumCommandStack *stack);

/* Checks the borrowed structures and the internal edit records. */
bool vellum_command_validate(const VellumCommandStack *stack, VellumErrorInfo *error);

#ifdef __cplusplus
}
#endif

#endif /* VELLUM_TEXT_COMMAND_H */
