#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include "cmdline.h"
#include <json-c/json.h>

#include "dlt/dlt.h"
#include "dlt/dlt_user_macros.h"

DLT_DECLARE_CONTEXT(dlt_ctxt_cpu);
DLT_DECLARE_CONTEXT(dlt_ctxt_proc);

#define BUFFER_SIZE 256

typedef struct
{
    // cpu load is computed by delta from one measurement to another
    // proc_jiffies = user_jiffies + system_jiffies
    // cpu_load = proc_jiffies[k] - proc_jiffies[k-1] / (g_uptime_jiffies [k] - g_uptime_jiffies[k-1])
    uint32_t proc_jiffies;
    uint32_t proc_jiffies_prev;

    uint32_t rss;

    double cpu;

    char *cmdline;
    uint32_t pid;

} ProcessCPU;

ProcessCPU processes[10];

uint32_t g_uptime_jiffies = 0U;
uint32_t g_uptime_jiffies_prev = 0U;
uint32_t g_clock_ticks = 0U;
size_t g_page_sz = 0;
struct gengetopt_args_info g_args_info = {0};

/*
- user: normal processes executing in user mode
- nice: niced processes executing in user mode
- system: processes executing in kernel mode
- idle: twiddling thumbs
- iowait: In a word, iowait stands for waiting for I/O to complete. But there
  are several problems:
  1. Cpu will not wait for I/O to complete, iowait is the time that a task is
     waiting for I/O to complete. When cpu goes into idle state for
     outstanding task io, another task will be scheduled on this CPU.
  2. In a multi-core CPU, the task waiting for I/O to complete is not running
     on any CPU, so the iowait of each CPU is difficult to calculate.
  3. The value of iowait field in /proc/stat will decrease in certain
     conditions.
  So, the iowait is not reliable by reading from /proc/stat.
- irq: servicing interrupts
- softirq: servicing softirqs
- steal: involuntary wait
- guest: running a normal guest
- guest_nice: running a niced guest
*/
static double get_cpu_usage(void)
{
    static uint32_t prev_idle = 0U, prev_total = 0U;
    FILE *file;
    double usage = 0.0f;

    file = fopen("/proc/stat", "r");
    if (file)
    {
        uint32_t idle, total;
        uint32_t user, nice, system, idle_time, iowait, irq, softirq, steal;
        char buffer[BUFFER_SIZE];

        if (fgets(buffer, sizeof(buffer), file))
        {
            sscanf(buffer, "cpu %ld %ld %ld %ld %ld %ld %ld %ld",
                   &user, &nice, &system, &idle_time, &iowait, &irq, &softirq, &steal);

            idle = idle_time + iowait;
            total = user + nice + system + idle_time + iowait + irq + softirq + steal;

            uint32_t total_diff = total - prev_total;
            uint32_t idle_diff = idle - prev_idle;
            usage = 100.0 * (1.0 - ((double)idle_diff / total_diff));

            prev_idle = idle;
            prev_total = total;
        }

        fclose(file);
    }

    return usage;
}

static ssize_t get_kbytes_available(void)
{
    char line[BUFFER_SIZE];
    ssize_t mem_available = -1;

    FILE *fp = fopen("/proc/meminfo", "r");
    if (fp)
    {
        while (fgets(line, sizeof(line), fp))
        {
            if (strncmp(line, "MemAvailable", 8) == 0)
            {
                sscanf(line, "MemAvailable: %d kB", &mem_available);
                break;
            }
        }

        fclose(fp);
    }

    // Calculate total available memory
    return mem_available;
}

static double get_uptime(void)
{
    FILE *file;

    file = fopen("/proc/uptime", "r");
    if (file)
    {
        double uptime_seconds = -1.0f;
        (void)fscanf(file, "%lf", &uptime_seconds);

        g_uptime_jiffies = (long)(uptime_seconds * g_clock_ticks);

        fclose(file);
    }
}

