/**
 * @file    logger.h
 * @brief   Tiny printf based logger, kept API-compatible with the vendor BSP
 *          sources that were reused in this project.
 *
 * Output goes through _write() in Core/Src/syscalls.c, i.e. the ITM/SWO port.
 * Set LOG_LEVEL to LOG_NONE to compile every call site away.
 */
#ifndef __LOGGER_H
#define __LOGGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

#define LOG_NONE    0
#define LOG_ERROR   1
#define LOG_WARN    2
#define LOG_INFO    3
#define LOG_DEBUG   4

#ifndef LOG_LEVEL
#define LOG_LEVEL   LOG_INFO
#endif

static inline const char *log_tag(int level)
{
    switch (level)
    {
    case LOG_ERROR: return "E";
    case LOG_WARN:  return "W";
    case LOG_INFO:  return "I";
    default:        return "D";
    }
}

/**
 * @brief PRINT_LOG(level, tick, fmt, ...)
 *        level: LOG_ERROR / LOG_WARN / LOG_INFO / LOG_DEBUG
 *        tick : timestamp, normally HAL_GetTick()
 */
#define PRINT_LOG(level, tick, fmt, ...)                                    \
    do {                                                                    \
        if ((level) <= LOG_LEVEL)                                           \
        {                                                                   \
            printf("[%s][%8lu] " fmt "\r\n", log_tag(level),                \
                   (unsigned long)(tick), ##__VA_ARGS__);                   \
        }                                                                   \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* __LOGGER_H */
