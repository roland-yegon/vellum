#include "vellum/graphics/renderer.h"

#include <SDL3/SDL.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct VellumRenderer {
    SDL_Renderer *renderer;
    SDL_Texture *white;
    SDL_Vertex *vertices;
    int *indices;
    size_t vertex_capacity;
    uint32_t draw_calls;
};

static bool ensure_capacity(VellumRenderer *backend, size_t quads, VellumErrorInfo *error)
{
    if (quads > (size_t)INT_MAX / 4u) {
        vellum_error_set(error, VELLUM_ERR_CAPACITY,
                         "draw list exceeds SDL vertex index range (%zu quads)", quads);
        return false;
    }

    const size_t needed_vertices = quads * 4u;
    if (needed_vertices <= backend->vertex_capacity) {
        return true;
    }

    size_t capacity = backend->vertex_capacity == 0u ? 1024u : backend->vertex_capacity;
    while (capacity < needed_vertices) {
        if (capacity > SIZE_MAX / 2u) {
            vellum_error_set(error, VELLUM_ERR_CAPACITY, "vertex buffer cannot grow further");
            return false;
        }
        capacity *= 2u;
    }

    SDL_Vertex *vertices = realloc(backend->vertices, capacity * sizeof(SDL_Vertex));
    int *indices = realloc(backend->indices, capacity / 4u * 6u * sizeof(int));
    if (vertices == NULL || indices == NULL) {
        if (vertices != NULL) {
            backend->vertices = vertices;
        }
        if (indices != NULL) {
            backend->indices = indices;
        }
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not resize geometry buffers");
        return false;
    }

    backend->vertices = vertices;
    backend->indices = indices;
    backend->vertex_capacity = capacity;

    for (size_t quad = 0u; quad < capacity / 4u; ++quad) {
        const int base = (int)(quad * 4u);
        indices[quad * 6u + 0u] = base;
        indices[quad * 6u + 1u] = base + 1;
        indices[quad * 6u + 2u] = base + 2;
        indices[quad * 6u + 3u] = base + 2;
        indices[quad * 6u + 4u] = base + 1;
        indices[quad * 6u + 5u] = base + 3;
    }
    return true;
}

static SDL_FColor to_sdl_color(VellumColor color)
{
    const SDL_FColor converted = {color.r, color.g, color.b, color.a};
    return converted;
}

static void append_vertex(SDL_Vertex *vertex, float x, float y, float u, float v, VellumColor color)
{
    vertex->position.x = x;
    vertex->position.y = y;
    vertex->tex_coord.x = u;
    vertex->tex_coord.y = v;
    vertex->color = to_sdl_color(color);
}

static void append_quad(SDL_Vertex *vertices, float x, float y, float width, float height, float u0,
                        float v0, float u1, float v1, VellumColor color)
{
    const float right = x + width;
    const float bottom = y + height;
    append_vertex(&vertices[0], x, y, u0, v0, color);
    append_vertex(&vertices[1], right, y, u1, v0, color);
    append_vertex(&vertices[2], x, bottom, u0, v1, color);
    append_vertex(&vertices[3], right, bottom, u1, v1, color);
}

VellumRenderer *vellum_renderer_create(struct SDL_Window *window, VellumErrorInfo *error)
{
    vellum_error_init(error);

    VellumRenderer *backend = malloc(sizeof(*backend));
    if (backend == NULL) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not allocate renderer state");
        return NULL;
    }

    backend->renderer = NULL;
    backend->white = NULL;
    backend->vertices = NULL;
    backend->indices = NULL;
    backend->vertex_capacity = 0u;
    backend->draw_calls = 0u;

    if (window == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "renderer requires a window");
        free(backend);
        return NULL;
    }

    /* NULL name lets SDL select the best available backend for this session. */
    backend->renderer = SDL_CreateRenderer(window, NULL);
    if (backend->renderer == NULL) {
        vellum_error_set_sdl(error, VELLUM_ERR_PLATFORM, "SDL_CreateRenderer");
        free(backend);
        return NULL;
    }

    const Uint32 white_pixel = 0xFFFFFFFFu;
    backend->white = SDL_CreateTexture(backend->renderer, SDL_PIXELFORMAT_RGBA8888,
                                       SDL_TEXTUREACCESS_STATIC, 1, 1);
    if (backend->white == NULL) {
        vellum_error_set_sdl(error, VELLUM_ERR_PLATFORM, "SDL_CreateTexture");
        SDL_DestroyRenderer(backend->renderer);
        free(backend);
        return NULL;
    }
    if (!SDL_UpdateTexture(backend->white, NULL, &white_pixel, 4) ||
        !SDL_SetTextureBlendMode(backend->white, SDL_BLENDMODE_BLEND) ||
        !SDL_SetRenderTextureAddressMode(backend->renderer, SDL_TEXTURE_ADDRESS_CLAMP,
                                         SDL_TEXTURE_ADDRESS_CLAMP)) {
        vellum_error_set_sdl(error, VELLUM_ERR_PLATFORM, "white texture setup");
        SDL_DestroyTexture(backend->white);
        SDL_DestroyRenderer(backend->renderer);
        free(backend);
        return NULL;
    }

    return backend;
}