#define SSCANF_EXTRACT_UTIME_STIME_RSS "%*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*lu %*s %lu %lu %*s %*s %*s %*s %*s %*s %*s %*s %ld"

static ProcessCPU get_process_cpu_kernel_5_4_3(uint8_t index)
{
    char stat_path[BUFFER_SIZE];
    FILE *file;

    snprintf(stat_path, sizeof(stat_path), "/proc/%ld/stat", processes[index].pid);

    file = fopen(stat_path, "r");
    if (file)
    {
        uint32_t utime, stime;
        uint32_t rss;

        if (fscanf(file, SSCANF_EXTRACT_UTIME_STIME_RSS, &utime, &stime, &rss) == 3)
        {
            processes[index].proc_jiffies = (utime + stime);
            processes[index].rss = rss * g_page_sz >> 10; // kBytes

            uint32_t delta_proc_jiffies = processes[index].proc_jiffies - processes[index].proc_jiffies_prev;
            uint32_t delta_total_jiffies = g_uptime_jiffies - g_uptime_jiffies_prev;

            processes[index].cpu = ((double)delta_proc_jiffies / (double)delta_total_jiffies) * 100.0;

            // shift
            processes[index].proc_jiffies_prev = processes[index].proc_jiffies;

            DLT_LOG(dlt_ctxt_cpu, DLT_LOG_DEBUG, DLT_STRING(processes[index].cmdline),
                    DLT_INT((int)utime),
                    DLT_INT((int)stime),
                    DLT_INT((int)processes[index].proc_jiffies),
                    DLT_INT((int)delta_proc_jiffies),
                    DLT_INT((int)g_uptime_jiffies),
                    DLT_STRING("PROC proc_jiffies, delta_proc_jiffies, g_uptime_jiffies"));
        }

        fclose(file);
    }
}

static int match_cmdline(const char *pid, const char *target_cmdline)
{
    char cmdline_path[PATH_MAX];
    char cmdline[BUFFER_SIZE];
    FILE *file;

    snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%s/cmdline", pid);

    file = fopen(cmdline_path, "r");
    if (!file)
    {
        return 0;
    }

    fread(cmdline, 1, sizeof(cmdline) - 1, file);
    fclose(file);

    return strstr(cmdline, target_cmdline) != NULL;
}

static uint32_t get_pid(const char *cmdline)
{
    DIR *dir = opendir("/proc");
    struct dirent *entry;
    uint32_t pid = 0;

    if (NULL != dir)
    {
        while ((entry = readdir(dir)) != NULL)
        {
            if (entry->d_type == DT_DIR)
            {
                uint32_t spid = atoi(entry->d_name);

                if ((0 < spid) && (match_cmdline(entry->d_name, cmdline)))
                {
                    pid = spid;
                }
            }
        }
        closedir(dir);
    }

    return pid;
}

static uint8_t read_json_cmdlines(const char *json_file_path, ProcessCPU *processes)
{
    // Open the JSON file
    uint8_t found_apps = 0;
    FILE *file = fopen(json_file_path, "r");

    if (!file)
    {
        perror("Unable to open file");
        return 0;
    }

    // Read the entire file into a buffer
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    rewind(file);

    char *buffer = (char *)malloc(file_size + 1);
    if (!buffer)
    {
        perror("Memory allocation failed");
        fclose(file);
        return 0;
    }

    fread(buffer, 1, file_size, file);
    buffer[file_size] = '\0';
    fclose(file);

    // Parse the JSON data
    struct json_object *parsed_json = json_tokener_parse(buffer);
    free(buffer);

    if (!parsed_json)
    {
        fprintf(stderr, "Error parsing JSON data\n");
        return 0;
    }

    if (!json_object_is_type(parsed_json, json_type_array))
    {
        fprintf(stderr, "'files' is not a JSON array\n");
        json_object_put(parsed_json); // Free JSON object
        return 0;
    }

    // Iterate over the array and print file names
    size_t num_files = json_object_array_length(parsed_json);

    for (size_t i = 0; i < num_files; i++)
    {
        struct json_object *file_name_obj = json_object_array_get_idx(parsed_json, i);
        char *cmdline = json_object_get_string(file_name_obj);

        if (NULL != cmdline)
        {
            uint32_t spid = get_pid(cmdline);
            if (0 < spid)
            {
                processes[found_apps].pid = spid;
                processes[found_apps].cmdline = cmdline;

                if (g_args_info.verbose_given != 0)
                {
                    printf("got pid %d for %s\n", processes[found_apps].pid, processes[found_apps].cmdline);
                }

                found_apps++;
            }
            else
            {
                printf("not found : %s\n", cmdline);
            }
        }
    }

    // Free the JSON object
    json_object_put(parsed_json);

    return found_apps;
}

