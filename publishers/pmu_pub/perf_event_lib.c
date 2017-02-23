/* 
 * File:   perf_event_lib.c
 * 
 * (c) 2017 ETH Zurich, [Integrated System Laboratory, D-ITET] 
 * (c) 2017 University of Bologna, [Department of Electrical, Electronic and Information Engineering, DEI]
 *
 * Contributed by:
 * Francesco Beneventi <francesco.beneventi@unibo.it>
 * Andrea Bartolini	<barandre@iis.ee.ethz.ch>
 *
 * Created on 12 dicembre 2016, 15.32
 */

#include <sys/types.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/syscall.h>

#include "perfmon/err.h"
#include "perfmon/pfmlib_perf_event.h"
#include "perf_event_lib.h"
#include "pmu_pub.h"
#include "sensor_read_lib.h"

int _perf_event_open(struct perf_event_attr *attr, pid_t pid, int cpu, int group_fd, unsigned long flags) {
    return syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
}

int pmu_is_present(pfm_pmu_t p) {
    pfm_pmu_info_t pinfo;
    int ret;

    memset(&pinfo, 0, sizeof (pinfo));
    ret = pfm_get_pmu_info(p, &pinfo);
    return ret == PFM_SUCCESS ? pinfo.is_present : 0;
}

int perf_program_core_events(struct perf_event_attr *attr, int ncore, int **fd, int group, int idx) {

    int core;
    int leader_counter = -1;

    for (core = 0; core < ncore; core++) {
        set_cpu_affinity(core);

        if (group == 1) {
            if (idx == 0) {
                leader_counter = -1;
            } else {
                leader_counter = fd[core][0];
            }
        } else {
            leader_counter = -1;
        }

        fd[core][idx] = _perf_event_open(attr, -1, core, leader_counter, 0);
        DEBUGMSG(stderr, "file desc core %d, event %d: %d\n", core, idx, fd[core][idx]);
        if (fd[core][idx] < 0) {
            errx(1, "Failed adding event %d %d\n", idx, fd[core][idx]);
            return -1;
        }


        if (group == 1) {
            if (idx == 0) {
                if (ioctl(fd[core][0], PERF_EVENT_IOC_ENABLE, 0)) {
                    perror("ioctl(PERF_EVENT_IOC_ENABLE");
                }
            }
        } else {
            if (ioctl(fd[core][idx], PERF_EVENT_IOC_ENABLE, 0)) {
                perror("ioctl(PERF_EVENT_IOC_ENABLE");
            }
        }
    }
    DEBUGMSG(stderr, "perf_program_core_events finished\n");
    return 0;

}

int perf_program_uncore_events(struct perf_event_attr *attr, int ncpu, int ncore, int **fd, int group, int idx) {

    int cpu, core;
    int leader_counter = -1;
    int* temp = NULL;


    for (core = 0; core < ncore; core++) {
        set_cpu_affinity(core);

        if (group == 1) {
            if (idx == 0) {
                leader_counter = -1;
            } else {
                leader_counter = fd[core][0];
            }
        } else {
            leader_counter = -1;
        }

        if ((core % (ncore / ncpu)) == 0) {
            printf(" Uncore event detected: Programming one counter on socket: %d ref core: %d\n", (int) (core / (ncore / ncpu)), core);
            fd[core][idx] = _perf_event_open(attr, -1, core, leader_counter, 0);
            DEBUGMSG(stderr, "file desc core %d, event %d: %d\n", core, idx, fd[core][idx]);
            if (fd[core][idx] < 0) {
                errx(1, "Failed adding event %d %d\n", idx, fd[core][idx]);
                return -1;
            }
        } else {
            //fd[core][idx] = -1;
            continue;
        }

        if (group == 1) {
            if (idx == 0) {
                if (ioctl(fd[core][0], PERF_EVENT_IOC_ENABLE, 0)) {
                    perror("ioctl(PERF_EVENT_IOC_ENABLE");
                }
            }
        } else {
            if (ioctl(fd[core][idx], PERF_EVENT_IOC_ENABLE, 0)) {
                perror("ioctl(PERF_EVENT_IOC_ENABLE");
            }
        }
    }

    return 0;
}

