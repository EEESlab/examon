
CC = gcc
CFLAGS = -std=gnu89
INSTALL = install
PREFIX=./bin
INC = -I../../lib/iniparser/src -I../../lib/mosquitto-1.3.5/lib -I../../lib/perfmon2-libpfm4/include/
LDIR = -L/usr/lib -L../../lib/iniparser
LIBMOSQ = ../../lib/mosquitto-1.3.5/lib/libmosquitto.a
LIBPFM = ../../lib/perfmon2-libpfm4/lib/libpfm.a
LIBS = -lm $(LIBMOSQ) $(LIBPFM) -lssl -lcrypto -lrt -lpthread -liniparser
FILES = pmu_pub.c sensor_read_lib.c perf_event_lib.c
TARGET = pmu_pub
DESTDIR=$(PREFIX)
CFGFILE=pmu_pub.conf host_whitelist


build: $(FILES)
	
	$(CC) $(CFLAGS) -o $(TARGET) $(FILES) $(INC) $(LDIR) $(LIBS)
	#sudo setcap cap_sys_rawio=ep $(TARGET)

install:
	@echo building: $(TARGET)
	-mkdir -p $(DESTDIR)
	$(INSTALL) -m 744 $(TARGET) $(DESTDIR)
	$(INSTALL) -m 644 $(CFGFILE) $(DESTDIR)
	
clean:
	rm -f $(TARGET)
	rm -f ./bin/$(TARGET)

rebuild: clean build
