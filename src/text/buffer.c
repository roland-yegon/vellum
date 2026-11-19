/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include "vellum/text/buffer.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define VELLUM_STORE_ORIGINAL 0u
#define VELLUM_STORE_ADDED 1u
#define VELLUM_INITIAL_ADD_CAPACITY 256u

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} VellumStore;

typedef struct {
    uint32_t store;
    size_t offset;
    size_t length;
} VellumPiece;

typedef struct VellumNode {
    struct VellumNode *left;
    struct VellumNode *right;
    VellumPiece piece;
    size_t length;
    int height;
} VellumNode;

struct VellumBuffer {
    VellumStore stores[2u];
    VellumNode *root;
    size_t length;
};

static size_t node_length(const VellumNode *node)
{
    return node == NULL ? 0u : node->length;
}

static int node_height(const VellumNode *node)
{
    return node == NULL ? 0 : node->height;
}

static VellumNode *node_create(VellumPiece piece)
{
    VellumNode *node = malloc(sizeof(*node));
    if (node == NULL) {
        return NULL;
    }
    node->left = NULL;
    node->right = NULL;
    node->piece = piece;
    node->length = piece.length;
    node->height = 1;
    return node;
}

static void node_update(VellumNode *node)
{
    const int left_height = node_height(node->left);
    const int right_height = node_height(node->right);
    node->height = 1 + (left_height > right_height ? left_height : right_height);
    node->length = node_length(node->left) + node->piece.length + node_length(node->right);
}

static VellumNode *rotate_left(VellumNode *node)
{
    VellumNode *pivot = node->right;
    node->right = pivot->left;
    pivot->left = node;
    node_update(node);
    node_update(pivot);
    return pivot;
}

static VellumNode *rotate_right(VellumNode *node)
{
    VellumNode *pivot = node->left;
    node->left = pivot->right;
    pivot->right = node;
    node_update(node);
    node_update(pivot);
    return pivot;
}

/* Restores the AVL invariant even when a split or join detaches a whole subtree
 * and leaves a balance factor far outside [-1, 1]. After each rotation both the
 * promoted subtree and the demoted node can still be unbalanced, so both are
 * rebalanced recursively. The loop repeats while the current root remains
 * unbalanced. Recursion depth is bounded by the tree height (O(log n)). */
static VellumNode *rebalance(VellumNode *node)
{
    if (node == NULL) {
        return NULL;
    }
    node_update(node);
    int balance = node_height(node->left) - node_height(node->right);
    while (balance > 1 || balance < -1) {
        if (balance > 1) {
            if (node_height(node->left->left) < node_height(node->left->right)) {
                node->left = rotate_left(node->left);
            }
            node = rotate_right(node);
            node->left = rebalance(node->left);
            node->right = rebalance(node->right);
        } else {
            if (node_height(node->right->right) < node_height(node->right->left)) {
                node->right = rotate_right(node->right);
            }
            node = rotate_left(node);
            node->left = rebalance(node->left);
            node->right = rebalance(node->right);
        }
        node_update(node);
        balance = node_height(node->left) - node_height(node->right);
    }
    return node;
}

static void node_destroy(VellumNode *node)
{
    if (node == NULL) {
        return;
    }
    node_destroy(node->left);
    node_destroy(node->right);
    free(node);
}

/* Removes the rightmost node from `tree`, returning that detached node and the
 * rebalanced remainder through `rest`. Both AVL properties remain valid. */
static VellumNode *pop_max(VellumNode *tree, VellumNode **rest)
{
    if (tree->right == NULL) {
        VellumNode *maximum = tree;
        *rest = tree->left;
        maximum->left = NULL;
        node_update(maximum);
        return maximum;
    }
    VellumNode *maximum = pop_max(tree->right, &tree->right);
    *rest = rebalance(tree);
    return maximum;
}

