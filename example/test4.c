#include <mvlcc_wrap.h>
#include <stdio.h>
#include <signal.h>
#include <stdbool.h>

// On SIGUP force printing a report, on SIGINT or SIGTERM quit.
volatile int g_do_quit ;
volatile int g_do_report;

void signal_handler(int signum)
{
    if (signum == SIGHUP)
        g_do_report = 1;
    else
        g_do_quit = 1;
}

int setup_signal_handlers()
{
    /* Set up the structure to specify the new action. */
    struct sigaction new_action;
    new_action.sa_handler = signal_handler;
    sigemptyset (&new_action.sa_mask);
    new_action.sa_flags = 0;

    const int sigInterest[] = { SIGINT, SIGHUP, SIGTERM };

    for (int i = 0; i < sizeof(sigInterest) / sizeof(sigInterest[0]); i++)
    {
        if (sigaction(sigInterest[i], &new_action, NULL) != 0)
            return 1;
    }

    return 0;
}

int main(int argc, char *argv[]){
    mvlcc_t mvlc = NULL;
    int ec;
    uint64_t i;
    int tell = 1;

    if (argc <= 1)
    {
        fprintf(stderr, "Device not specified.\n");
        return 1;
    }

    if (setup_signal_handlers())
    {
        fprintf(stderr, "Could not set up signal handlers.\n");
        return 1;
    }

    mvlc = mvlcc_make_mvlc(argv[1]);

    if (ec = mvlcc_connect(mvlc))
    {
        fprintf(stderr, "Could not connect.\n");
        return 1;
    }

    uint16_t regAddr = 0x1304u; // the controller/crate id register
    uint32_t readValue = 0u;
    const uint32_t writeValue = 0b11;
    int ret = 0;

    for (i = 0; true; i++)
    {
        // XXX: can't actually pass the enum constants here. surprising.
        ec = mvlcc_single_vme_write(mvlc, 0xffff0000u + regAddr, writeValue, 32, 16);

        if (ec)
        {
            fprintf(stderr, "Could not write internal register through VME @ 0x%04x: %s.\n", regAddr, mvlcc_strerror(ec));
            ret = 1;
            break;
        }

        ec = mvlcc_single_vme_read(mvlc, 0xffff0000u + regAddr, &readValue, 32, 16);

        if (ec)
        {
            fprintf(stderr, "Could not read internal register through VME @ 0x%04x: %s.\n", regAddr, mvlcc_strerror(ec));
            ret = 1;
            break;
        }

        if (i == tell || g_do_report)
        {
            fprintf(stdout, "cycle=%lu, ", i);
            mvlcc_print_mvlc_cmd_counters(stdout, mvlc);
            fprintf(stdout, "\n");

            fflush(stdout);
            if (!g_do_report)
            {
                if (tell < 5000)
                    tell = tell * 2;
                else
                    tell += 5000;
            }

            g_do_report = 0;
        }

        if (g_do_quit)
            break;
    }

    printf("Final stats:\n");
    fprintf(stdout, "cycle=%lu, ", i);
    mvlcc_print_mvlc_cmd_counters(stdout, mvlc);
    fprintf(stdout, "\n");

    mvlcc_disconnect(mvlc);
    mvlcc_free_mvlc(mvlc);

    return ret;
}
