#include <mvlcc_wrap.h>
#include <signal.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

int timeval_subtract (struct timeval *result, struct timeval *x, struct timeval *y);
void print_buffer(FILE *out, const uint32_t *buffer, size_t size, const char *prefix);
int run_commands(mvlcc_t mvlc, mvlcc_command_list_t cmds);
long unsigned tv_to_ms(const struct timeval *tv);
void signal_handler(int signum);
void setup_signal_handlers();

typedef struct
{
    bool print_data;
} user_context_t;

MVLCC_DEFINE_EVENT_CALLBACK(event_data_callback)
{
    user_context_t *user_context = (user_context_t *)userContext;
    if (user_context->print_data)
    {
        fprintf(stdout, "event_data_callback: event %d, moduleCount=%u\n", eventIndex, moduleCount);

        for (size_t moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex)
        {
            mvlcc_module_data_t module_data = moduleDataList[moduleIndex];
            fprintf(stdout, "  event %d, module %zu, size=%zu: prefix=%zu, dynamic=%zu, suffix=%zu\n",
                eventIndex, moduleIndex, module_data.data_span.size,
                module_data.prefix_size, module_data.dynamic_size,
                module_data.suffix_size);
            if (module_data.data_span.size > 0)
                print_buffer(stdout, module_data.data_span.data, module_data.data_span.size, "    ");
        }
    }
}

MVLCC_DEFINE_SYSTEM_CALLBACK(system_event_callback)
{
    user_context_t *user_context = (user_context_t *)userContext;
    if (user_context->print_data)
    {
        printf("System event callback\n");
        print_buffer(stdout, data.data, data.size, "");
    }
}

typedef struct
{
    uint8_t *data;
    size_t capacity;
    size_t used;
} readout_buffer_t;

volatile bool signal_received_ = false;

int main(int argc, char *argv[])
{
    int res = 0;
    /* Do actually need all these at the top for the goto error cleanup to work. */
    mvlcc_crateconfig_t crateconfig = {};
    mvlcc_readout_parser_t parser = {};
    mvlcc_t mvlc = {};
    mvlcc_command_list_t mcst_start_commands = {};
    mvlcc_command_list_t mcst_stop_commands = {};
    mvlcc_readout_context_t readout_context = {};
    readout_buffer_t readout_buffer = {};

    setup_signal_handlers();

    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <crateconfig> [<duration_s>]\n", argv[0]);
        return 1;
    }

    const char *config_filename = argv[1];
    int duration_s = 0;

    if (argc > 2)
    {
        duration_s = atoi(argv[2]);
        if (duration_s < 0)
        {
            fprintf(stderr, "Invalid duration: %s\n", argv[2]);
            return 1;
        }
    }

    if ((res = mvlcc_crateconfig_from_file(&crateconfig, config_filename)))
    {
        fprintf(stderr, "Error reading crate config: %s\n", mvlcc_crateconfig_strerror(crateconfig));
        goto free_things;
    }

    user_context_t user_context = { .print_data = false };

    if ((res = mvlcc_readout_parser_create(&parser, crateconfig, &user_context, event_data_callback, system_event_callback)))
    {
        fprintf(stderr, "Error creating readout parser: %s\n", mvlcc_strerror(res));
        goto free_things;
    }

    mvlc = mvlcc_make_mvlc_from_crateconfig_t(crateconfig);

    if (!mvlcc_is_mvlc_valid(mvlc))
    {
        fprintf(stderr, "Error creating MVLC from crate config\n");
        goto free_things;
    }

    if ((res = mvlcc_connect(mvlc)))
    {
        fprintf(stderr, "Error connecting to MVLC: %s\n", mvlcc_strerror(res));
        goto free_things;
    }

    if ((res = mvlcc_init_readout2(mvlc, crateconfig)))
    {
        fprintf(stderr, "Error initializing readout: %s\n", mvlcc_strerror(res));
        goto free_things;
    }

    mcst_start_commands = mvlcc_crateconfig_get_mcst_daq_start(crateconfig);
    mcst_stop_commands = mvlcc_crateconfig_get_mcst_daq_start(crateconfig);
    readout_context = mvlcc_readout_context_create2(mvlc);

    const size_t readout_buffer_size = 1024 * 1024;
    readout_buffer.data = malloc(readout_buffer_size);
    readout_buffer.capacity = readout_buffer_size;

    int readout_timeout_ms = 500;
    bool keepRunning = true;

    /* Enables trigger processing of the MVLC. It is assumed that modules are
     * initialized and 'ready' at this point but not yet started. */
    if ((res = mvlcc_set_daq_mode(mvlc, true)))
    {
        fprintf(stderr, "Error enabling DAQ mode: %s\n", mvlcc_strerror(res));
        goto free_things;
    }

    /* Execute the multicast daq start commands. Ideally the readout would
     * already be running in a different thread. */
    if ((res = run_commands(mvlc, mcst_start_commands)))
    {
        fprintf(stderr, "Error running MCST DAQ start commands: %s\n", mvlcc_strerror(res));
        goto free_things;
    }

    size_t linear_buffer_number = 1;
    struct timeval start_time;
    gettimeofday(&start_time, NULL);
    struct timeval last_report_time = start_time;
    size_t total_bytes = 0;
    size_t report_bytes = 0;

    while (keepRunning)
    {
        readout_buffer.used = 0;

        res = mvlcc_readout(readout_context,
            readout_buffer.data, readout_buffer.capacity,
            &readout_buffer.used, readout_timeout_ms);

        if (res)
        {
            fprintf(stderr, "Error reading out data: %s\n", mvlcc_strerror(res));
            break;
        }

        fprintf(stderr, "readout received %zu bytes / %zu words\n", readout_buffer.used, readout_buffer.used / 4);

        if (readout_buffer.used == 0)
            continue;

        mvlcc_parse_result_t parse_result = mvlcc_readout_parser_parse_buffer(
            parser, linear_buffer_number++,
            (const uint32_t *)readout_buffer.data, readout_buffer.used / 4);

        if (parse_result != 0)
        {
            fprintf(stderr, "Error parsing readout buffer: %s\n", mvlcc_parse_result_to_string(parse_result));
        }

        total_bytes += readout_buffer.used;
        report_bytes += readout_buffer.used;

        struct timeval now;
        struct timeval delta;
        gettimeofday(&now, NULL);
        timeval_subtract(&delta, &now, &last_report_time);
        double millis = tv_to_ms(&delta);

        if (millis >= 500)
        {
            double totalMiB = total_bytes / (1024.0 * 1024.0);
            double MiB = report_bytes / (1024.0 * 1024.0);
            double MiBs = MiB / (millis / 1000.0);
            fprintf(stdout, "Processed %.2lf MiB in %.2lf ms (%.2lf MiB/s). Total: %.2lf MiB\n", MiB, millis, MiBs, totalMiB);
            last_report_time = now;
            report_bytes = 0;
        }

        if (signal_received_)
            keepRunning = false;

        timeval_subtract(&delta, &now, &start_time);
        double total_millis = tv_to_ms(&delta);
        if (total_millis >= 5 * 1000)
            keepRunning = false;
    }

    fprintf(stderr, "Stopping readout\n");

    /* Ideally the readout would still be running in another thread. */

    if ((res = run_commands(mvlc, mcst_stop_commands)))
    {
        fprintf(stderr, "Error running MCST DAQ stop commands: %s\n", mvlcc_strerror(res));
        goto free_things;
    }

    if ((res = mvlcc_set_daq_mode(mvlc, false)))
    {
        fprintf(stderr, "Error disabling DAQ mode: %s\n", mvlcc_strerror(res));
        goto free_things;
    }

    fprintf(stderr, "Readout stopped\n");

