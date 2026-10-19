#pragma once

#include <cstdint>

#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
#include <xmmintrin.h>
#endif

// Recursive filters, reverbs and envelopes decay toward zero during silence and
// end up in the subnormal float range, where x86 takes a ~100-cycle microcode
// assist per operation. Flushing them to zero costs nothing audible (< -700 dBFS).
// The mode is per thread, so every thread that runs DSP must call this.
inline void enable_flush_denormals_to_zero() {
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
    constexpr unsigned int kFlushToZero = 0x8000;
    constexpr unsigned int kDenormalsAreZero = 0x0040;
    _mm_setcsr(_mm_getcsr() | kFlushToZero | kDenormalsAreZero);
#elif defined(__aarch64__)
    uint64_t fpcr = 0;
    asm volatile("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= (uint64_t{1} << 24);
    asm volatile("msr fpcr, %0" : : "r"(fpcr));
#endif
}
