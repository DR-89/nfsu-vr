#ifndef NFSU_ANDROID_LOGS_H
#define NFSU_ANDROID_LOGS_H

/* stdout/stderr must already point at the current nfsu1.log. */
int android_logs_start(void);
void android_logs_flush(void);

#endif
