/*
 * main.c — ib-tcg-dump entry point.
 *
 * Windows user-mode CLI (see specs/boot-chain-recorder). Subcommands:
 *
 *   capture  TBS-capture the genuine measured-boot log into tcglog.ib
 *   probe    validate the empty-bank precondition on the local TPM
 *   health   report per-CPU resident hypervisor health without TPM access
 *
 * No kernel code; links tbs.dll (Tbs.lib) only.
 */
#include <stdio.h>
#include <string.h>

#include "tcgdump.h"
#include "hypervisor_health.h"

static void usage(const char *argv0)
{
    printf("ib-tcg-dump — capture the measured-boot log and probe the TPM\n");
    printf("\n");
    printf("usage:\n");
    printf("  %s capture [--out <file>] [--full <file>]\n", argv0);
    printf("      capture the pre-boot log into tcglog.ib (default) or --out.\n");
    printf("      --full also writes the complete raw SRTM log to <file>.\n");
    printf("  %s probe\n", argv0);
    printf("      report TPM presence/version, active banks, PCR[0-7], log\n");
    printf("      format, and the empty-bank verdict (non-zero exit if NOT MET).\n");
    printf("  %s health\n", argv0);
    printf("      report per-CPU and aggregate hypervisor CPUID health, without\n");
    printf("      TPM access; read PASS/FAIL/ABSENT from the report (exit 0 means\n");
    printf("      the query completed, not that a hypervisor is present).\n");
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }

    if (strcmp(argv[1], "capture") == 0) {
        return cmd_capture(argc - 1, argv + 1);
    }
    if (strcmp(argv[1], "probe") == 0) {
        return cmd_probe(argc - 1, argv + 1);
    }
    if (strcmp(argv[1], "health") == 0) {
        if (argc != 2) {
            usage(argv[0]);
            return 2;
        }
        tcgdump_hypervisor_health_report();
        return 0;
    }

    fprintf(stderr, "error: unknown subcommand '%s'\n", argv[1]);
    usage(argv[0]);
    return 2;
}
