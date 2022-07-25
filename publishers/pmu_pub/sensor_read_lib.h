/*
 * sensor_read_lib.h : MSR handling routines
 * 
 * (c) 2017 ETH Zurich, [Integrated System Laboratory, D-ITET] 
 * (c) 2017 University of Bologna, [Department of Electrical, Electronic and Information Engineering, DEI]
 *
 * Contributed by:
 * Francesco Beneventi <francesco.beneventi@unibo.it>
 * Andrea Bartolini	<barandre@iis.ee.ethz.ch>
 * 
 * Date:
 * 19/09/2014
 * 
 */

#ifndef SENSOR_READ_LIB_H
#define	SENSOR_READ_LIB_H

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <inttypes.h>
#include <unistd.h>
#include <math.h>

#include "perf_event_lib.h"

#ifndef USE_RDMSR
    #define USE_RDPMC
#endif
#define MAX_CORES	1024
#define MAX_PACKAGES	16


/***** Intel core REGISTER ADDRESSES ********/
/*
 * Platform specific RAPL Domains.
 * Note that PP1 RAPL Domain is supported on 062A only
 * And DRAM RAPL Domain is supported on 062D only
 */
#define MSR_RAPL_POWER_UNIT		0x606
/* Package RAPL Domain */
#define MSR_PKG_RAPL_POWER_LIMIT	0x610
#define MSR_PKG_ENERGY_STATUS		0x611
#define MSR_PKG_PERF_STATUS		0x613
#define MSR_PKG_POWER_INFO		0x614

/* PP0 RAPL Domain */
#define MSR_PP0_POWER_LIMIT		0x638
#define MSR_PP0_ENERGY_STATUS		0x639
#define MSR_PP0_POLICY			0x63A
#define MSR_PP0_PERF_STATUS		0x63B

/* PP1 RAPL Domain, may reflect to uncore devices */
#define MSR_PP1_POWER_LIMIT		0x640
#define MSR_PP1_ENERGY_STATUS		0x641
#define MSR_PP1_POLICY			0x642

/* DRAM RAPL Domain */
#define MSR_DRAM_POWER_LIMIT		0x618
#define MSR_DRAM_ENERGY_STATUS		0x619
#define MSR_DRAM_PERF_STATUS		0x61B
#define MSR_DRAM_POWER_INFO		0x61C

/* Per-Core C States */
#define MSR_CORE_C3_RESIDENCY           0x3FC
#define MSR_CORE_C6_RESIDENCY           0x3FD
#define MSR_CORE_C7_RESIDENCY           0x3FE

/* Per-Socket C States */
#define MSR_PKG_C2_RESIDENCY            0x60D
#define MSR_PKG_C3_RESIDENCY            0x3F8
#define MSR_PKG_C6_RESIDENCY            0x3F9
#define MSR_PKG_C7_RESIDENCY            0x3FA

/* RAPL UNIT BITMASK */
#define POWER_UNIT_OFFSET               0
#define POWER_UNIT_MASK                 0x0F

#define ENERGY_UNIT_OFFSET              0x08
#define ENERGY_UNIT_MASK                0x1F00

#define TIME_UNIT_OFFSET                0x10
#define TIME_UNIT_MASK                  0xF000

// Address of the performance counters and temperature
#define IA32_TEMPERATURE_TARGET         0x000001a2
#define MSR_IA32_THERM_STATUS           0x0000019c
#define MSR_IA32_PACKAGE_THERM_STATUS   0x000001b1
/* Temperature mask - select the temperature field in the MSR_IA32_THERM_STATUS */
#define TEMP_MASK                       0x007F0000
/* Intel Core-based CPU performance counters */
#define MSR_CORE_PERF_FIXED_CTR0        0x00000309
#define MSR_CORE_PERF_FIXED_CTR1        0x0000030a
#define MSR_CORE_PERF_FIXED_CTR2        0x0000030b
#define MSR_CORE_PERF_FIXED_CTR_CTRL    0x0000038d
#define MSR_CORE_PERF_GLOBAL_STATUS     0x0000038e
#define MSR_CORE_PERF_GLOBAL_CTRL       0x0000038f
#define MSR_CORE_PERF_GLOBAL_OVF_CTRL   0x00000390

