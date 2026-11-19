#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tests/unit/tests.h"
#include "vellum/core/array.h"

bool test_array(void)
{
    VellumArray array;
    if (vellum_array_init(&array, 0u, 0u) || vellum_array_init(NULL, sizeof(uint32_t), 0u)) {
        return false;
    }
    if (!vellum_array_init(&array, sizeof(uint32_t), 4u)) {
        return false;
    }
    if (vellum_array_size(&array) != 0u || vellum_array_byte_size(&array) != 0u ||
        vellum_array_at(&array, 0u) != NULL) {
        vellum_array_deinit(&array);
        return false;
    }

    for (uint32_t i = 0u; i < 32u; ++i) {
        if (!vellum_array_append(&array, &i)) {
            vellum_array_deinit(&array);
            return false;
        }
    }
    if (vellum_array_size(&array) != 32u || vellum_array_byte_size(&array) != 32u * 4u) {
        vellum_array_deinit(&array);
        return false;
    }
    for (uint32_t i = 0u; i < 32u; ++i) {
        const uint32_t *value = vellum_array_at_const(&array, i);
        if (value == NULL || *value != i) {
            vellum_array_deinit(&array);
            return false;
        }
    }

    if (vellum_array_at(&array, 32u) != NULL || vellum_array_append(&array, NULL)) {
        vellum_array_deinit(&array);
        return false;
    }

    vellum_array_remove_swap(&array, 0u);
    if (vellum_array_size(&array) != 31u) {
        vellum_array_deinit(&array);
        return false;
    }
    const uint32_t *moved = vellum_array_at_const(&array, 0u);
    if (moved == NULL || *moved != 31u) {
        vellum_array_deinit(&array);
        return false;
    }

    vellum_array_clear(&array);
    if (vellum_array_size(&array) != 0u) {
        vellum_array_deinit(&array);
        return false;
    }
    void *element = vellum_array_push(&array);
    if (element == NULL || *(const uint32_t *)element != 0u) {
        vellum_array_deinit(&array);
        return false;
    }

    vellum_array_deinit(&array);
    return array.data == NULL && array.capacity == 0u;
}
