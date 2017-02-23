/*
 * sensor_read_lib.c : MSR handling routines
 *
 * (c) 2017 ETH Zurich, [Integrated System Laboratory, D-ITET] 
 * (c) 2017 University of Bologna, [Department of Electrical, Electronic and Information Engineering, DEI]
 *
 * Contributed by:
 * Francesco Beneventi <francesco.beneventi@unibo.it>
 * Andrea Bartolini	<barandre@iis.ee.ethz.ch>
 * 
 * 
 * Date:
 * 19/09/2014
 * 
 */

#ifndef _GNU_SOURCE
    #define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <inttypes.h>
#include <unistd.h>
#include <sched.h>
#include "sensor_read_lib.h"

#include "pmu_pub.h"


int open_msr(int core) {

  char msr_filename[BUFSIZ];
  int fd;

  sprintf(msr_filename, "/dev/cpu/%d/msr", core);
  fd = open(msr_filename, O_RDWR);
  if ( fd < 0 ) {
    if ( errno == ENXIO ) {
      fprintf(stderr, "rdmsr: No CPU %d\n", core);
      exit(2);
    } else if ( errno == EIO ) {
      fprintf(stderr, "rdmsr: CPU %d doesn't support MSRs\n", core);
      exit(3);
    } else {
      perror("rdmsr:open");
      fprintf(stderr,"Trying to open %s\n",msr_filename);
      exit(127);
    }
  }

  return fd;
}

long long read_msr(int fd, int which) {
  uint64_t data;
  if ( pread(fd, &data, sizeof data, which) != sizeof data ) {
    perror("rdmsr:pread");
    exit(127);
  }
  return (long long)data;
}

void write_msr(int fd, int which, uint64_t data) {
  if ( pwrite(fd, &data, sizeof data, which) != sizeof data ) {
    perror("wrmsr:pwrite");
    exit(127);
  }
}

inline unsigned long long read_tsc(void)
{
  DECLARE_ARGS(val, low, high);

  asm volatile("rdtsc" : EAX_EDX_RET(val, low, high));

  return EAX_EDX_VAL(val, low, high);
}


unsigned long rdpmc(unsigned c)
{
   unsigned a, d;
 
   __asm__ volatile("rdpmc" : "=a" (a), "=d" (d) : "c" (c));
 
   return ((unsigned long)a) | (((unsigned long)d) << 32);;
}