int perf_program_os_events(int num_events, const char **events, int **fd, struct sys_data * sysd) {

    pfm_pmu_info_t pinfo;
    struct perf_event_attr attr;
    pfm_perf_encode_arg_t e;
    const char *arg[PERF_MAX_EVENTS];
    const char **p;
    char *fqstr;
    pfm_event_info_t info;
    int i, j, ret, cpu, core;
    int total_supported_events = 0;
    int total_available_events = 0;
    int leader_counter = -1;
    int group = 0;
    int num_core_events = 0;


    ret = pfm_initialize();
    if (ret != PFM_SUCCESS)
        errx(1, "cannot initialize library: %s\n", pfm_strerror(ret));

    memset(&attr, 0, sizeof (struct perf_event_attr));
    memset(&pinfo, 0, sizeof (pinfo));
    memset(&info, 0, sizeof (info));

    DEBUGMSG(stderr, "Supported PMU models:\n");
    for (i = 0; i < PFM_PMU_MAX; i++) {
        ret = pfm_get_pmu_info(i, &pinfo);
        if (ret != PFM_SUCCESS)
            continue;
        DEBUGMSG(stderr, "\t[%d, %s, \"%s\"]\n", i, pinfo.name, pinfo.desc);
    }

    DEBUGMSG(stderr, "Detected PMU models:\n");
    for (i = 0; i < PFM_PMU_MAX; i++) {
        ret = pfm_get_pmu_info(i, &pinfo);
        if (ret != PFM_SUCCESS)
            continue;
        if (pinfo.is_present) {
            DEBUGMSG(stderr, "\t[%d, %s, \"%s\"]\n", i, pinfo.name, pinfo.desc);
            total_supported_events += pinfo.nevents;
        }
        total_available_events += pinfo.nevents;
    }

    DEBUGMSG(stderr, "Total events: %d available, %d supported\n", total_available_events, total_supported_events);


    if (num_events == 0) {
        return 0;
    } else {
        p = events;
    }

    if (!*p)
        errx(1, "you must pass at least one event");

    i = 0;
    memset(&e, 0, sizeof (e));

    if (!sysd->use_perf) {
        sysd->core_pmu_events = malloc(sysd->NCORE * sizeof (core_pmu_events_t));
    }

    while (num_events) {
        e.size = sizeof (e);
        DEBUGMSG(stderr, "size of e: %d\n", e.size);
        e.attr = &attr;
        fqstr = NULL;
        e.fstr = &fqstr;
        DEBUGMSG(stderr, "encoding event:\t%s\n", *p);
        ret = pfm_get_os_event_encoding(*p, PFM_PLM0 | PFM_PLM3, PFM_OS_PERF_EVENT_EXT, &e);
        if (ret != PFM_SUCCESS) {
            if (ret == PFM_ERR_NOTFOUND && strstr(*p, "::"))
                errx(1, "%s: try setting LIBPFM_ENCODE_INACTIVE=1", pfm_strerror(ret));
            errx(1, "cannot encode event %s: %s", *p, pfm_strerror(ret));
        }
        ret = pfm_get_event_info(e.idx, PFM_OS_PERF_EVENT_EXT, &info);
        if (ret != PFM_SUCCESS)
            errx(1, "cannot get event info: %s", pfm_strerror(ret));

        ret = pfm_get_pmu_info(info.pmu, &pinfo);
        if (ret != PFM_SUCCESS)
            errx(1, "cannot get PMU info: %s", pfm_strerror(ret));

        printf("Requested Event : %s\n", *p);
        printf("Actual    Event : %s\n", fqstr);
        printf("PMU             : %s\n", pinfo.desc);
        printf("Name            : %s\n", info.name);
        printf("IDX             : %d\n", e.idx);
        printf("Config          : %#" PRIx64 "\n", attr.config);
        printf("Type            : %u\n", attr.type);
        printf("read_format     : %lu\n", attr.read_format);
        printf("Pinned          : %lu\n", attr.pinned);
        printf("disabled        : %lu\n", attr.disabled);
        printf("Exclude_kernel  : %lu\n", attr.exclude_kernel);
        printf("Exclude_hv      : %lu\n", attr.exclude_hv);
        printf("size            : %d\n", attr.size);
        printf("sizeof(struct perf_event_attr)  :%d\n", sizeof (struct perf_event_attr));

        //attr.read_format = PERF_FORMAT_GROUP |
        //                  PERF_FORMAT_TOTAL_TIME_ENABLED |
        //                  PERF_FORMAT_TOTAL_TIME_RUNNING;
        attr.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED |
                PERF_FORMAT_TOTAL_TIME_RUNNING;

        printf("\nStart PMU programming for event %s, index: %d\n", *p, i);

        if (strstr(pinfo.desc, "uncore")) { //check if uncore event
            sysd->is_uncore_event[i] = 1;
            perf_program_uncore_events(&attr, sysd->NCPU, sysd->NCORE, fd, group, i);
        } else {
            if (sysd->use_perf) {
                perf_program_core_events(&attr, sysd->NCORE, fd, group, i);
            } else {
                if (num_core_events < sysd->PMC_NUM) {
                    printf("programming for event %s, total: %d\n", *p, num_core_events);
                    perf_program_core_events(&attr, sysd->NCORE, fd, group, num_core_events);
                    for (core = 0; core < sysd->NCORE; core++) {//save event config
                        sysd->core_pmu_events[core].event_code[num_core_events] = attr.config;
                        DEBUGMSG(stderr, "[DEBUG]: core[%d].PMU[%d].event[0x%"PRIx64"]\n", core, num_core_events, sysd->core_pmu_events[core].event_code[num_core_events]);
                    }
                    num_core_events++;
                }
            }
        }

        i++;
        free(fqstr);
        p++;
        num_events--;
    }
    if (!sysd->use_perf) {
        sysd->num_core_events = num_core_events;
        perf_assign_pmu_idx(sysd);
    }

    printf("PMU programming finished!\n");

    return 0;
}

