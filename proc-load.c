#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include "cmdline.h"
#include "cJSON.h" // Assurez-vous d'avoir cJSON

#include "dlt/dlt.h"
#include "dlt/dlt_user_macros.h"

DLT_DECLARE_CONTEXT(dlt_ctxt_load);

#define BUFFER_SIZE 256

typedef struct
{
    long utime;
    long stime;
} ProcessCPU;

void get_cpu_usage(double *usage)
{
    static long prev_idle = 0, prev_total = 0;
    long idle, total;
    long user, nice, system, idle_time, iowait, irq, softirq, steal;
    char buffer[BUFFER_SIZE];
    FILE *file;

    file = fopen("/proc/stat", "r");
    if (!file)
    {
        perror("Erreur lors de l'ouverture de /proc/stat");
        exit(EXIT_FAILURE);
    }

    if (fgets(buffer, sizeof(buffer), file))
    {
        sscanf(buffer, "cpu %ld %ld %ld %ld %ld %ld %ld %ld",
               &user, &nice, &system, &idle_time, &iowait, &irq, &softirq, &steal);

        idle = idle_time + iowait;
        total = user + nice + system + idle + irq + softirq + steal;

        if (prev_total != 0)
        {
            long total_diff = total - prev_total;
            long idle_diff = idle - prev_idle;
            *usage = 100.0 * (1.0 - ((double)idle_diff / total_diff));
        }
        else
        {
            *usage = 0.0;
        }

        prev_idle = idle;
        prev_total = total;
    }

    fclose(file);
}

void get_uptime(double *uptime)
{
    FILE *file;

    file = fopen("/proc/uptime", "r");
    if (!file)
    {
        perror("Erreur lors de l'ouverture de /proc/uptime");
        exit(EXIT_FAILURE);
    }

    if (fscanf(file, "%lf", uptime) != 1)
    {
        perror("Erreur lors de la lecture de /proc/uptime");
        fclose(file);
        exit(EXIT_FAILURE);
    }

    fclose(file);
}

ProcessCPU get_process_cpu(const char *pid)
{
    char stat_path[BUFFER_SIZE];
    FILE *file;
    ProcessCPU p_cpu = {0, 0};

    snprintf(stat_path, sizeof(stat_path), "/proc/%s/stat", pid);

    file = fopen(stat_path, "r");
    if (!file)
    {
        perror("Erreur lors de l'ouverture de /proc/[pid]/stat");
        return p_cpu;
    }

    long utime, stime;
    if (fscanf(file, "%*d %*s %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %*u %ld %ld",
               &utime, &stime) == 2)
    {
        p_cpu.utime = utime;
        p_cpu.stime = stime;
    }

    fclose(file);
    return p_cpu;
}

int match_cmdline(const char *pid, const char *target_cmdline)
{
    char cmdline_path[BUFFER_SIZE];
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

void monitor_process(const char *cmdline, double *cpu_usage)
{
    DIR *dir = opendir("/proc");
    struct dirent *entry;
    ProcessCPU prev_cpu = {0, 0};
    double total_time = 0.0;

    if (!dir)
    {
        perror("Erreur lors de l'ouverture de /proc");
        exit(EXIT_FAILURE);
    }

    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_type == DT_DIR && atoi(entry->d_name) > 0)
        {
            if (match_cmdline(entry->d_name, cmdline))
            {
                ProcessCPU current_cpu = get_process_cpu(entry->d_name);

                long utime_diff = current_cpu.utime - prev_cpu.utime;
                long stime_diff = current_cpu.stime - prev_cpu.stime;
                total_time = utime_diff + stime_diff;

                *cpu_usage = (total_time / sysconf(_SC_CLK_TCK)) * 100.0;

                prev_cpu = current_cpu;
            }
        }
    }

    closedir(dir);
}

void read_json_cmdlines(const char *filename, char ***cmdlines, int *cmdline_count)
{
    *cmdline_count = 0;

    FILE *file = fopen(filename, "r");
    if (file)
    {
        fseek(file, 0, SEEK_END);
        long file_size = ftell(file);
        fseek(file, 0, SEEK_SET);

        char *json_content = malloc(file_size + 1);
        fread(json_content, 1, file_size, file);
        json_content[file_size] = '\0';
        fclose(file);

        cJSON *json = cJSON_Parse(json_content);
        if (!json)
        {
            perror("Erreur lors de l'analyse du fichier JSON");
            free(json_content);
            exit(EXIT_FAILURE);
        }

        int count = cJSON_GetArraySize(json);
        *cmdline_count = count;
        *cmdlines = malloc(count * sizeof(char *));

        for (int i = 0; i < count; i++)
        {
            cJSON *item = cJSON_GetArrayItem(json, i);
            (*cmdlines)[i] = strdup(item->valuestring);
        }

        cJSON_Delete(json);
        free(json_content);
    }
}

int main(int argc, char *argv[])
{
    // Parse command-line arguments using gengetopt
    struct gengetopt_args_info args_info;
    if (cmdline_parser(argc, argv, &args_info) != 0)
    {
        return EXIT_FAILURE;
    }

    DLT_REGISTER_APP("LOAD", "proc load application");
    DLT_REGISTER_CONTEXT_LL_TS(dlt_ctxt_load, "LOAD", "Load", DLT_LOG_INFO, DLT_TRACE_STATUS_DEFAULT);

    DLT_LOG(dlt_ctxt_load, DLT_LOG_INFO, DLT_STRING("Starting proc load application."));

    // Get the JSON file specified by the -c option or the default value
    const char *json_file = args_info.config_arg;

    char **cmdlines;
    int cmdline_count;

    // Read cmdlines from the JSON file
    read_json_cmdlines(json_file, &cmdlines, &cmdline_count);

    // Main loop
    while (1)
    {
        double cpu_usage = 0.0, uptime = 0.0;
        get_cpu_usage(&cpu_usage);
        get_uptime(&uptime);

        DLT_LOG(dlt_ctxt_load, DLT_LOG_INFO, DLT_STRING("CPU"), DLT_INT((int)cpu_usage));

        for (int i = 0; i < cmdline_count; i++)
        {
            double process_cpu = 0.0;
            monitor_process(cmdlines[i], &process_cpu);

            DLT_LOG(dlt_ctxt_load, DLT_LOG_INFO, DLT_STRING("PROC"), DLT_STRING(cmdlines[i]), DLT_INT((int)process_cpu));
        }

        sleep(3);
    }

    for (int i = 0; i < cmdline_count; i++)
    {
        free(cmdlines[i]);
    }
    free(cmdlines);

    // Free memory used for arguments
    cmdline_parser_free(&args_info);

    DLT_UNREGISTER_CONTEXT(dlt_ctxt_load);

    DLT_UNREGISTER_APP();

    return 0;
}