/* Concatenates two piece trees whose pieces are all ordered left before right. */
static VellumNode *tree_join(VellumNode *left, VellumNode *right)
{
    if (left == NULL) {
        return right;
    }
    if (right == NULL) {
        return left;
    }
    if (node_height(left) > node_height(right) + 1) {
        left->right = tree_join(left->right, right);
        return rebalance(left);
    }
    if (node_height(right) > node_height(left) + 1) {
        right->left = tree_join(left, right->left);
        return rebalance(right);
    }
    /* Comparable heights: the rightmost node of `left` is a balanced pivot. */
    VellumNode *rest = NULL;
    VellumNode *pivot = pop_max(left, &rest);
    pivot->left = rest;
    pivot->right = right;
    return rebalance(pivot);
}

/* Splits a tree at a byte offset. Both outputs are independent AVL trees; the
 * piece straddling the offset is divided using the caller's two reserved nodes.
 * All nodes are reserved before this helper runs, so it never fails. */
static void tree_split(VellumNode *tree, size_t offset, VellumNode **left, VellumNode **right,
                       VellumNode **spare_left, VellumNode **spare_right)
{
    if (tree == NULL) {
        *left = NULL;
        *right = NULL;
        return;
    }

    const size_t left_length = node_length(tree->left);
    if (offset < left_length) {
        VellumNode *first = NULL;
        VellumNode *second = NULL;
        tree_split(tree->left, offset, &first, &second, spare_left, spare_right);
        tree->left = second;
        tree = rebalance(tree);
        *left = first;
        *right = tree;
    } else if (offset > left_length + tree->piece.length) {
        VellumNode *first = NULL;
        VellumNode *second = NULL;
        tree_split(tree->right, offset - left_length - tree->piece.length, &first, &second,
                   spare_left, spare_right);
        tree->right = first;
        tree = rebalance(tree);
        *left = tree;
        *right = second;
    } else if (offset == left_length) {
        *left = tree->left;
        tree->left = NULL;
        tree = rebalance(tree);
        *right = tree;
    } else if (offset == left_length + tree->piece.length) {
        *right = tree->right;
        tree->right = NULL;
        tree = rebalance(tree);
        *left = tree;
    } else {
        /* Strictly inside this piece: two new pieces replace one node. */
        const size_t local = offset - left_length;
        const VellumPiece whole = tree->piece;
        VellumPiece first_piece = whole;
        first_piece.length = local;
        VellumPiece second_piece = whole;
        second_piece.offset += local;
        second_piece.length = whole.length - local;

        VellumNode *first = *spare_left;
        VellumNode *second = *spare_right;
        *spare_left = NULL;
        *spare_right = NULL;
        first->left = tree->left;
        first->right = NULL;
        first->piece = first_piece;
        second->left = NULL;
        second->right = tree->right;
        second->piece = second_piece;
        first = rebalance(first);
        second = rebalance(second);
        free(tree);
        *left = first;
        *right = second;
    }
}

static const VellumStore *store_for(const VellumBuffer *buffer, uint32_t store)
{
    return &buffer->stores[store == VELLUM_STORE_ADDED ? 1u : 0u];
}

static bool validate_node(const VellumNode *node, const VellumBuffer *buffer, size_t *total,
                          int *height)
{
    if (node == NULL) {
        *total = 0u;
        *height = 0;
        return true;
    }
    size_t left_total = 0u;
    size_t right_total = 0u;
    int left_height = 0;
    int right_height = 0;
    if (!validate_node(node->left, buffer, &left_total, &left_height) ||
        !validate_node(node->right, buffer, &right_total, &right_height)) {
        return false;
    }
    if (node->piece.length == 0u || node->piece.store > VELLUM_STORE_ADDED) {
        return false;
    }
    const VellumStore *store = store_for(buffer, node->piece.store);
    if (node->piece.offset > store->length ||
        node->piece.length > store->length - node->piece.offset) {
        return false;
    }
    const size_t expected = left_total + node->piece.length + right_total;
    const int expected_height = 1 + (left_height > right_height ? left_height : right_height);
    const int balance = left_height - right_height;
    if (node->length != expected || node->height != expected_height || balance < -1 ||
        balance > 1) {
        return false;
    }
    *total = expected;
    *height = expected_height;
    return true;
}

bool vellum_buffer_validate(const VellumBuffer *buffer)
{
    if (buffer == NULL) {
        return false;
    }
    size_t total = 0u;
    int unused_height = 0;
    if (!validate_node(buffer->root, buffer, &total, &unused_height)) {
        return false;
    }
    return total == buffer->length;
}