free_things:
    fprintf(stderr, "Free all the things!\n");
    free(readout_buffer.data);
    mvlcc_readout_context_destroy(readout_context);
    mvlcc_command_list_destroy(mcst_start_commands);
    mvlcc_command_list_destroy(mcst_stop_commands);
    mvlcc_free_mvlc(mvlc);
    mvlcc_readout_parser_destroy(parser);
    mvlcc_crateconfig_destroy(crateconfig);

    return res == 0 ? 0 : 1;
}

int run_commands(mvlcc_t mvlc, mvlcc_command_list_t cmds)
{
    int res = 0;
    const size_t cmd_count = mvlcc_command_list_total_size(cmds);

    for (size_t i=0; i<cmd_count; ++i)
    {
        mvlcc_command_t cmd = mvlcc_command_list_get_command(cmds, i);
        // not interested in the response contents, just the status
        res = mvlcc_run_command(mvlc, cmd, NULL, 0, NULL);
        mvlcc_command_destroy(cmd);

        if (res)
            break;
    }

    return res;
}

void print_buffer(FILE *out, const uint32_t *buffer, size_t size, const char *prefix)
{
    fprintf(out, prefix);

    for (size_t i=0; i<size; ++i)
    {
        fprintf(out, "%08x ", buffer[i]);
        if ((i+1) % 8 == 0)
            fprintf(out, "\n%s", prefix);
    }
    fprintf(out, "\n");
}

/* Source: https://www.gnu.org/software/libc/manual/html_node/Calculating-Elapsed-Time.html */
/* Subtract the ‘struct timeval’ values X and Y,
   storing the result in RESULT.
   Return 1 if the difference is negative, otherwise 0. */
int
timeval_subtract (struct timeval *result, struct timeval *x, struct timeval *y)
{
  /* Perform the carry for the later subtraction by updating y. */
  if (x->tv_usec < y->tv_usec) {
    int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
    y->tv_usec -= 1000000 * nsec;
    y->tv_sec += nsec;
  }
  if (x->tv_usec - y->tv_usec > 1000000) {
    int nsec = (x->tv_usec - y->tv_usec) / 1000000;
    y->tv_usec += 1000000 * nsec;
    y->tv_sec -= nsec;
  }

  /* Compute the time remaining to wait.
     tv_usec is certainly positive. */
  result->tv_sec = x->tv_sec - y->tv_sec;
  result->tv_usec = x->tv_usec - y->tv_usec;

  /* Return 1 if result is negative. */
  return x->tv_sec < y->tv_sec;
}

long unsigned tv_to_ms(const struct timeval *tv)
{
	return tv->tv_sec * 1000 + (tv->tv_usec + 500)/ 1000;
}

void signal_handler(int signum)
{
    (void) signum;
    signal_received_ = true;
}

void setup_signal_handlers()
{
    /* Set up the structure to specify the new action. */
    struct sigaction new_action;
    new_action.sa_handler = signal_handler;
    sigemptyset (&new_action.sa_mask);
    new_action.sa_flags = 0;

    sigaction(SIGINT, &new_action, NULL);
    sigaction(SIGHUP, &new_action, NULL);
    sigaction(SIGTERM, &new_action, NULL);
}
