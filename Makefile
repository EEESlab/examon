
INIPARSER = ./lib/iniparser
MOSQUITTO = ./lib/mosquitto-1.3.5
PERFMON = ./lib/perfmon2-libpfm4
PMU_PUB = ./publishers/pmu_pub
COLLECTOR = ./collector


INSTALL_TARGET = $(PMU_PUB) 

SUBDIRS = $(INIPARSER) $(MOSQUITTO) $(PMU_PUB) $(PERFMON) $(COLLECTOR)

.PHONY: subdirs $(SUBDIRS) clean

subdirs: $(SUBDIRS)

clean:
	for dir in $(SUBDIRS); do \
	$(MAKE) -C $$dir clean; \
	done

install:
	for dir in $(INSTALL_TARGET); do \
	$(MAKE) -C $$dir install; \
	done

$(SUBDIRS):
	$(MAKE) -C $@

$(PMU_PUB): $(INIPARSER) $(MOSQUITTO) $(PERFMON)

