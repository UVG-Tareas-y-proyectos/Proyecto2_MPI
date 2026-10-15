#define _POSIX_C_SOURCE 200809L
#include "des_common.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <openssl/crypto.h>

typedef struct {
    const char *input;
    const char *output;
    const char *key_file;
    const char *keyword;
    const char *start_text;
    const char *end_text;
    const char *format_text;
    uint64_t start;
    uint64_t end;
    des_format format;
} options;

static void usage(FILE *stream)
{
    fprintf(stream,
        "Usage:\n"
        "  des_tool encrypt --input FILE --output FILE --key-file FILE [--format course|pkcs7]\n"
        "  des_tool decrypt --input FILE --output FILE --key-file FILE [--format course|pkcs7]\n"
        "  des_tool search --input FILE --keyword TEXT --start INDEX --end INDEX\n"
        "                  [--output FILE] [--format course|pkcs7]\n"
        "Indices are decimal; keys are in [0, 2^56), end is exclusive.\n"
        "Default format: pkcs7. Use course for files made with the class base code.\n"
        "Exit codes: 0 success/found; 1 argument or operational error; 2 search exhausted.\n");
}

static int same_file(const char *left, const char *right)
{
    struct stat a, b;
    if (left == NULL || right == NULL) return 0;
    if (strcmp(left, right) == 0) return 1;
    return stat(left, &a) == 0 && stat(right, &b) == 0 &&
           a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}

static int parse_options(int argc, char **argv, int search, options *opts)
{
    memset(opts, 0, sizeof(*opts));
    opts->format = DES_FORMAT_PKCS7;
    for (int i = 2; i < argc; i += 2) {
        const char **destination = NULL;
        if (strcmp(argv[i], "--input") == 0) destination = &opts->input;
        else if (strcmp(argv[i], "--output") == 0) destination = &opts->output;
        else if (!search && strcmp(argv[i], "--key-file") == 0) destination = &opts->key_file;
        else if (search && strcmp(argv[i], "--keyword") == 0) destination = &opts->keyword;
        else if (search && strcmp(argv[i], "--start") == 0) destination = &opts->start_text;
        else if (search && strcmp(argv[i], "--end") == 0) destination = &opts->end_text;
        else if (strcmp(argv[i], "--format") == 0) destination = &opts->format_text;
        if (destination == NULL) {
            fprintf(stderr, "error=Unknown or unsupported option '%s'\n", argv[i]);
            return 0;
        }
        if (*destination != NULL) {
            fprintf(stderr, "error=Duplicate option '%s'\n", argv[i]);
            return 0;
        }
        if (i + 1 >= argc || argv[i + 1][0] == '\0') {
            fprintf(stderr, "error=Option '%s' requires a nonempty value\n", argv[i]);
            return 0;
        }
        *destination = argv[i + 1];
    }
    if (opts->format_text != NULL) {
        if (strcmp(opts->format_text, "course") == 0) {
            opts->format = DES_FORMAT_COURSE;
        } else if (strcmp(opts->format_text, "pkcs7") != 0) {
            fprintf(stderr, "error=Format must be 'course' or 'pkcs7'\n");
            return 0;
        }
    }
    if (opts->input == NULL ||
        (!search && (opts->output == NULL || opts->key_file == NULL)) ||
        (search && (opts->keyword == NULL || opts->start_text == NULL || opts->end_text == NULL))) {
        fprintf(stderr, "error=Missing required options\n");
        return 0;
    }
    if (search && (!des_parse_index(opts->start_text, DES_KEY_LIMIT - 1, &opts->start) ||
                   !des_parse_index(opts->end_text, DES_KEY_LIMIT, &opts->end) ||
                   opts->start > opts->end)) {
        fprintf(stderr, "error=Invalid range: require 0 <= start <= end <= 2^56 and start < 2^56\n");
        return 0;
    }
    if (same_file(opts->input, opts->output) || same_file(opts->key_file, opts->output)) {
        fprintf(stderr, "error=Output must differ from the input and key file\n");
        return 0;
    }
    return 1;
}

