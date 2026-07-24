#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

int64_t kond_ffi_add(int64_t left, int64_t right) {
    return left + right;
}

double kond_ffi_scale(double value, double factor) {
    return value * factor;
}

int64_t kond_ffi_is_even(int64_t value) {
    return value % 2 == 0 ? 1 : 0;
}

const char *kond_ffi_greeting(const char *name) {
    static char result[128];
    if (name == NULL) return NULL;
    (void)snprintf(result, sizeof(result), "hello, %s", name);
    return result;
}

void kond_ffi_noop(const char *message) {
    (void)message;
}