/* core PMU */
#define IA32_PERFEVTSEL0_ADDR           (0x186)
#define IA32_PERFEVTSEL1_ADDR           (IA32_PERFEVTSEL0_ADDR + 1)
#define IA32_PERFEVTSEL2_ADDR           (IA32_PERFEVTSEL0_ADDR + 2)
#define IA32_PERFEVTSEL3_ADDR           (IA32_PERFEVTSEL0_ADDR + 3)
#define IA32_PERFEVTSEL4_ADDR           (IA32_PERFEVTSEL0_ADDR + 4)
#define IA32_PERFEVTSEL5_ADDR           (IA32_PERFEVTSEL0_ADDR + 5)
#define IA32_PERFEVTSEL6_ADDR           (IA32_PERFEVTSEL0_ADDR + 6)
#define IA32_PERFEVTSEL7_ADDR           (IA32_PERFEVTSEL0_ADDR + 7)
#define IA32_PMC0                       (0xC1)
#define IA32_PMC1                       (IA32_PMC0 + 1)
#define IA32_PMC2                       (IA32_PMC0 + 2)
#define IA32_PMC3                       (IA32_PMC0 + 3)
#define IA32_PMC4                       (IA32_PMC0 + 4)
#define IA32_PMC5                       (IA32_PMC0 + 5)
#define IA32_PMC6                       (IA32_PMC0 + 6)
#define IA32_PMC7                       (IA32_PMC0 + 7)


/* UBox performance monitoring MSR */
#define U_MSR_PMON_UCLK_FIXED_CTL       0x0703      //counter 64bit
#define U_MSR_PMON_UCLK_FIXED_CTR       0x0704      //control 48bit

/* scaled CPU utilisation statistics */
#define MSR_MPERF                       0xe7
#define MSR_APERF                       0xe8

#define PLATFORM_INFO_ADDR              0xce

#define RDPMC_INSTR                     (1 << 30)
#define RDPMC_CLKCURR                   ((1 << 30) + 1)
#define RDPMC_CLKREF                    ((1 << 30) + 2)

#define DECLARE_ARGS(val, low, high)  unsigned low, high
#define EAX_EDX_VAL(val, low, high) ((low) | ((uint64_t)(high) << 32))
#define EAX_EDX_RET(val, low, high) "=a" (low), "=d" (high)


/* Intel CPU Codenames */
#define SANDYBRIDGE	42
#define SANDYBRIDGE_EP  45
#define IVYBRIDGE	58
#define IVYBRIDGE_EP	62
#define HASWELL		60	
#define HASWELL_EP	63
#define BROADWELL	61	
#define BROADWELL_EP	79
#define BROADWELL_DE	86
#define SKYLAKE		78
#define SKYLAKE_HS	94
#define SKYLAKE_X   85



/* data structures */
typedef struct {
    uint64_t tsc;
    unsigned int temp ;
    uint64_t instr ;
    uint64_t clk_curr;
    uint64_t clk_ref ;
    uint64_t C3;
    uint64_t C6;
    uint64_t aperf ;
    uint64_t mperf ;
    uint64_t pmc[MAX_PMC];
    perf_read_format *perf_event;
}per_core_data;

typedef struct {
    uint64_t tsc;
    unsigned int ergU ;
    unsigned int powPP0 ;
    unsigned int powPP1 ;
    unsigned int powPkg ;
    unsigned int tempPkg ;
    unsigned int powDramC ;
    uint64_t uclk ;
    uint64_t C2 ;
    uint64_t C3 ;
    uint64_t C6 ;
}per_cpu_data;

struct sys_data {
    int NCPU;
    int NCORE;
    int CPU_MODEL;
    int HT_EN;
    float nom_freq;
    int DRAM_SUPP;
    int PP1_SUPP;
    int dieTemp[MAX_PACKAGES];
    int dieTempEn[MAX_PACKAGES];
    per_cpu_data *cpu_data;
    per_core_data *core_data;
    char logfile[256];
    char tmpstr[80];
    char* hostid;
    char* topic;
    char* cmd_topic;
    char* brokerHost;
    int brokerPort;
    int qos;
    float dT;
    int extra_counters;
    int use_perf;
    int perf_num_events;
    int PMC_NUM;
    char **my_events;
    int *is_uncore_event;
    int **fdd;
    core_pmu_events_t *core_pmu_events;
    int num_core_events;
};



int open_msr(int core);
long long read_msr(int fd, int which);
void write_msr(int fd, int which, uint64_t data);
unsigned long long read_tsc(void);
void read_msr_data(struct sys_data * sysd);
inline int set_cpu_affinity(unsigned int cpu);
int detect_topology(struct sys_data * sysd);
int detect_cpu_model(struct sys_data * sysd);
int detect_nominal_frequency(struct sys_data * sysd);
int reset_PMU(struct sys_data * sysd);
int clean_PMU(struct sys_data * sysd);
int program_msr(struct sys_data * sysd);


#endif /* SENSOR_READ_LIB_H */
