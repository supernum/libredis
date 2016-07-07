include ./MAKEFILE

ifeq ($(ostype), SunOS)
endif
ifeq ($(ostype), AIX)
endif
ifeq ($(ostype), Linux)
endif


LIBNAME = libredis
DYLIBSUFFIX=so
STLIBSUFFIX=a
DYLIBNAME=$(LIBNAME).$(DYLIBSUFFIX)
DYLIB_MAKE_CMD=$(CC) -fPIC -shared -o $(DYLIBNAME) 
STLIBNAME=$(LIBNAME).$(STLIBSUFFIX)
STLIB_MAKE_CMD=ar rcs $(STLIBNAME)

INC = -I./

OBJ = ccds.o
OBJ += ccsocket.o
OBJ += ccel.o
OBJ += libredis.o

ALL: $(DYLIBNAME) $(STLIBNAME)

$(DYLIBNAME): $(OBJ)
	$(DYLIB_MAKE_CMD) $^ $(MODULE) 

$(STLIBNAME): $(OBJ)
	$(STLIB_MAKE_CMD) $^ $(MODULE)
    
%.o : %.c 
	$(CC) -c -fPIC $< $(INC) 

.PHONY:clean
clean:
	rm -f $(OBJ) $(DYLIBNAME) $(STLIBNAME)

