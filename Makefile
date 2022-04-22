# msys2
CC = gcc
FLAGS = -std=c11 -pedantic -Wpedantic -Wall -O3 -DSDL_MAIN_HANDLED -DENABLE_SL6

LIBS = -lSDL2
# comment these two lines if you are under Linux :
WIN32-LIBS = -lwinmm -limm32 -lole32 -loleaut32 -lversion -lgdi32 -lgdiplus -lsetupapi -lcomdlg32

LD_FLAGS = -static -Wl,-subsystem,windows

WIN32-RES = reinetteII+.res

all: reinetteIIplus

reinetteII+.res: reinetteII+.rc
	windres $^ -O coff -o $(WIN32-RES)

reinetteIIplus: reinetteII+.c puce6502.c $(WIN32-RES)
	$(CC) $^ $(FLAGS) $(LIBS) $(WIN32-LIBS) $(LD_FLAGS) -o $@
