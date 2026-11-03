#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*dx_range_fn)(void *ctx, int begin, int end, int worker);

void dx_pool_set_threads(int n);
int dx_pool_threads(void);

int dx_pool_auto_threads(void);

void dx_parallel_for(int count, int chunk, int minParallel, dx_range_fn fn, void *ctx);

#ifdef __cplusplus
}
#endif
