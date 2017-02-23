/* 
 * File:   perf_event_lib.h
 * 
 * (c) 2017 ETH Zurich, [Integrated System Laboratory, D-ITET] 
 * (c) 2017 University of Bologna, [Department of Electrical, Electronic and Information Engineering, DEI]
 *
 * Contributed by:
 * Francesco Beneventi <francesco.beneventi@unibo.it>
 * Andrea Bartolini	<barandre@iis.ee.ethz.ch>
 *
 * Created on 12 dicembre 2016, 15.44
 */

#ifndef PERF_EVENT_LIB_H
#define	PERF_EVENT_LIB_H

#define PERF_QUIET_MODE
#define MAX_PMC         8       // per-core
#define PERF_MAX_EVENTS 64


//typedef struct read_format {
//	uint64_t nr;
//	uint64_t time_enabled;
//	uint64_t time_running;
//	uint64_t value[PERF_MAX_EVENTS];
//}perf_read_format;

typedef struct read_format {
        uint64_t value;
	uint64_t time_enabled;
	uint64_t time_running;
	uint64_t id;
}perf_read_format;

typedef struct {
    uint64_t event_code[MAX_PMC];       //event raw code
    int event_pmu_idx[MAX_PMC];         //event index in the core PMU
}core_pmu_events_t;


inline uint64_t perf_scale(perf_read_format *event);
inline double perf_scale_ratio(perf_read_format *event);


#ifdef DEBUG
    uint64_t before,after;
#endif


#endif	/* PERF_EVENT_LIB_H */

