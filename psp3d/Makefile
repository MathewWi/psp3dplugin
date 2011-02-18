TARGET  = psp3d
OBJS    = main.o exports.o debug.o hook.o render3d.o gameinfo.o config.o
# Use only kernel libraries 
USE_KERNEL_LIBS = 1 
USE_KERNEL_LIBSC = 1

LIBS    = -lpspsystemctrl_kernel -lpspctrl_driver -lpspumd -lpspgum -lm
#CFLAGS  = -O2 -G0 -Wall
CFLAGS  = -O2 -G0 -Wall -fno-builtin-printf -DRELEASE -ffast-math
#CFLAGS  = -O2 -G0 -Wall -fno-builtin-printf
ASFLAGS = $(CFLAGS)

PSP_FW_VERSION = 371

PSPSDK=$(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build_prx.mak
