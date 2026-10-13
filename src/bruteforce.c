#include "des_common.h"

#include <inttypes.h>
#include <limits.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum { REPORT_FOUND, REPORT_KEY, REPORT_EXHAUSTED, REPORT_ERROR, REPORT_SIZE };
enum { DECISION_CONTINUE, DECISION_FOUND, DECISION_NOT_FOUND, DECISION_ERROR };
enum { REPORT_TAG = 37 };
enum { DISTRIBUTION_CYCLIC, DISTRIBUTION_BLOCK };

typedef struct {
    const char *input;
    const char *keyword;
    const char *output;
    uint64_t start;
    uint64_t end;
    uint64_t batch;
    int distribution;
} search_options;

static void usage(FILE *stream, const char *program)
{
    fprintf(stream,
            "Usage: %s --input FILE --keyword TEXT [--start N] [--end N]\n"
            "          [--batch N] [--distribution cyclic|block] [--output FILE]\n"
            "Searches [start,end) with cyclic or contiguous-block MPI distribution.\n"
            "Defaults: start=0, end=72057594037927936 (2^56), batch=4096,\n"
            "distribution=cyclic.\n"
            "Exit codes: 0=found, 2=not_found, 1=error.\n",
            program);
}

static int same_file(const char *left, const char *right)
{
    struct stat a;
    struct stat b;
    if (left == NULL || right == NULL) {
        return 0;
    }
    return strcmp(left, right) == 0 ||
           (stat(left, &a) == 0 && stat(right, &b) == 0 &&
            a.st_dev == b.st_dev && a.st_ino == b.st_ino);
}

/* Returns 0 on error, 1 for a search, and 2 for --help. */
static int parse_options(int argc, char **argv, search_options *options,
                         char *error, size_t error_size)
{
    unsigned int seen = 0;
    int i;

    *options = (search_options){NULL, NULL, NULL, 0, DES_KEY_LIMIT, 4096,
                                DISTRIBUTION_CYCLIC};
    for (i = 1; i < argc; ++i) {
        const char *name = argv[i];
        const char *value;
        unsigned int flag;

        if (strcmp(name, "--help") == 0) {
            return 2;
        }
        if (strcmp(name, "--input") == 0) {
            flag = 1u;
        } else if (strcmp(name, "--keyword") == 0) {
            flag = 2u;
        } else if (strcmp(name, "--start") == 0) {
            flag = 4u;
        } else if (strcmp(name, "--end") == 0) {
            flag = 8u;
        } else if (strcmp(name, "--batch") == 0) {
            flag = 16u;
        } else if (strcmp(name, "--output") == 0) {
            flag = 32u;
        } else if (strcmp(name, "--distribution") == 0) {
            flag = 64u;
        } else {
            snprintf(error, error_size, "unknown argument: %s", name);
            return 0;
        }
        if ((seen & flag) != 0u || i + 1 >= argc) {
            snprintf(error, error_size, "duplicate argument or missing value: %s", name);
            return 0;
        }
        seen |= flag;
        value = argv[++i];
        if (value[0] == '\0') {
            snprintf(error, error_size, "empty value for %s", name);
            return 0;
        }
        if (flag == 1u) {
            options->input = value;
        } else if (flag == 2u) {
            options->keyword = value;
        } else if (flag == 32u) {
            options->output = value;
        } else if (flag == 64u) {
            if (strcmp(value, "cyclic") == 0) {
                options->distribution = DISTRIBUTION_CYCLIC;
            } else if (strcmp(value, "block") == 0) {
                options->distribution = DISTRIBUTION_BLOCK;
            } else {
                snprintf(error, error_size,
                         "invalid distribution: use cyclic or block");
                return 0;
            }
        } else {
            uint64_t number;
            uint64_t limit = flag == 16u ? UINT64_MAX : DES_KEY_LIMIT;
            if (!des_parse_index(value, limit, &number)) {
                snprintf(error, error_size, "invalid decimal value for %s", name);
                return 0;
            }
            if (flag == 4u) {
                options->start = number;
            } else if (flag == 8u) {
                options->end = number;
            } else {
                options->batch = number;
            }
        }
    }
    if (options->input == NULL || options->keyword == NULL) {
        snprintf(error, error_size, "--input and --keyword are required");
        return 0;
    }
    if (options->start > options->end || options->batch == 0) {
        snprintf(error, error_size, "require 0 <= start <= end <= 2^56 and batch > 0");
        return 0;
    }
    if (strlen(options->keyword) >= (size_t)INT_MAX) {
        snprintf(error, error_size, "keyword is too long for MPI");
        return 0;
    }
    if (same_file(options->input, options->output)) {
        snprintf(error, error_size, "output must differ from the ciphertext input");
        return 0;
    }
    return 1;
}