int perf_program_raw_events(int num_events, const char **events, int **fd, struct sys_data * sysd) {

    pfm_pmu_info_t pinfo;
    pfm_pmu_encode_arg_t e;
    struct perf_event_attr attr;
    const char *arg[PERF_MAX_EVENTS];
    const char **p;
    char *fqstr;
    pfm_event_info_t info;
    int i, j, ret, core;
    int total_supported_events = 0;
    int total_available_events = 0;
    int leader_counter;


    ret = pfm_initialize();
    if (ret != PFM_SUCCESS)
        errx(1, "cannot initialize library: %s\n", pfm_strerror(ret));

    memset(&pinfo, 0, sizeof (pinfo));
    memset(&info, 0, sizeof (info));

    DEBUGMSG(stderr, "Supported PMU models:\n");
    for (i = 0; i < PFM_PMU_MAX; i++) {
        ret = pfm_get_pmu_info(i, &pinfo);
        if (ret != PFM_SUCCESS)
            continue;
        DEBUGMSG(stderr, "\t[%d, %s, \"%s\"]\n", i, pinfo.name, pinfo.desc);
    }

    DEBUGMSG(stderr, "Detected PMU models:\n");
    for (i = 0; i < PFM_PMU_MAX; i++) {
        ret = pfm_get_pmu_info(i, &pinfo);
        if (ret != PFM_SUCCESS)
            continue;
        if (pinfo.is_present) {
            DEBUGMSG(stderr, "\t[%d, %s, \"%s\"]\n", i, pinfo.name, pinfo.desc);
            total_supported_events += pinfo.nevents;
        }
        total_available_events += pinfo.nevents;
    }

    DEBUGMSG(stderr, "Total events: %d available, %d supported\n", total_available_events, total_supported_events);


    if (num_events == 0) {
        return 0;
    } else {
        p = events;
    }

    if (!*p)
        errx(1, "you must pass at least one event");
    i = 0;
    memset(&e, 0, sizeof (e));
#ifdef DEBUG
    before = read_tsc();
#endif

    sysd->core_pmu_events = malloc(sysd->NCORE * sizeof (core_pmu_events_t));

    while (num_events) {
        fqstr = NULL;
        e.fstr = &fqstr;
        DEBUGMSG(stderr, "encoding event:\t%s\n", *p);
        ret = pfm_get_os_event_encoding(*p, PFM_PLM0 | PFM_PLM3, PFM_OS_NONE, &e);
        if (ret != PFM_SUCCESS) {
            if (ret == PFM_ERR_TOOSMALL) {
                free(e.codes);
                e.codes = NULL;
                e.count = 0;
                free(fqstr);
                continue;
            }
            if (ret == PFM_ERR_NOTFOUND && strstr(*p, "::"))
                errx(1, "%s: try setting LIBPFM_ENCODE_INACTIVE=1", pfm_strerror(ret));
            errx(1, "cannot encode event %s: %s", *p, pfm_strerror(ret));
        }
        ret = pfm_get_event_info(e.idx, PFM_OS_NONE, &info);
        if (ret != PFM_SUCCESS)
            errx(1, "cannot get event info: %s", pfm_strerror(ret));

        ret = pfm_get_pmu_info(info.pmu, &pinfo);
        if (ret != PFM_SUCCESS)
            errx(1, "cannot get PMU info: %s", pfm_strerror(ret));

        printf("Requested Event: %s\n", *p);
        printf("Actual    Event: %s\n", fqstr);
        printf("PMU            : %s\n", pinfo.desc);
        printf("IDX            : %d\n", e.idx);
        printf("Codes          :");
        for (j = 0; j < e.count; j++)
            printf(" 0x%"PRIx64, e.codes[j]);
        putchar('\n');


        memset(&attr, 0, sizeof (struct perf_event_attr));
        attr.type = PERF_TYPE_RAW;
        attr.config = e.codes[0];
        if (e.count == 2)
            attr.config1 = e.codes[1];
        if (e.count == 3)
            attr.config2 = e.codes[2];

        attr.size = PERF_ATTR_SIZE_VER4;
        attr.read_format = PERF_FORMAT_GROUP |
                PERF_FORMAT_TOTAL_TIME_ENABLED |
                PERF_FORMAT_TOTAL_TIME_RUNNING;
        attr.disabled = 0;
        attr.exclude_kernel = 0;
        //attr.pinned = 1;

        printf(" Start PMU programming for event %s, index: %d\n", *p, i);
        for (core = 0; core < sysd->NCORE; core++) {
            set_cpu_affinity(core);
            if (i == 0) {
                leader_counter = -1;
            } else {
                leader_counter = fd[core][0];
            }
            fd[core][i] = _perf_event_open(&attr, -1, core, leader_counter, 0);
            if (fd[core][i] < 0) {
                errx(1, "Failed adding event %d %d\n", i, fd[core][i]);
                return -1;
            }

            sysd->core_pmu_events[core].event_code[i] = attr.config;
            DEBUGMSG(stderr, "[DEBUG]: core[%d].PMU[%d].event[0x%"PRIx64"]\n", core, i, sysd->core_pmu_events[core].event_code[i]);

            if (i == 0) {
                if (ioctl(fd[core][0], PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP)) {
                    perror("ioctl(PERF_EVENT_IOC_ENABLE");
                }
            }
        }
        i++;
        num_events--;
        free(fqstr);
        p++;
    }
    ret = perf_assign_pmu_idx(sysd);
    if (ret < 0) {
        errx(1, "Failed adding raw events\n");
        return -1;
    }


#ifdef DEBUG
    after = read_tsc();
    fprintf(stderr, "[DEBUG]: perf_program_raw_events():while_loop overhead CPU cycles: %d \n", abs(before - after));
#endif

    if (e.codes)
        free(e.codes);
    return 0;
}

