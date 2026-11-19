/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "vellum/core/error.h"
#include "vellum/graphics/draw_list.h"
#include "vellum/graphics/renderer.h"

static bool submit_with(VellumRenderer *renderer, VellumTextureHandle texture,
                        VellumErrorInfo *error)
{
    VellumDrawList *list = vellum_draw_list_create(4u, 4u);
    if (list == NULL) {
        return false;
    }
    const VellumColor tint = vellum_color(1.0f, 1.0f, 1.0f, 1.0f);
    bool ok = vellum_draw_list_rect(list, 0.0f, 0.0f, 4.0f, 4.0f, tint) &&
              vellum_draw_list_textured_quad(list, VELLUM_TEXTURE_NONE, 0.0f, 0.0f, 4.0f, 4.0f,
                                             0.5f, 0.5f, 0.5f, 0.5f, tint) &&
              vellum_draw_list_textured_quad(list, texture, 4.0f, 0.0f, 4.0f, 4.0f, 0.0f, 0.0f,
                                             1.0f, 1.0f, tint);
    if (ok) {
        ok = vellum_renderer_submit(renderer, list, error);
    }
    vellum_draw_list_destroy(list);
    return ok;
}

int main(void)
{
    (void)SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    (void)SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");

    bool ok = false;
    SDL_Window *window = NULL;
    VellumRenderer *renderer = NULL;
    VellumErrorInfo error;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        goto done;
    }
    window = SDL_CreateWindow("renderer textures", 16, 16, 0);
    if (window == NULL) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        goto done;
    }
    renderer = vellum_renderer_create(window, &error);
    if (renderer == NULL) {
        fprintf(stderr, "renderer create failed: %s\n", vellum_error_message(&error));
        goto done;
    }

    const uint8_t red[16] = {255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255};
    const uint8_t blue[16] = {0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255};
    const VellumTextureHandle handle =
        vellum_renderer_create_texture(renderer, 2u, 2u, red, &error);
    if (handle == VELLUM_TEXTURE_NONE) {
        fprintf(stderr, "texture create failed: %s\n", vellum_error_message(&error));
        goto done;
    }
    if (!vellum_renderer_update_texture(renderer, handle, blue, &error)) {
        fprintf(stderr, "texture update failed: %s\n", vellum_error_message(&error));
        goto done;
    }
    if (vellum_renderer_update_texture(renderer, VELLUM_TEXTURE_NONE, red, &error) ||
        error.code != VELLUM_ERR_INVALID_ARGUMENT) {
        fprintf(stderr, "invalid handle update unexpectedly succeeded\n");
        goto done;
    }

    const uint32_t before = vellum_renderer_draw_calls(renderer);
    /* One white rect batch, one white quad batch, one custom-texture quad batch. */
    if (!submit_with(renderer, handle, &error) ||
        vellum_renderer_draw_calls(renderer) != before + 3u) {
        fprintf(stderr, "submit failed: %s\n", vellum_error_message(&error));
        goto done;
    }
    if (submit_with(renderer, handle + 100u, &error) || error.code != VELLUM_ERR_INVALID_ARGUMENT ||
        vellum_renderer_draw_calls(renderer) != before + 3u) {
        fprintf(stderr, "stale handle submit unexpectedly succeeded\n");
        goto done;
    }

    vellum_renderer_destroy_texture(renderer, handle);
    if (submit_with(renderer, handle, &error) || error.code != VELLUM_ERR_INVALID_ARGUMENT) {
        fprintf(stderr, "destroyed handle submit unexpectedly succeeded\n");
        goto done;
    }
    ok = true;

done:
    vellum_renderer_destroy(renderer);
    if (window != NULL) {
        SDL_DestroyWindow(window);
    }
    SDL_Quit();
    printf("%s\n",
           ok ? "renderer texture integration: PASS" : "renderer texture integration: FAIL");
    return ok ? 0 : 1;
}