inline void read_msr_data(struct sys_data * sysd){

    int cpuid = 0;
    int core = 0;
    int i;
    int fd;
    uint64_t tsc;
    uint64_t result;
    unsigned int mask;
    
#ifdef DEBUG
    uint64_t before,after;
    int count = 0;
#endif
  
    for (core=0;core<sysd->NCORE;core++){
        set_cpu_affinity(core);
        fd=open_msr(core);
        tsc = read_tsc();
        if ((core==0)|(core==sysd->NCORE/2)){
            cpuid = trunc(core*sysd->NCPU)/sysd->NCORE;
            sysd->cpu_data[cpuid].tsc = tsc;
            if (sysd->dieTempEn[cpuid] == 0){
                result           = read_msr(fd,IA32_TEMPERATURE_TARGET);
                sysd->dieTemp[cpuid]   = (result >> 16) & 0x0ff;
                sysd->dieTempEn[cpuid] = 1;
            }
            sysd->cpu_data[cpuid].ergU      = read_msr(fd,MSR_RAPL_POWER_UNIT);
            sysd->cpu_data[cpuid].powPP0    = read_msr(fd,MSR_PP0_ENERGY_STATUS);
            sysd->cpu_data[cpuid].powPkg    = read_msr(fd,MSR_PKG_ENERGY_STATUS);
            result                          = read_msr(fd,MSR_IA32_PACKAGE_THERM_STATUS);
            sysd->cpu_data[cpuid].tempPkg   = sysd->dieTemp[cpuid] - ((result & TEMP_MASK ) >> 16);
                       
            if (sysd->DRAM_SUPP==1){
                sysd->cpu_data[cpuid].powDramC = read_msr(fd,MSR_DRAM_ENERGY_STATUS);
            }
            
            if (sysd->PP1_SUPP==1){
                sysd->cpu_data[cpuid].powPP1=read_msr(fd,MSR_PP1_ENERGY_STATUS);
            }
            
            DEBUGMSG(stderr, "[DEBUG]: read_msr_data() CPU: \n");
            // extra PKG counters
            if (sysd->extra_counters == 1){
                sysd->cpu_data[cpuid].C2        = read_msr(fd,MSR_PKG_C2_RESIDENCY);
                sysd->cpu_data[cpuid].C3        = read_msr(fd,MSR_PKG_C3_RESIDENCY);
                sysd->cpu_data[cpuid].C6        = read_msr(fd,MSR_PKG_C6_RESIDENCY);
                if(sysd->CPU_MODEL==HASWELL_EP){
                    sysd->cpu_data[cpuid].uclk = read_msr(fd,U_MSR_PMON_UCLK_FIXED_CTR);
                    DEBUGMSG(stderr, "[DEBUG]: sysd->cpu_data[%d].uclk        : %lu\n", cpuid,  sysd->cpu_data[cpuid].uclk);
                }

                DEBUGMSG(stderr, "[DEBUG]: sysd->cpu_data[%d].C2          : %lu\n", cpuid,  sysd->cpu_data[cpuid].C2);
                DEBUGMSG(stderr, "[DEBUG]: sysd->cpu_data[%d].C3          : %lu\n", cpuid,  sysd->cpu_data[cpuid].C3);
                DEBUGMSG(stderr, "[DEBUG]: sysd->cpu_data[%d].C6          : %lu\n", cpuid,  sysd->cpu_data[cpuid].C6);
                
                
                // read uncore events 
                // if (sysd->use_perf){
                if (1) {    
                #ifdef DEBUG
                before = read_tsc();
                #endif 
                    for (i=0;i<sysd->perf_num_events;i++){
                        if (sysd->is_uncore_event[i]){
                            read(sysd->fdd[core][i], &sysd->core_data[core].perf_event[i], sizeof(perf_read_format));
                            sysd->core_data[core].perf_event[i].value = perf_scale(&sysd->core_data[core].perf_event[i]);  //scaled value
                            //DEBUGMSG(stderr, "[DEBUG]: PERF: core[%d].event[%d=%s].ratio[%.2f]\t\t\t:%lu\n", core,i,sysd->my_events[i],sysd->core_data[core].perf_event[i].value,perf_scale_ratio(&sysd->core_data[core].perf_event[i]));
                        }
                    }
                #ifdef DEBUG
                after = read_tsc();
                count =0;
                    for (i=0;i<sysd->perf_num_events;i++){
                        if (sysd->is_uncore_event[i]){
                            count++;
                            DEBUGMSG(stderr, "[DEBUG]: PERF: core[%d].event[%d=%s].ratio[%.2f]\t\t\t:%lu\n", core,i,sysd->my_events[i],sysd->core_data[core].perf_event[i].value,perf_scale_ratio(&sysd->core_data[core].perf_event[i]));
                        }
                    }
                    fprintf(stderr, "[DEBUG]: read_msr_data() - %d read per-CPU Perf fd - CPU cycles: %lu \n", count, abs(before-after));
                #endif 
                }
                
                
                
            }
            
            
            DEBUGMSG(stderr, "[DEBUG]: sysd->cpu_data[%d].ergU        : %lu\n", cpuid,  sysd->cpu_data[cpuid].ergU     );
            DEBUGMSG(stderr, "[DEBUG]: sysd->cpu_data[%d].powPP0      : %lu\n", cpuid,  sysd->cpu_data[cpuid].powPP0 );
            DEBUGMSG(stderr, "[DEBUG]: sysd->cpu_data[%d].powPkg      : %lu\n", cpuid,  sysd->cpu_data[cpuid].powPkg );
            DEBUGMSG(stderr, "[DEBUG]: sysd->cpu_data[%d].tempPkg     : %lu\n", cpuid,  sysd->cpu_data[cpuid].tempPkg  );
            DEBUGMSG(stderr, "[DEBUG]: sysd->cpu_data[%d].uclk        : %lu\n", cpuid,  sysd->cpu_data[cpuid].uclk);
            DEBUGMSG(stderr, "[DEBUG]: sysd->cpu_data[%d].powDramC    : %lu\n", cpuid,  sysd->cpu_data[cpuid].powDramC );
            DEBUGMSG(stderr, "[DEBUG]: sysd->cpu_data[%d].powPP1      : %lu\n", cpuid,  sysd->cpu_data[cpuid].powPP1 );

            
        }
        // Set the fixed counter to count all event in both user and kernel space
        result = read_msr(fd,MSR_CORE_PERF_FIXED_CTR_CTRL);
        mask   = 0x0333;
        result = result | mask;
        write_msr(fd,MSR_CORE_PERF_FIXED_CTR_CTRL,result);
        sysd->core_data[core].tsc       = tsc;
        result                          = read_msr(fd,MSR_IA32_THERM_STATUS);
        sysd->core_data[core].temp      = sysd->dieTemp[cpuid] - ((result & TEMP_MASK ) >> 16);

        
#ifdef USE_RDPMC
    #ifdef DEBUG
        before = read_tsc();
    #endif       
        sysd->core_data[core].instr = rdpmc(RDPMC_INSTR);
        sysd->core_data[core].clk_curr = rdpmc(RDPMC_CLKCURR);
        sysd->core_data[core].clk_ref = rdpmc(RDPMC_CLKREF);
    #ifdef DEBUG
        after = read_tsc();
        fprintf(stderr, "[DEBUG]: read_msr_data() - 3 FIXED counters RDPMC - CPU cycles: %lu \n", abs(before-after));
    #endif         
        
        
#else
        
    #ifdef DEBUG
        before = read_tsc();
    #endif        
        
        sysd->core_data[core].instr     = read_msr(fd,MSR_CORE_PERF_FIXED_CTR0);
        sysd->core_data[core].clk_curr  = read_msr(fd,MSR_CORE_PERF_FIXED_CTR1);
        sysd->core_data[core].clk_ref   = read_msr(fd,MSR_CORE_PERF_FIXED_CTR2);
    #ifdef DEBUG
        after = read_tsc();
        fprintf(stderr, "[DEBUG]: read_msr_data() - 3 FIXED counters - CPU cycles: %lu \n", abs(before-after));
    #endif 
        
        
#endif
       
        //result = 0L;
        //write_msr(fd,MSR_CORE_PERF_FIXED_CTR1,result);
        
        // extra counters
        if (sysd->extra_counters == 1){
            sysd->core_data[core].C3        = read_msr(fd,MSR_CORE_C3_RESIDENCY);
            sysd->core_data[core].C6        = read_msr(fd,MSR_CORE_C6_RESIDENCY);
            sysd->core_data[core].aperf     = read_msr(fd,MSR_APERF);
            sysd->core_data[core].mperf     = read_msr(fd,MSR_MPERF);
            DEBUGMSG(stderr, "[DEBUG]: sysd->core_data[%d].C3         : %lu\n", core,sysd->core_data[core].C3);
            DEBUGMSG(stderr, "[DEBUG]: sysd->core_data[%d].C6         : %lu\n", core,sysd->core_data[core].C6);
            DEBUGMSG(stderr, "[DEBUG]: sysd->core_data[%d].aperf      : %lu\n", core,sysd->core_data[core].aperf);
            DEBUGMSG(stderr, "[DEBUG]: sysd->core_data[%d].mperf      : %lu\n", core,sysd->core_data[core].mperf); 
            

            if (!sysd->use_perf){                      
#ifdef USE_RDPMC           
                #ifdef DEBUG
                    for (i=0;i<sysd->PMC_NUM;i++){
                        result = read_msr(fd,IA32_PERFEVTSEL0_ADDR+i);
                        DEBUGMSG(stderr, "[DEBUG]: IA32_PERFEVTSEL0_ADDR[%d]        : %#" PRIx64 "\n", i, result);
                    }

                    before = read_tsc();
                #endif
                for (i=0;i<sysd->num_core_events;i++){
                    sysd->core_data[core].pmc[i] = rdpmc(i); 
                }
                #ifdef DEBUG
                    after = read_tsc();
                    fprintf(stderr, "[DEBUG]: read_msr_data() - %d GPC counters RDPMC - CPU cycles: %lu \n", sysd->num_core_events, abs(before-after));
                    for (i=0;i<sysd->num_core_events;i++){
                        DEBUGMSG(stderr, "[DEBUG]: sysd->core_data[%d].pmc[%d]        : %lu\n", core, i, sysd->core_data[core].pmc[i]); 
                    }

                #endif              
#else
                #ifdef DEBUG     
                    for (i=0;i<sysd->PMC_NUM;i++){
                        result = read_msr(fd,IA32_PERFEVTSEL0_ADDR+i);
                        DEBUGMSG(stderr, "[DEBUG]: IA32_PERFEVTSEL0_ADDR[%d]        : %#" PRIx64 "\n", i, result);
                    }
                    before = read_tsc();
                #endif
                for (i=0;i<sysd->num_core_events;i++){
                    sysd->core_data[core].pmc[i] = read_msr(fd,IA32_PMC0+i);
                }
                #ifdef DEBUG
                    after = read_tsc();
                    fprintf(stderr, "[DEBUG]: read_msr_data() - %d GPC counters - CPU cycles: %lu \n", sysd->num_core_events, abs(before-after));
                    for (i=0;i<sysd->num_core_events;i++){
                        DEBUGMSG(stderr, "[DEBUG]: sysd->core_data[%d].pmc[%d]        : %lu\n", core, i, sysd->core_data[core].pmc[i]); 
                    }
                #endif 
#endif
            }else{// use perf driver to read PMC counters
                #ifdef DEBUG
                for (i=0;i<sysd->PMC_NUM;i++){
                    result = read_msr(fd,IA32_PERFEVTSEL0_ADDR+i);
                    DEBUGMSG(stderr, "[DEBUG]: PERF: IA32_PERFEVTSEL0_ADDR[%d]        : %#" PRIx64 "\n", i, result);
                }
                before = read_tsc();
                #endif

                for(i=0;i<sysd->perf_num_events;i++){                   
                    if (sysd->is_uncore_event[i] != 1){
                        read(sysd->fdd[core][i], &sysd->core_data[core].perf_event[i], sizeof(perf_read_format));
                        sysd->core_data[core].perf_event[i].value = perf_scale(&sysd->core_data[core].perf_event[i]);  //scaled value
                        //DEBUGMSG(stderr, "[DEBUG]: PERF: core[%d].event[%d=%s].ratio[%.2f]\t\t\t:%lu\n", core,i,sysd->my_events[i],sysd->core_data[core].perf_event[i].value,perf_scale_ratio(&sysd->core_data[core].perf_event[i]));
                    }  
                }
                #ifdef DEBUG
                after = read_tsc();
                count = 0;
                for(i=0;i<sysd->perf_num_events;i++){
                    if (sysd->is_uncore_event[i] != 1){
                        count++;
                        DEBUGMSG(stderr, "[DEBUG]: PERF: core[%d].event[%d=%s].ratio[%.2f]\t\t\t:%lu\n", core,i,sysd->my_events[i],sysd->core_data[core].perf_event[i].value,perf_scale_ratio(&sysd->core_data[core].perf_event[i]));
                    }
                }
                fprintf(stderr, "[DEBUG]: read_msr_data() - %d read per-core Perf fd - CPU cycles: %lu \n", count, abs(before-after));
                    
                #endif
            }     
        }
        
        DEBUGMSG(stderr, "[DEBUG]: read_msr_data() Cores: \n");
        DEBUGMSG(stderr, "[DEBUG]: sysd->core_data[%d].tsc        : %lu\n", core,sysd->core_data[core].tsc);
        DEBUGMSG(stderr, "[DEBUG]: sysd->core_data[%d].temp       : %u\n",  core,sysd->core_data[core].temp);
        DEBUGMSG(stderr, "[DEBUG]: sysd->core_data[%d].instr      : %lu\n", core,sysd->core_data[core].instr);
        DEBUGMSG(stderr, "[DEBUG]: sysd->core_data[%d].clk_curr   : %lu\n", core,sysd->core_data[core].clk_curr);
        DEBUGMSG(stderr, "[DEBUG]: sysd->core_data[%d].clk_ref    : %lu\n", core,sysd->core_data[core].clk_ref);

        close(fd);
    }

}