int perf_disable_per_core(int **fd, struct sys_data * sysd) {

    int core, i;

    for (core = 0; core < sysd->NCORE; core++) {
        set_cpu_affinity(core);
        if (sysd->use_perf) {
            // event 0 is leader
            DEBUGMSG(stderr, "Disabling perf...\n");
            for (i = 0; i < sysd->perf_num_events; i++) {
                //DEBUGMSG(stderr,"Disabling fd: %d\n",fd[core][i]);
                if (fd[core][i]) {
                    if (ioctl(fd[core][i], PERF_EVENT_IOC_DISABLE, 0)) {
                        perror("ioctl(PERF_EVENT_IOC_DISABLE");
                    }
                }
            }
        }
    }

    DEBUGMSG(stderr, "Closing perf descriptors...\n");
    for (core = 0; core < sysd->NCORE; core++) {
        for (i = 0; i < sysd->perf_num_events; i++) {
            close(sysd->fdd[core][i]);
        }
    }

    return 0;

}

int perf_assign_pmu_idx(struct sys_data * sysd) {

    int core, i, j;
    uint64_t result;
    int fd;
    //DEBUGMSG(stderr,"Verifing core PMU events...\n");
    for (core = 0; core < sysd->NCORE; core++) {
        set_cpu_affinity(core);
        fd = open_msr(core);
        //DEBUGMSG(stderr,"Verifing core PMU events...\n");
        memset(sysd->core_pmu_events[core].event_pmu_idx, -1, sizeof (sysd->core_pmu_events[core].event_pmu_idx));
        for (i = 0; i < sysd->PMC_NUM; i++) {
            //DEBUGMSG(stderr,"reading config reg: %d\n",i);
            result = read_msr(fd, IA32_PERFEVTSEL0_ADDR + i);
            for (j = 0; j < sysd->perf_num_events; j++) {
                if (result == sysd->core_pmu_events[core].event_code[j]) {
                    sysd->core_pmu_events[core].event_pmu_idx[j] = i;
                }
            }
        }
    }

    for (core = 0; core < sysd->NCORE; core++) {
        for (i = 0; i < sysd->num_core_events; i++) {
            DEBUGMSG(stderr, "[DEBUG]: core[%d].PMU[%d].event_code[0x%"PRIx64"].idx[%d]\n", core, i, sysd->core_pmu_events[core].event_code[i], sysd->core_pmu_events[core].event_pmu_idx[i]);
            if (sysd->core_pmu_events[core].event_pmu_idx[i] < 0) {
                printf("WARNING!: Event 0x%"PRIx64" not assigned to any PMU counter\n", sysd->core_pmu_events[core].event_code[i]);
                return -1;
            }
        }
    }

    return 0;

}

inline uint64_t perf_scale(perf_read_format *event) {
    uint64_t res = 0;

    if (!event->time_running && !event->time_enabled && event->value)
        warnx("WARNING: time_running = 0 = time_enabled, raw count not zero\n");

    if (event->time_running > event->time_enabled)
        warnx("WARNING: time_running > time_enabled\n");

    if (event->time_running)
        res = (uint64_t) ((double) event->value * event->time_enabled / event->time_running);
    return res;
}

inline double perf_scale_ratio(perf_read_format *event) {
    if (!event->time_enabled)
        return 0.0;

    return event->time_running * 1.0 / event->time_enabled;
}


