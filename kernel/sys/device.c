#include "device.h"
#include "kstring.h"

static device_info_t registry[DEVICE_REGISTRY_CAP];
static int registry_used;

void device_registry_init(void) {
    for (int i = 0; i < DEVICE_REGISTRY_CAP; i++) {
        registry[i].name[0] = '\0';
        registry[i].driver[0] = '\0';
        registry[i].device_class = DEVICE_CLASS_SYSTEM;
        registry[i].state = DEVICE_STATE_OFFLINE;
    }
    registry_used = 0;
}

int device_register(const char *name, const char *driver,
                    enum device_class type, enum device_state state) {
    if (!name || !name[0] || !driver || !driver[0] ||
        type < DEVICE_CLASS_SYSTEM || type > DEVICE_CLASS_NETWORK ||
        state < DEVICE_STATE_READY || state > DEVICE_STATE_OFFLINE ||
        kstrnlen(name, DEVICE_NAME_CAP) >= DEVICE_NAME_CAP ||
        kstrnlen(driver, DEVICE_DRIVER_CAP) >= DEVICE_DRIVER_CAP)
        return -1;

    for (int i = 0; i < registry_used; i++) {
        if (kstrcmp(registry[i].name, name) == 0) {
            kstrlcpy(registry[i].driver, driver, DEVICE_DRIVER_CAP);
            registry[i].device_class = (u8)type;
            registry[i].state = (u8)state;
            return i;
        }
    }
    if (registry_used >= DEVICE_REGISTRY_CAP) return -1;

    int slot = registry_used++;
    kstrlcpy(registry[slot].name, name, DEVICE_NAME_CAP);
    kstrlcpy(registry[slot].driver, driver, DEVICE_DRIVER_CAP);
    registry[slot].device_class = (u8)type;
    registry[slot].state = (u8)state;
    return slot;
}

int device_set_state(const char *name, enum device_state state) {
    if (!name || state < DEVICE_STATE_READY || state > DEVICE_STATE_OFFLINE)
        return -1;
    for (int i = 0; i < registry_used; i++) {
        if (kstrcmp(registry[i].name, name) == 0) {
            registry[i].state = (u8)state;
            return 0;
        }
    }
    return -1;
}

int device_count(void) { return registry_used; }

const device_info_t *device_get(int index) {
    if (index < 0 || index >= registry_used) return 0;
    return &registry[index];
}

const device_info_t *device_find(const char *name) {
    if (!name) return 0;
    for (int i = 0; i < registry_used; i++)
        if (kstrcmp(registry[i].name, name) == 0) return &registry[i];
    return 0;
}

const char *device_class_name(enum device_class type) {
    switch (type) {
        case DEVICE_CLASS_SYSTEM: return "system";
        case DEVICE_CLASS_STORAGE: return "storage";
        case DEVICE_CLASS_DISPLAY: return "display";
        case DEVICE_CLASS_INPUT: return "input";
        case DEVICE_CLASS_TIMER: return "timer";
        case DEVICE_CLASS_AUDIO: return "audio";
        case DEVICE_CLASS_NETWORK: return "network";
        default: return "unknown";
    }
}

const char *device_state_name(enum device_state state) {
    switch (state) {
        case DEVICE_STATE_READY: return "ready";
        case DEVICE_STATE_DEGRADED: return "degraded";
        case DEVICE_STATE_OFFLINE: return "offline";
        default: return "unknown";
    }
}