inline int set_cpu_affinity(unsigned int cpu) {

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);

    if (sched_setaffinity(getpid(), sizeof (cpu_set_t), &cpuset) < 0) {
        perror("sched_setaffinity");
        fprintf(stderr, "warning: unable to set cpu affinity\n");
        return -1;
    }
    return 0;
}


int detect_topology(struct sys_data * sysd) {

    FILE *fd;
    int total_logical_cpus = 0;
    int total_sockets = 0;
    int cores_per_socket = 0;
    int logical_cpus[MAX_CORES];
    int sockets[MAX_PACKAGES];
    int cores[MAX_CORES];
    int hyperthreading = 0;
    int cpu = 0;
    int phys_id = 0;
    int core = 0;
    int i;
    char buffer[BUFSIZ];
    char *result;

    printf("\nDetecting host topology...\n\n");

    fd = fopen("/proc/cpuinfo", "r");
    if (fd == NULL) {
        printf("Cannot parse file: /proc/cpuinfo\n");
        return -1;
    }

    for (i = 0; i < MAX_CORES; i++) logical_cpus[i] = -1;
    for (i = 0; i < MAX_PACKAGES; i++) sockets[i] = -1;
    for (i = 0; i < MAX_CORES; i++) cores[i] = -1;

    while (1) {
        result = fgets(buffer, BUFSIZ, fd);
        if (result == NULL) break;

        if (!strncmp(result, "processor", 9)) {
            sscanf(result, "%*s%*s%d", &cpu);

            if (logical_cpus[cpu] == -1) {
                logical_cpus[cpu] = 1;
                total_logical_cpus += 1;
            }
        }
        if (!strncmp(result, "physical id", 11)) {
            sscanf(result, "%*s%*s%*s%d", &phys_id);

            if (sockets[phys_id] == -1) {
                sockets[phys_id] = 1;
                total_sockets += 1;
            }
        }
        if (!strncmp(result, "core id", 7)) {
            sscanf(result, "%*s%*s%*s%d", &core);

            if (cores[core] == -1) {
                cores[core] = 1;
                cores_per_socket += 1;
            }
        }
    }

    fclose(fd);

    if ((cores_per_socket * total_sockets) * 2 == total_logical_cpus)
        hyperthreading = 1;

    if (hyperthreading) {
        printf("Hyperthreading enabled\n");
        sysd->HT_EN = 1;
        sysd->PMC_NUM = (int) (MAX_PMC / 2);
    } else {
        printf("Hyperthreading disabled\n");
        sysd->HT_EN = 0;
        sysd->PMC_NUM = MAX_PMC;
    }

    printf("%d physical sockets\n", total_sockets);
    printf("%d cores per socket\n", cores_per_socket);
    printf("%d total cores\n", (cores_per_socket * total_sockets));
    printf("%d logical CPUs\n", total_logical_cpus);
    printf("%d programmable counters available\n", sysd->PMC_NUM);

    sysd->NCORE = (cores_per_socket * total_sockets);
    sysd->NCPU = total_sockets;


    return 0;

}
int detect_cpu_model(struct sys_data * sysd) {

    FILE *fd;
    int model = -1;
    char buffer[BUFSIZ];
    char *result;

    printf("\nDetecting CPU model...\n\n");

    fd = fopen("/proc/cpuinfo", "r");
    if (fd == NULL) {
        printf("Cannot parse file: /proc/cpuinfo\n");
        exit(1);
    }

    while (1) {
        result = fgets(buffer, BUFSIZ, fd);
        if (result == NULL) break;
        if (!strncmp(result, "model", 5)) {
            sscanf(result, "%*s%*s%d", &model);
        }
    }

    fclose(fd);

    printf("CPU type: ");
    switch (model) {
        case SANDYBRIDGE:
            printf("Sandybridge");
            break;
        case SANDYBRIDGE_EP:
            printf("Sandybridge-EP");
            break;
        case IVYBRIDGE:
            printf("Ivybridge");
            break;
        case IVYBRIDGE_EP:
            printf("Ivybridge-EP");
            break;
        case HASWELL:
            printf("Haswell");
            break;
        case HASWELL_EP:
            printf("Haswell-EP");
            break;
        case BROADWELL:
            printf("Broadwell");
            break;
        case SKYLAKE:
        case SKYLAKE_HS:
            printf("Skylake");
            break;
        default:
            printf("Unknown model %d\n", model);
            model = -1;
            break;
    }

    printf("\n");

    if (model > 0) {
        sysd->CPU_MODEL = model;

        // DRAM power reading support    
        if ((model == SANDYBRIDGE_EP) ||
                (model == IVYBRIDGE_EP) ||
                (model == HASWELL_EP) ||
                (model == HASWELL) ||
                (model == BROADWELL)) {

            sysd->DRAM_SUPP = 1;
        }

        // Core power reading support
        if ((model == SANDYBRIDGE) ||
                (model == IVYBRIDGE) ||
                (model == HASWELL) ||
                (model == BROADWELL)) {

            sysd->PP1_SUPP = 1;
        }

    }

    return model;
}