static bool append_bytes(VellumBuffer *buffer, const char *text, size_t length, size_t *offset,
                         VellumErrorInfo *error)
{
    VellumStore *store = &buffer->stores[1u];
    if (length > SIZE_MAX - store->length) {
        vellum_error_set(error, VELLUM_ERR_CAPACITY, "append store offset overflow");
        return false;
    }
    const size_t required = store->length + length;
    if (required > store->capacity) {
        size_t capacity = store->capacity == 0u ? VELLUM_INITIAL_ADD_CAPACITY : store->capacity;
        while (capacity < required) {
            if (capacity > SIZE_MAX / 2u) {
                capacity = required;
                break;
            }
            capacity *= 2u;
        }
        char *data = realloc(store->data, capacity);
        if (data == NULL) {
            vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not grow append store to %zu",
                             capacity);
            return false;
        }
        store->data = data;
        store->capacity = capacity;
    }
    if (length > 0u) {
        memcpy(store->data + store->length, text, length);
    }
    *offset = store->length;
    store->length = required;
    return true;
}

VellumBuffer *vellum_buffer_create(const char *text, size_t length, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (text == NULL && length != 0u) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "text is NULL with nonzero length");
        return NULL;
    }

    VellumBuffer *buffer = calloc(1u, sizeof(*buffer));
    if (buffer == NULL) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not allocate buffer state");
        return NULL;
    }
    if (length == 0u) {
        return buffer;
    }

    char *copy = malloc(length);
    if (copy == NULL) {
        free(buffer);
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not copy %zu original bytes",
                         length);
        return NULL;
    }
    memcpy(copy, text, length);
    buffer->stores[0u].data = copy;
    buffer->stores[0u].length = length;
    buffer->stores[0u].capacity = length;

    const VellumPiece piece = {VELLUM_STORE_ORIGINAL, 0u, length};
    buffer->root = node_create(piece);
    if (buffer->root == NULL) {
        free(copy);
        free(buffer);
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not allocate first piece");
        return NULL;
    }
    buffer->length = length;
    return buffer;
}

void vellum_buffer_destroy(VellumBuffer *buffer)
{
    if (buffer == NULL) {
        return;
    }
    node_destroy(buffer->root);
    free(buffer->stores[0u].data);
    free(buffer->stores[1u].data);
    free(buffer);
}

size_t vellum_buffer_length(const VellumBuffer *buffer)
{
    return buffer == NULL ? 0u : buffer->length;
}

static size_t count_nodes(const VellumNode *node)
{
    if (node == NULL) {
        return 0u;
    }
    return 1u + count_nodes(node->left) + count_nodes(node->right);
}

size_t vellum_buffer_piece_count(const VellumBuffer *buffer)
{
    return buffer == NULL ? 0u : count_nodes(buffer->root);
}

typedef struct {
    const VellumBuffer *buffer;
    size_t start;
    size_t end;
    char *out;
} ReadContext;

static void read_node(const VellumNode *node, size_t base, ReadContext *context)
{
    if (node == NULL || base >= context->end) {
        return;
    }
    if (base + node_length(node) <= context->start) {
        return;
    }
    read_node(node->left, base, context);

    const size_t piece_base = base + node_length(node->left);
    const size_t piece_end = piece_base + node->piece.length;
    if (piece_end > context->start && piece_base < context->end) {
        const size_t from = context->start > piece_base ? context->start - piece_base : 0u;
        const size_t to = context->end < piece_end ? context->end - piece_base : node->piece.length;
        const size_t take = to - from;
        if (take > 0u) {
            const VellumStore *store = store_for(context->buffer, node->piece.store);
            memcpy(context->out, store->data + node->piece.offset + from, take);
            context->out += take;
        }
    }
    read_node(node->right, piece_end, context);
}