static int run_cipher(const char *operation, const options *opts)
{
    des_crypto crypto = {0};
    unsigned char *input = NULL, *output = NULL;
    size_t input_length = 0, output_length = 0;
    uint64_t index = 0;
    char error[DES_ERROR_SIZE] = {0};
    int result = 1;
    if (!des_read_key_file(opts->key_file, &index, error, sizeof(error)) ||
        !des_read_file(opts->input, &input, &input_length, error, sizeof(error)) ||
        !des_crypto_init(&crypto, error, sizeof(error))) goto cleanup;
    if (strcmp(operation, "encrypt") == 0) {
        if (!des_encrypt_buffer(&crypto, index, opts->format, input, input_length, &output,
                                &output_length, error, sizeof(error))) goto cleanup;
    } else {
        if (!des_decrypt_buffer(&crypto, index, opts->format, input, input_length, &output,
                                &output_length, error, sizeof(error))) goto cleanup;
    }
    if (!des_write_file(opts->output, output, output_length, error, sizeof(error))) goto cleanup;
    printf("operation=%s\nformat=%s\nstatus=ok\ninput=%s\noutput=%s\nbytes_in=%zu\nbytes_out=%zu\n",
           operation, opts->format == DES_FORMAT_COURSE ? "course" : "pkcs7",
           opts->input, opts->output, input_length, output_length);
    result = 0;
cleanup:
    if (result != 0) fprintf(stderr, "status=error\nerror=%s\n", error);
    if (input != NULL) OPENSSL_cleanse(input, input_length);
    if (output != NULL) OPENSSL_cleanse(output, output_length);
    free(input);
    free(output);
    OPENSSL_cleanse(&index, sizeof(index));
    des_crypto_cleanup(&crypto);
    return result;
}

static int run_search(const options *opts)
{
    des_crypto crypto = {0};
    des_candidate candidate = {0};
    unsigned char *ciphertext = NULL;
    size_t ciphertext_length = 0, plaintext_length = 0;
    size_t keyword_length = strlen(opts->keyword);
    uint64_t attempts = 0, found_key = 0;
    double begin = 0.0, end = 0.0;
    char error[DES_ERROR_SIZE] = {0};
    int found = 0, result = 1;
    if (!des_read_file(opts->input, &ciphertext, &ciphertext_length, error, sizeof(error)) ||
        !des_crypto_init(&crypto, error, sizeof(error)) ||
        !des_candidate_init(&candidate, &crypto, ciphertext_length, opts->format,
                            error, sizeof(error))) goto cleanup;
    begin = des_monotonic_seconds();
    if (begin < 0.0) {
        (void)snprintf(error, sizeof(error), "Cannot read monotonic clock");
        goto cleanup;
    }
    for (uint64_t index = opts->start; index < opts->end; ++index) {
        int match = des_candidate_try(&candidate, index, ciphertext, ciphertext_length,
                                      (const unsigned char *)opts->keyword, keyword_length,
                                      &plaintext_length, error, sizeof(error));
        ++attempts;
        if (match < 0) goto cleanup;
        if (match == 1) {
            found = 1;
            found_key = index;
            break;
        }
    }
    end = des_monotonic_seconds();
    if (end < 0.0 || end < begin) {
        (void)snprintf(error, sizeof(error), "Cannot read monotonic clock");
        goto cleanup;
    }
    if (found && opts->output != NULL &&
        !des_write_file(opts->output, candidate.plaintext, plaintext_length, error, sizeof(error))) goto cleanup;
    printf("algorithm=sequential\nformat=%s\ninput=%s\nkeyword=%s\nstart=%" PRIu64 "\nend=%" PRIu64 "\n",
           opts->format == DES_FORMAT_COURSE ? "course" : "pkcs7",
           opts->input, opts->keyword, opts->start, opts->end);
    printf("status=%s\nattempts=%" PRIu64 "\nseconds=%.9f\n",
           found ? "found" : "not_found", attempts, end - begin);
    if (found) {
        printf("key=%" PRIu64 "\nbytes_recovered=%zu\n", found_key, plaintext_length);
        if (opts->output != NULL) printf("output=%s\n", opts->output);
    } else {
        printf("key=none\n");
    }
    result = found ? 0 : 2;
cleanup:
    if (result == 1) fprintf(stderr, "status=error\nerror=%s\n", error);
    des_candidate_cleanup(&candidate);
    des_crypto_cleanup(&crypto);
    free(ciphertext);
    return result;
}

int main(int argc, char **argv)
{
    options opts;
    int search;
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        usage(stdout);
        return 0;
    }
    if (argc < 2) {
        usage(stderr);
        return 1;
    }
    search = strcmp(argv[1], "search") == 0;
    if (!search && strcmp(argv[1], "encrypt") != 0 && strcmp(argv[1], "decrypt") != 0) {
        fprintf(stderr, "error=Unknown command '%s'\n", argv[1]);
        usage(stderr);
        return 1;
    }
    if (!parse_options(argc, argv, search, &opts)) {
        usage(stderr);
        return 1;
    }
    return search ? run_search(&opts) : run_cipher(argv[1], &opts);
}