int detect_nominal_frequency(struct sys_data * sysd) {

    uint64_t result;
    uint64_t bus_freq;
    uint64_t nom_freq = 0;
    int model;
    int fd;

    printf("\nDetecting CPU Nominal Frequency...\n\n");

    if (sysd->CPU_MODEL > 0) {
        model = sysd->CPU_MODEL;
    } else {
        printf("\n Error, CPU model Unknown! \n");
        return -1;
    }


    if ((model == SANDYBRIDGE) ||
            (model == SANDYBRIDGE_EP) ||
            (model == IVYBRIDGE) ||
            (model == IVYBRIDGE_EP) ||
            (model == HASWELL) ||
            (model == HASWELL_EP) ||
            (model == BROADWELL) ||
            (model == BROADWELL_EP) ||
            (model == BROADWELL_DE) ||
            (model == SKYLAKE_HS)) {

        bus_freq = 100000000;

    } else {

        bus_freq = 133333333;

    }

    fd = open_msr(0);
    result = read_msr(fd, PLATFORM_INFO_ADDR);
    close(fd);

    // Maximum Non-Turbo Ratio (R/O) - 15:8 bitfield
    nom_freq = ((result >> 0x8) & 0xff) * bus_freq;

    if (!nom_freq) {
        printf("\nError in detecting CPU Nominal Frequency! \n");
        return -1;
    } else {
        printf("\nCPU Nominal Frequency: %f MHz \n\n", (float) nom_freq / 1000000);
        sysd->nom_freq = nom_freq;
    }

    return 0;

}


