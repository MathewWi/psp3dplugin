TARGET  = psp3d
OBJS    = main.o exports.o debug.o hook.o hook_display.o
# Use only kernel libraries 
USE_KERNEL_LIBS = 1 

LIBS    = -lc -lpspsystemctrl_kernel -lpspctrl_driver -lpsppower_driver -lpspdisplay_driver -lpspgu -lpspgum -lm
CFLAGS  = -O2 -G0 -Wall
#CFLAGS  = -O2 -G0 -Wall -fno-builtin-printf -DRELEASE
#CFLAGS  = -O2 -G0 -Wall -fno-builtin-printf
ASFLAGS = $(CFLAGS)

PSPSDK=$(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build_prx.mak
