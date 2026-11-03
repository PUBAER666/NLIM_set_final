#include <stdint.h>
#include <cmath>
void accel_add(uint32_t, uint32_t, uint32_t *);
void accel_multiply(float, float, float *);
int main() {
    uint32_t result = 0;
    accel_add(0xffffffffu, 1u, &result);
    if (result != 0u) return 1;
    accel_add(123u, 456u, &result);
    if (result != 579u) return 2;
    float scaled = 0;
    accel_multiply(-1.25f, 2.0f, &scaled);
    if (scaled != -2.5f) return 3;
    accel_multiply(0.1f, 0.2f, &scaled);
    if (std::fabs(scaled - 0.02f) > 1e-7f) return 4;
    return 0;
}
