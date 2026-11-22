// Linux stand-in for <mmsystem.h>: the engine only needs the millisecond multimedia timer.
#pragma once
#include <cstdint>
#include <ctime>
inline uint32_t timeGetTime()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000ull + ts.tv_nsec / 1000000);
}
inline uint32_t timeBeginPeriod(uint32_t) { return 0; } // TIMERR_NOERROR; Linux timers are already fine-grained
inline uint32_t timeEndPeriod(uint32_t) { return 0; }