int program_msr(struct sys_data * sysd) {

    uint64_t result, mask;
    int core, cpuid;
    int fd;

    for (core = 0; core < sysd->NCORE; core++) {
        set_cpu_affinity(core);
        fd = open_msr(core);
        // Per CPU
        if ((core == 0) | (core == sysd->NCORE / 2)) {
            cpuid = trunc(core * sysd->NCPU) / sysd->NCORE;
            // Enable uncore clock
            if (sysd->CPU_MODEL == HASWELL_EP) {
                result = read_msr(fd, U_MSR_PMON_UCLK_FIXED_CTL);
                mask = 0x400000;
                result |= mask;
                write_msr(fd, U_MSR_PMON_UCLK_FIXED_CTL, result);
            }
        }
    }

    return 0;

}

int reset_PMU(struct sys_data * sysd) {

    int core, fd, i;

    for (core = 0; core < sysd->NCORE; core++) {
        set_cpu_affinity(core);
        DEBUGMSG(stderr, "RESET: open msr driver..\n");
        fd = open_msr(core);

        write_msr(fd, MSR_CORE_PERF_GLOBAL_CTRL, 0x0);
        for (i = 0; i < sysd->PMC_NUM; i++) {
            write_msr(fd, IA32_PERFEVTSEL0_ADDR + i, 0x0);
        }
        write_msr(fd, MSR_CORE_PERF_FIXED_CTR_CTRL, 0x0);
    }

    return 0;

}

int clean_PMU(struct sys_data * sysd) {

    uint64_t result;
    int core, fd, i;

    for (core = 0; core < sysd->NCORE; core++) {
        set_cpu_affinity(core);
        DEBUGMSG(stderr, "CLEAN: open msr driver..\n");
        fd = open_msr(core);
        result = (1L << 32) + (1L << 33) + (1L << 34);
        write_msr(fd, MSR_CORE_PERF_GLOBAL_CTRL, result);

        for (i = 0; i < sysd->PMC_NUM; i++) {
            write_msr(fd, IA32_PERFEVTSEL0_ADDR + i, 0x0);
        }
    }

    return 0;
}
