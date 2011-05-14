TARGET  = psp3d
OBJS    = main.o debug.o config.o render3d.o gameinfo.o blit.o exports.o  

CFLAGS  = -O2 -G0 -Wall -g #-ffast-math -fno-builtin-printf
ASFLAGS = $(CFLAGS) 
#LDFLAGS = -nodefaultlibs

BUILD_PRX = 1

USE_KERNEL_LIBS = 1
USE_KERNEL_LIBC = 1

LIBS = -lpspsystemctrl_kernel -lpspctrl_driver -lpsppower -lpsprtc_driver -lpspumd_driver -lpspge -lpspgum -lm 

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build_prx.mak