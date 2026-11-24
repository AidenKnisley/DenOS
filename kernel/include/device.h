#ifndef DENOS_DEVICE_H
#define DENOS_DEVICE_H

#include "types.h"

#define DEVICE_NAME_CAP 24
#define DEVICE_DRIVER_CAP 24
#define DEVICE_REGISTRY_CAP 16

enum device_class {
    DEVICE_CLASS_SYSTEM = 0,
    DEVICE_CLASS_STORAGE,
    DEVICE_CLASS_DISPLAY,
    DEVICE_CLASS_INPUT,
    DEVICE_CLASS_TIMER,
    DEVICE_CLASS_AUDIO,
    DEVICE_CLASS_NETWORK
};

enum device_state {
    DEVICE_STATE_READY = 0,
    DEVICE_STATE_DEGRADED,
    DEVICE_STATE_OFFLINE
};

typedef struct device_info {
    char name[DEVICE_NAME_CAP];
    char driver[DEVICE_DRIVER_CAP];
    u8 device_class;
    u8 state;
} device_info_t;

void device_registry_init(void);
int device_register(const char *name, const char *driver,
                    enum device_class type, enum device_state state);
int device_set_state(const char *name, enum device_state state);
int device_count(void);
const device_info_t *device_get(int index);
const device_info_t *device_find(const char *name);
const char *device_class_name(enum device_class type);
const char *device_state_name(enum device_state state);

#endif