int main(int argc, char *argv[])
{
    // Parse command-line arguments using gengetopt
    if (cmdline_parser(argc, argv, &g_args_info) != 0)
    {
        return EXIT_FAILURE;
    }

    DLT_REGISTER_APP("LOAD", "proc load application");
    DLT_REGISTER_CONTEXT_LL_TS(dlt_ctxt_cpu, "CPU", "CPU Load", DLT_LOG_INFO, DLT_TRACE_STATUS_DEFAULT);
    DLT_REGISTER_CONTEXT_LL_TS(dlt_ctxt_proc, "PROC", "Process Stats", DLT_LOG_INFO, DLT_TRACE_STATUS_DEFAULT);

    DLT_LOG(dlt_ctxt_cpu, DLT_LOG_INFO, DLT_STRING("Starting proc load application."));

    // Get the JSON file specified by the -c option or the default value
    const char *json_file = g_args_info.config_arg;

    uint8_t cmdline_count;

    g_clock_ticks = sysconf(_SC_CLK_TCK);
    g_page_sz = sysconf(_SC_PAGE_SIZE);

    // Read cmdlines from the JSON file
    cmdline_count = read_json_cmdlines(json_file, processes);

    // Main loop
    while (0 < cmdline_count)
    {
        double cpu_usage = get_cpu_usage();
        ssize_t ram_avail_kBytes = get_kbytes_available();
        // update total_jiffies
        get_uptime();

        DLT_LOG(dlt_ctxt_cpu, DLT_LOG_INFO, DLT_INT((int)cpu_usage), DLT_UINT((int)ram_avail_kBytes));
        if (g_args_info.verbose_given != 0)
        {
            printf("global:\t%02.2f(%%cpu)\t%d(kBytes available)\n", cpu_usage, ram_avail_kBytes);
        }

        for (uint8_t i = 0; i < cmdline_count; i++)
        {
            if (0 < processes[i].pid)
            {
                get_process_cpu_kernel_5_4_3(i);

                DLT_LOG(dlt_ctxt_proc, DLT_LOG_INFO, DLT_STRING(processes[i].cmdline), DLT_FLOAT32(processes[i].cpu), DLT_UINT(processes[i].rss));
                if (g_args_info.verbose_given != 0)
                {
                    printf("%s:\t%02.2f(%%cpu)\t%d(kBytes)\n", processes[i].cmdline, processes[i].cpu, processes[i].rss);
                }
            }
        }

        // shift
        g_uptime_jiffies_prev = g_uptime_jiffies;

        sleep(g_args_info.interval_arg); // integration time
    }

    for (uint8_t i = 0; i < cmdline_count; i++)
    {
        free(processes[i].cmdline);
    }

    // Free memory used for arguments
    cmdline_parser_free(&g_args_info);

    DLT_UNREGISTER_CONTEXT(dlt_ctxt_cpu);
    DLT_UNREGISTER_CONTEXT(dlt_ctxt_proc);

    DLT_UNREGISTER_APP();

    return 0;
}
