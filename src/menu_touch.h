#ifndef NFSU_MENU_TOUCH_H
#define NFSU_MENU_TOUCH_H

/* SDL thread publishes normalized surface positions; guest thread consumes. */
enum MenuTouchEvent { MENU_TOUCH_DOWN, MENU_TOUCH_MOVE, MENU_TOUCH_UP };
typedef struct {
    float x, y;
    int width, height;
    unsigned down, active, reset, prepare;
} MenuTouchSample;

void menu_touch_event(enum MenuTouchEvent event, float x, float y, int width, int height);
void menu_touch_cancel(void);
MenuTouchSample menu_touch_sample(void);
int menu_touch_position(const MenuTouchSample *sample, int wide, float menu_size,
                        short *x, short *y);

#endif
