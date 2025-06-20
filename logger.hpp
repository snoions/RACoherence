#ifndef _LOGGER_H_
#define _LOGGER_H_

#define __FILENAME__ (__builtin_strrchr(__FILE__, '/') ? __builtin_strrchr(__FILE__, '/') + 1 : __FILE__)

#define LOG_LEVEL_DEBUG 1
#define LOG_LEVEL_INFO  2

#define CURRENT_LOG_LEVEL 3 // Set desired log level

#if CURRENT_LOG_LEVEL <= LOG_LEVEL_DEBUG
#define LOG_DEBUG(msg) std::cout << __FILENAME__ << ": " << msg << std::endl
#else
#define LOG_DEBUG(msg) // Empty macro, effectively removes debug logs
#endif

#if CURRENT_LOG_LEVEL <= LOG_LEVEL_INFO
#define LOG_INFO(msg) std::cout << __FILENAME__ << ": " << msg << std::endl
#else
#define LOG_INFO(msg) // Empty macro, effectively removes debug logs
#endif

#endif
