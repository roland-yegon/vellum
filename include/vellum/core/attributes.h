#ifndef VELLUM_CORE_ATTRIBUTES_H
#define VELLUM_CORE_ATTRIBUTES_H

#if defined(__GNUC__) || defined(__clang__)
#define VELLUM_PRINTF_ATTR(fmt_index, args_index) \
    __attribute__((format(printf, fmt_index, args_index)))
#else
#define VELLUM_PRINTF_ATTR(fmt_index, args_index)
#endif

#endif /* VELLUM_CORE_ATTRIBUTES_H */
