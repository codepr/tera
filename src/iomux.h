#pragma once
#include <sys/types.h>

typedef struct iomux IO_Mux;
typedef enum iomux_event {
    IOMUX_READ  = 1 << 0, // 0x01
    IOMUX_WRITE = 1 << 1, // 0x02
} IO_Mux_Event;

IO_Mux *iomux_create(void);
void iomux_free(IO_Mux *mux);

int iomux_add(IO_Mux *mux, int fd, IO_Mux_Event events);
int iomux_del(IO_Mux *mux, int fd);
int iomux_wait(IO_Mux *mux, time_t timeout_ms);

int iomux_get_event_fd(IO_Mux *mux, int index);
IO_Mux_Event iomux_get_event_flags(IO_Mux *mux, int index);
