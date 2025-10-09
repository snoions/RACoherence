#ifdef ENABLE_RAC_IGNORE
#define RAC_IGNORE_BLOCK(STMT) \
    do { \
        asm volatile ("#ANNOTATE rac_ignore_begin"); \
        STMT \
        asm volatile ("#ANNOTATE rac_ignore_end"); \
    } while(0);
#else
#define RAC_IGNORE_BLOCK(STMT) STMT
#endif