bool vellum_buffer_read(const VellumBuffer *buffer, size_t offset, char *out, size_t length,
                        VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (buffer == NULL || (length > 0u && out == NULL)) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "read requires buffer and output");
        return false;
    }
    if (offset > buffer->length || length > buffer->length - offset) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "read range is outside the buffer");
        return false;
    }
    if (length == 0u) {
        return true;
    }

    ReadContext context = {buffer, offset, offset + length, out};
    read_node(buffer->root, 0u, &context);
    return true;
}

bool vellum_buffer_insert(VellumBuffer *buffer, size_t offset, const char *text, size_t length,
                          VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (buffer == NULL || (length > 0u && text == NULL)) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "insert requires buffer and text");
        return false;
    }
    if (offset > buffer->length) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "insert offset is outside the buffer");
        return false;
    }
    if (length == 0u) {
        return true;
    }
    if (length > SIZE_MAX - buffer->length) {
        vellum_error_set(error, VELLUM_ERR_CAPACITY, "buffer length overflow");
        return false;
    }

    /* Reserve all nodes before mutating the tree or the append store, so an
     * allocation failure leaves the buffer unchanged. */
    const VellumPiece empty = {VELLUM_STORE_ADDED, 0u, 0u};
    VellumNode *inserted = node_create((VellumPiece){VELLUM_STORE_ADDED, 0u, length});
    VellumNode *spare_left = node_create(empty);
    VellumNode *spare_right = node_create(empty);
    if (inserted == NULL || spare_left == NULL || spare_right == NULL) {
        node_destroy(inserted);
        node_destroy(spare_left);
        node_destroy(spare_right);
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not reserve insert pieces");
        return false;
    }

    size_t store_offset = 0u;
    if (!append_bytes(buffer, text, length, &store_offset, error)) {
        node_destroy(inserted);
        node_destroy(spare_left);
        node_destroy(spare_right);
        return false;
    }
    inserted->piece.offset = store_offset;

    VellumNode *left = NULL;
    VellumNode *right = NULL;
    tree_split(buffer->root, offset, &left, &right, &spare_left, &spare_right);
    node_destroy(spare_left); /* Unused when the split lands on a piece boundary. */
    node_destroy(spare_right);
    left = tree_join(left, inserted);
    buffer->root = tree_join(left, right);
    buffer->length += length;
    return true;
}

bool vellum_buffer_delete(VellumBuffer *buffer, size_t offset, size_t length,
                          VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (buffer == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "delete requires a buffer");
        return false;
    }
    if (offset > buffer->length || length > buffer->length - offset) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "delete range is outside the buffer");
        return false;
    }
    if (length == 0u) {
        return true;
    }

    const VellumPiece empty = {VELLUM_STORE_ORIGINAL, 0u, 0u};
    VellumNode *spares[4u] = {node_create(empty), node_create(empty), node_create(empty),
                              node_create(empty)};
    for (size_t i = 0u; i < 4u; ++i) {
        if (spares[i] == NULL) {
            for (size_t j = 0u; j < 4u; ++j) {
                node_destroy(spares[j]);
            }
            vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not reserve split pieces");
            return false;
        }
    }

    VellumNode *before = NULL;
    VellumNode *rest = NULL;
    tree_split(buffer->root, offset, &before, &rest, &spares[0u], &spares[1u]);
    VellumNode *removed = NULL;
    VellumNode *after = NULL;
    tree_split(rest, length, &removed, &after, &spares[2u], &spares[3u]);
    for (size_t i = 0u; i < 4u; ++i) {
        node_destroy(spares[i]);
    }
    node_destroy(removed);
    buffer->root = tree_join(before, after);
    buffer->length -= length;
    return true;
}

char *vellum_buffer_snapshot(const VellumBuffer *buffer, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (buffer == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "snapshot requires a buffer");
        return NULL;
    }
    if (buffer->length == SIZE_MAX) {
        vellum_error_set(error, VELLUM_ERR_CAPACITY, "buffer is too large to snapshot");
        return NULL;
    }
    char *copy = malloc(buffer->length + 1u);
    if (copy == NULL) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not allocate snapshot");
        return NULL;
    }
    if (buffer->length > 0u && !vellum_buffer_read(buffer, 0u, copy, buffer->length, error)) {
        free(copy);
        return NULL;
    }
    copy[buffer->length] = '\0';
    return copy;
}