static void mpi_check(int result, const char *operation, int rank)
{
    if (result != MPI_SUCCESS) {
        char message[MPI_MAX_ERROR_STRING];
        int length = 0;
        MPI_Error_string(result, message, &length);
        fprintf(stderr, "rank=%d error=%s: %.*s\n", rank, operation, length, message);
        MPI_Abort(MPI_COMM_WORLD, 1);
        exit(1);
    }
}

#define MPI_CALL(operation) mpi_check((operation), #operation, rank)

int main(int argc, char **argv)
{
    int rank = 0;
    int processes = 1;
    int setup_status = 0;
    int local_ready = 1;
    int all_ready = 0;
    int exit_code = 1;
    int result;
    search_options options = {0};
    unsigned char *ciphertext = NULL;
    unsigned char *keyword = NULL;
    size_t ciphertext_length = 0;
    size_t keyword_length = 0;
    uint64_t settings[6] = {0};
    uint64_t *reports = NULL;
    MPI_Request *requests = NULL;
    des_crypto crypto = {0};
    des_candidate candidate = {0};
    char error[DES_ERROR_SIZE] = {0};
    uint64_t attempts = 0;
    uint64_t total_attempts = 0;
    uint64_t next;
    uint64_t local_end;
    uint64_t stride;
    uint64_t local_report[REPORT_SIZE] = {0};
    uint64_t decision[2] = {DECISION_CONTINUE, 0};
    double begin;
    double elapsed;
    double max_elapsed = 0.0;

    result = MPI_Init(&argc, &argv);
    if (result != MPI_SUCCESS) {
        fprintf(stderr, "error=MPI_Init failed\n");
        return 1;
    }
    MPI_CALL(MPI_Comm_rank(MPI_COMM_WORLD, &rank));
    MPI_CALL(MPI_Comm_size(MPI_COMM_WORLD, &processes));
    MPI_CALL(MPI_Comm_set_errhandler(MPI_COMM_WORLD, MPI_ERRORS_RETURN));

    /* Only root reads arguments and input. It announces every validation result
     * before other ranks enter a collective that needs this input. */
    if (rank == 0) {
        result = parse_options(argc, argv, &options, error, sizeof(error));
        if (result == 2) {
            usage(stdout, argv[0]);
            setup_status = 2;
        } else if (!result) {
            fprintf(stderr, "error=%s\n", error);
            usage(stderr, argv[0]);
            setup_status = 1;
        } else if (!des_read_file(options.input, &ciphertext, &ciphertext_length,
                                  error, sizeof(error))) {
            fprintf(stderr, "error=%s\n", error);
            setup_status = 1;
        } else if (ciphertext_length == 0 ||
                   ciphertext_length % DES_BLOCK_SIZE != 0 ||
                   ciphertext_length > (size_t)INT_MAX) {
            fprintf(stderr, "error=ciphertext must contain complete DES blocks and fit MPI count\n");
            setup_status = 1;
        } else {
            keyword_length = strlen(options.keyword);
            settings[0] = options.start;
            settings[1] = options.end;
            settings[2] = options.batch;
            settings[3] = (uint64_t)ciphertext_length;
            settings[4] = (uint64_t)keyword_length;
            settings[5] = (uint64_t)options.distribution;
        }
    }
    MPI_CALL(MPI_Bcast(&setup_status, 1, MPI_INT, 0, MPI_COMM_WORLD));
    if (setup_status != 0) {
        exit_code = setup_status == 2 ? 0 : 1;
        goto cleanup;
    }
    MPI_CALL(MPI_Bcast(settings, 6, MPI_UINT64_T, 0, MPI_COMM_WORLD));
    ciphertext_length = (size_t)settings[3];
    keyword_length = (size_t)settings[4];
    keyword = malloc(keyword_length + 1);
    if (rank != 0) {
        ciphertext = malloc(ciphertext_length);
    }
    if (rank == 0) {
        if ((size_t)processes > SIZE_MAX / (REPORT_SIZE * sizeof(*reports)) ||
            (size_t)processes > SIZE_MAX / sizeof(*requests)) {
            local_ready = 0;
        } else {
            reports = calloc((size_t)processes * REPORT_SIZE, sizeof(*reports));
            if (processes > 1) {
                requests = malloc((size_t)(processes - 1) * sizeof(*requests));
            }
        }
        if (reports == NULL || (processes > 1 && requests == NULL)) {
            local_ready = 0;
        }
    }
    if (keyword == NULL || ciphertext == NULL) {
        local_ready = 0;
    }
    MPI_CALL(MPI_Allreduce(&local_ready, &all_ready, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD));
    if (!all_ready) {
        if (rank == 0) {
            fprintf(stderr, "error=cannot allocate MPI input or status buffers\n");
        }
        goto cleanup;
    }
    if (rank == 0) {
        memcpy(keyword, options.keyword, keyword_length + 1);
    }
    MPI_CALL(MPI_Bcast(keyword, (int)(keyword_length + 1), MPI_BYTE, 0, MPI_COMM_WORLD));
    MPI_CALL(MPI_Bcast(ciphertext, (int)ciphertext_length, MPI_BYTE, 0, MPI_COMM_WORLD));

    local_ready = des_crypto_init(&crypto, error, sizeof(error));
    if (local_ready) {
        local_ready = des_candidate_init(&candidate, &crypto, ciphertext_length,
                                         error, sizeof(error));
    }
    if (!local_ready) {
        fprintf(stderr, "rank=%d error=%s\n", rank, error);
    }
    MPI_CALL(MPI_Allreduce(&local_ready, &all_ready, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD));
    if (!all_ready) {
        goto cleanup;
    }

    /* Both policies partition [start,end) without overlap and include root.
     * A rank with no keys still sends a report and joins every broadcast. */
    if (settings[5] == DISTRIBUTION_BLOCK) {
        uint64_t count = settings[1] - settings[0];
        uint64_t process_count = (uint64_t)processes;
        uint64_t base = count / process_count;
        uint64_t remainder = count % process_count;
        uint64_t rank_value = (uint64_t)rank;
        uint64_t offset = rank_value * base +
                          (rank_value < remainder ? rank_value : remainder);
        uint64_t local_count = base + (rank_value < remainder ? 1u : 0u);
        next = settings[0] + offset;
        local_end = next + local_count;
        stride = 1;
    } else {
        next = (uint64_t)rank < settings[1] - settings[0]
                   ? settings[0] + (uint64_t)rank : settings[1];
        local_end = settings[1];
        stride = (uint64_t)processes;
    }
    local_report[REPORT_EXHAUSTED] = next >= local_end;
    MPI_CALL(MPI_Barrier(MPI_COMM_WORLD));
    begin = MPI_Wtime();
    do {
        uint64_t tried_in_batch = 0;
        int process;

        /* Receives are posted before root searches its own batch. Buffers and
         * requests remain alive until every matching MPI_Wait completes. */
        if (rank == 0) {
            for (process = 1; process < processes; ++process) {
                MPI_CALL(MPI_Irecv(reports + (size_t)process * REPORT_SIZE,
                                   REPORT_SIZE, MPI_UINT64_T, process, REPORT_TAG,
                                   MPI_COMM_WORLD, &requests[process - 1]));
            }
        }
        while (!local_report[REPORT_EXHAUSTED] &&
               !local_report[REPORT_FOUND] && !local_report[REPORT_ERROR] &&
               tried_in_batch < settings[2]) {
            size_t plaintext_length = 0;
            int matched = des_candidate_try(&candidate, next, ciphertext,
                                             ciphertext_length, keyword,
                                             keyword_length, &plaintext_length,
                                             error, sizeof(error));
            ++attempts;
            ++tried_in_batch;
            if (matched < 0) {
                local_report[REPORT_ERROR] = 1;
                fprintf(stderr, "rank=%d error=%s\n", rank, error);
            } else if (matched == 1) {
                local_report[REPORT_FOUND] = 1;
                local_report[REPORT_KEY] = next;
            }
            next += stride;
            local_report[REPORT_EXHAUSTED] = next >= local_end;
        }
        if (rank != 0) {
            MPI_CALL(MPI_Send(local_report, REPORT_SIZE, MPI_UINT64_T, 0,
                              REPORT_TAG, MPI_COMM_WORLD));
        } else {
            int exhausted = 1;
            int failed = 0;
            int found = 0;
            uint64_t found_key = DES_KEY_LIMIT;
            memcpy(reports, local_report, sizeof(local_report));
            for (process = 1; process < processes; ++process) {
                MPI_CALL(MPI_Wait(&requests[process - 1], MPI_STATUS_IGNORE));
            }
            for (process = 0; process < processes; ++process) {
                const uint64_t *report = reports + (size_t)process * REPORT_SIZE;
                exhausted = exhausted && report[REPORT_EXHAUSTED] != 0;
                failed = failed || report[REPORT_ERROR] != 0;
                if (report[REPORT_FOUND] && report[REPORT_KEY] < found_key) {
                    found = 1;
                    found_key = report[REPORT_KEY];
                }
            }
            decision[0] = failed ? DECISION_ERROR
                                   : found ? DECISION_FOUND
                                           : exhausted ? DECISION_NOT_FOUND
                                                       : DECISION_CONTINUE;
            decision[1] = found_key;
        }
        MPI_CALL(MPI_Bcast(decision, 2, MPI_UINT64_T, 0, MPI_COMM_WORLD));
    } while (decision[0] == DECISION_CONTINUE);

    /* Each elapsed time subtracts two readings from the same rank. Input and
     * crypto setup are excluded; batch communication is included. */
    elapsed = MPI_Wtime() - begin;
    MPI_CALL(MPI_Reduce(&elapsed, &max_elapsed, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD));
    MPI_CALL(MPI_Reduce(&attempts, &total_attempts, 1, MPI_UINT64_T, MPI_SUM, 0, MPI_COMM_WORLD));
    if (rank == 0) {
        const char *status;
        if (decision[0] == DECISION_FOUND) {
            unsigned char *plaintext = NULL;
            size_t plaintext_length = 0;
            int verified = des_decrypt_buffer(&crypto, decision[1], ciphertext,
                                               ciphertext_length, &plaintext,
                                               &plaintext_length, error,
                                               sizeof(error));
            if (verified && !des_contains_bytes(plaintext, plaintext_length,
                                                 keyword, keyword_length)) {
                snprintf(error, sizeof(error), "recovered key did not verify keyword");
                verified = 0;
            }
            if (verified && options.output != NULL) {
                verified = des_write_file(options.output, plaintext,
                                            plaintext_length, error, sizeof(error));
            }
            free(plaintext);
            if (verified) {
                exit_code = 0;
                status = "found";
            } else {
                fprintf(stderr, "error=%s\n", error);
                exit_code = 1;
                status = "error";
            }
        } else if (decision[0] == DECISION_NOT_FOUND) {
            exit_code = 2;
            status = "not_found";
        } else {
            exit_code = 1;
            status = "error";
        }
        printf("algorithm=mpi\ninput=%s\nkeyword=%s\nprocesses=%d\nstart=%" PRIu64
               "\nend=%" PRIu64 "\nbatch=%" PRIu64 "\ndistribution=%s\n",
               options.input, options.keyword, processes, settings[0],
                settings[1], settings[2],
                settings[5] == DISTRIBUTION_BLOCK ? "block" : "cyclic");
        if (decision[0] == DECISION_FOUND) {
            printf("key=%" PRIu64 "\n", decision[1]);
        } else {
            printf("key=none\n");
        }
        printf("attempts=%" PRIu64 "\nstatus=%s\nseconds=%.9f\n",
               total_attempts, status, max_elapsed);
    }
    MPI_CALL(MPI_Bcast(&exit_code, 1, MPI_INT, 0, MPI_COMM_WORLD));

cleanup:
    des_candidate_cleanup(&candidate);
    des_crypto_cleanup(&crypto);
    free(requests);
    free(reports);
    free(keyword);
    free(ciphertext);
    result = MPI_Finalize();
    return result == MPI_SUCCESS ? exit_code : 1;
}
