# Collector Makefile
# © 2017 ETH Zurich, 
#   [Integrated System Laboratory, D-ITET], 
#   [Antonio Libri, a.libri@iis.ee.ethz.ch]
# 
# © 2017 University of Bologna, 
#   [Department of Electrical, Electronic and Information Engineering, DEI],
#   [Andrea Bartolini, a.bartolini@unibo.it]

LIBMOSQ = ../lib/mosquitto-1.3.5/lib/libmosquitto.a
MOSQUITTO = ../lib/mosquitto-1.3.5
INC = -I../lib/mosquitto-1.3.5/lib
LIBS = $(LIBMOSQ) -lssl -lcrypto

all: lib
	$(MAKE) -C $(MOSQUITTO)
	gcc collector-example.c collector.c -o collector-example -g -fopenmp $(INC) $(LIBS)

lib:
	gcc -c collector.c $(INC) -fopenmp -g -o collector.o
	ar rcs libcollector.a collector.o


clean:
	$(MAKE) -C $(MOSQUITTO) $$dir clean;
	rm collector-example
	rm *.a