bool vellum_renderer_submit(VellumRenderer *backend, const VellumDrawList *list,
                            VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (backend == NULL || list == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "submit requires renderer and list");
        return false;
    }

    size_t rect_count = 0u;
    size_t quad_count = 0u;
    const VellumRect *rects = vellum_draw_list_rects(list, &rect_count);
    const VellumTexturedQuad *quads = vellum_draw_list_quads(list, &quad_count);
    const size_t total = rect_count + quad_count;

    const VellumColor clear = vellum_draw_list_clear_color(list);
    if (!SDL_SetRenderDrawColor(backend->renderer, (Uint8)(clear.r * 255.0f),
                                (Uint8)(clear.g * 255.0f), (Uint8)(clear.b * 255.0f),
                                (Uint8)(clear.a * 255.0f)) ||
        !SDL_RenderClear(backend->renderer)) {
        vellum_error_set_sdl(error, VELLUM_ERR_PLATFORM, "SDL_RenderClear");
        return false;
    }

    if (total == 0u) {
        if (!SDL_RenderPresent(backend->renderer)) {
            vellum_error_set_sdl(error, VELLUM_ERR_PLATFORM, "SDL_RenderPresent");
            return false;
        }
        return true;
    }

    if (!ensure_capacity(backend, total, error)) {
        return false;
    }

    SDL_Vertex *vertices = backend->vertices;
    size_t vertex_index = 0u;
    for (size_t i = 0u; i < rect_count; ++i) {
        const VellumRect *rect = &rects[i];
        append_quad(&vertices[vertex_index], rect->x, rect->y, rect->width, rect->height, 0.5f,
                    0.5f, 0.5f, 0.5f, rect->color);
        vertex_index += 4u;
    }
    for (size_t i = 0u; i < quad_count; ++i) {
        const VellumTexturedQuad *quad = &quads[i];
        append_quad(&vertices[vertex_index], quad->x, quad->y, quad->width, quad->height, quad->u0,
                    quad->v0, quad->u1, quad->v1, quad->tint);
        vertex_index += 4u;
    }

    bool scissor_enabled = false;
    float sx = 0.0f;
    float sy = 0.0f;
    float sw = 0.0f;
    float sh = 0.0f;
    (void)vellum_draw_list_scissor(list, &scissor_enabled, &sx, &sy, &sw, &sh);
    SDL_Rect clip = {(int)sx, (int)sy, (int)sw, (int)sh};
    if (scissor_enabled && !SDL_SetRenderClipRect(backend->renderer, &clip)) {
        vellum_error_set_sdl(error, VELLUM_ERR_PLATFORM, "SDL_SetRenderClipRect");
        return false;
    }

    /* Rects and textured quads share one geometry call: rects sample the centre
     * of the white 1x1 texture and are coloured entirely per vertex. */
    const bool geometry_ok =
        SDL_RenderGeometry(backend->renderer, backend->white, vertices, (int)vertex_index,
                           backend->indices, (int)(total * 6u));
    if (scissor_enabled) {
        (void)SDL_SetRenderClipRect(backend->renderer, NULL);
    }
    if (!geometry_ok) {
        vellum_error_set_sdl(error, VELLUM_ERR_PLATFORM, "SDL_RenderGeometry");
        return false;
    }
    backend->draw_calls++;

    if (!SDL_RenderPresent(backend->renderer)) {
        vellum_error_set_sdl(error, VELLUM_ERR_PLATFORM, "SDL_RenderPresent");
        return false;
    }
    return true;
}

const char *vellum_renderer_name(const VellumRenderer *backend)
{
    if (backend == NULL || backend->renderer == NULL) {
        return "none";
    }
    const char *name = SDL_GetRendererName(backend->renderer);
    return name == NULL ? "unknown" : name;
}

uint32_t vellum_renderer_draw_calls(const VellumRenderer *backend)
{
    return backend == NULL ? 0u : backend->draw_calls;
}

size_t vellum_renderer_vertex_capacity(const VellumRenderer *backend)
{
    return backend == NULL ? 0u : backend->vertex_capacity;
}

void vellum_renderer_destroy(VellumRenderer *backend)
{
    if (backend == NULL) {
        return;
    }
    if (backend->white != NULL) {
        SDL_DestroyTexture(backend->white);
    }
    if (backend->renderer != NULL) {
        SDL_DestroyRenderer(backend->renderer);
    }
    free(backend->indices);
    free(backend->vertices);
    free(backend);
}
