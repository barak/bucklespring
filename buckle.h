#ifndef buckle_h
#define buckle_h

int play(int code, int press);
int repeat_enabled(void);
void repeat_override(int *delay_ms, int *period_ms);
int scan(int verbose);
void printd(const char *fmt, ...);
void open_console(void);

/* wl-repeat.c, built only for the libinput backend */

int wl_repeat_open(void);
int wl_repeat_dispatch(void);
int wl_repeat_get(int *delay_ms, int *period_ms);

#endif
