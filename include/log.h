#ifndef _MCFS_LOG_H
#define _MCFS_LOG_H

#include "common_headers.h"

struct logger {
    FILE *file;
    /* name: relative or absolute path to the log file, but excludes
     * the '.log' extension suffix. */
    char *name;
    size_t bytes_written;
    /* To be assigned from stat.st_mode */
    mode_t type;
};

struct log_entry {
    struct logger *dest;
    size_t loglen;
    char *content;
};

void submit_log(struct logger *dest, const char *fmt, ...);
void submit_message(const char *fmt, ...);
void vsubmit_message(const char *fmt, va_list args);
void submit_error(const char *fmt, ...);
void vsubmit_error(const char *fmt, va_list args);
void submit_seq(const char *fmt, ...);
void vsubmit_seq(const char *fmt, va_list args);
void make_logger(struct logger *lgr, const char *name, FILE *default_fp);
void init_log_daemon(const char *output_log_name, const char *err_log_name,
        const char *seq_name);
void destroy_log_daemon();

#endif