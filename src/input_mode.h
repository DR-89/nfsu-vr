#ifndef NFSU_INPUT_MODE_H
#define NFSU_INPUT_MODE_H

/* Saved preference; Auto follows physical controller presence. */
typedef enum {
    INPUT_SENSOR_AUTO = -1,
    INPUT_SENSOR_OFF = 0,
    INPUT_SENSOR_ON = 1
} InputSensorPreference;

typedef struct {
    InputSensorPreference preference;
    int controller_present;
    int sensor_enabled;
    int overlay_requested;
    int overlay_enabled;
    unsigned revision;
} InputMode;

/* Each read is one consistent snapshot. Writers may run on different threads. */
InputMode input_mode_get(void);
int input_mode_sensor_enabled(void);
void input_mode_set_controller_present(int present);
void input_mode_configure(InputSensorPreference preference, int overlay);
#endif
