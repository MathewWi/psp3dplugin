TARGET  = psp3d620
OBJS    = main.o debug.o hook.o config.o render3d.o gameinfo.o exports.o  

CFLAGS  = -O2 -G0 -Wall -fno-builtin-printf -ffast-math
ASFLAGS = $(CFLAGS) 
#LDFLAGS = -nodefaultlibs

BUILD_PRX = 1

USE_KERNEL_LIBS = 1
USE_KERNEL_LIBC = 1

LIBS = -lpspsystemctrl_kernel -lpspctrl_driver -lpsppower -lpspumd_driver -lpspgum -lm 

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build_prx.mak