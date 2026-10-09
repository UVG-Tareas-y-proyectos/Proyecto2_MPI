#define _POSIX_C_SOURCE 200809L
#include "des_common.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <openssl/crypto.h>
#include <openssl/err.h>

static void set_error(char *error, size_t size, const char *format, ...)
{
    va_list args;
    if (error == NULL || size == 0) return;
    va_start(args, format);
    (void)vsnprintf(error, size, format, args);
    va_end(args);
}

static void crypto_error(char *error, size_t size, const char *operation)
{
    char detail[160];
    unsigned long code = ERR_get_error();
    if (code != 0) {
        ERR_error_string_n(code, detail, sizeof(detail));
        set_error(error, size, "%s: %s", operation, detail);
    } else {
        set_error(error, size, "%s", operation);
    }
}

static int valid_ciphertext_length(size_t length)
{
    return length > 0 && length % DES_BLOCK_SIZE == 0 &&
           length <= DES_MAX_FILE_SIZE;
}

int des_crypto_init(des_crypto *crypto, char *error, size_t error_size)
{
    if (crypto == NULL) {
        set_error(error, error_size, "Missing crypto context");
        return 0;
    }
    memset(crypto, 0, sizeof(*crypto));
    ERR_clear_error();
    crypto->legacy = OSSL_PROVIDER_load(NULL, "legacy");
    if (crypto->legacy == NULL) {
        crypto_error(error, error_size, "Cannot load OpenSSL legacy provider");
        return 0;
    }
    crypto->cipher = EVP_CIPHER_fetch(NULL, "DES-ECB", "provider=legacy");
    if (crypto->cipher == NULL) {
        crypto_error(error, error_size, "DES-ECB is unavailable");
        des_crypto_cleanup(crypto);
        return 0;
    }
    if (EVP_CIPHER_get_block_size(crypto->cipher) != (int)DES_BLOCK_SIZE ||
        EVP_CIPHER_get_key_length(crypto->cipher) != (int)DES_BLOCK_SIZE) {
        set_error(error, error_size, "Unexpected DES-ECB parameters");
        des_crypto_cleanup(crypto);
        return 0;
    }
    return 1;
}

void des_crypto_cleanup(des_crypto *crypto)
{
    if (crypto == NULL) return;
    EVP_CIPHER_free(crypto->cipher);
    if (crypto->legacy != NULL) (void)OSSL_PROVIDER_unload(crypto->legacy);
    memset(crypto, 0, sizeof(*crypto));
}

int des_parse_index(const char *text, uint64_t maximum, uint64_t *value)
{
    uint64_t result = 0;
    if (text == NULL || value == NULL || *text == '\0') return 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        unsigned digit;
        if (*p < '0' || *p > '9') return 0;
        digit = (unsigned)(*p - '0');
        if (result > maximum / 10 ||
            (result == maximum / 10 && digit > maximum % 10)) return 0;
        result = result * 10 + digit;
    }
    *value = result;
    return 1;
}

int des_read_key_file(const char *path, uint64_t *value,
                      char *error, size_t error_size)
{
    FILE *file;
    char text[128];
    size_t length;
    char *begin, *end;
    if (path == NULL || value == NULL) {
        set_error(error, error_size, "Missing key file argument");
        return 0;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        set_error(error, error_size, "Cannot open key file '%s': %s", path, strerror(errno));
        return 0;
    }
    length = fread(text, 1, sizeof(text) - 1, file);
    if (ferror(file)) {
        set_error(error, error_size, "Cannot read key file '%s'", path);
        (void)fclose(file);
        return 0;
    }
    if (length == sizeof(text) - 1) {
        set_error(error, error_size, "Key file is too long (maximum 126 bytes)");
        (void)fclose(file);
        return 0;
    }
    if (fclose(file) != 0) {
        set_error(error, error_size, "Cannot close key file '%s': %s", path, strerror(errno));
        return 0;
    }
    if (memchr(text, '\0', length) != NULL) {
        set_error(error, error_size, "Key file contains a NUL byte");
        return 0;
    }
    text[length] = '\0';
    begin = text;
    while (isspace((unsigned char)*begin)) ++begin;
    end = text + length;
    while (end > begin && isspace((unsigned char)end[-1])) --end;
    *end = '\0';
    if (!des_parse_index(begin, DES_KEY_LIMIT - 1, value)) {
        OPENSSL_cleanse(text, sizeof(text));
        set_error(error, error_size, "Key file must contain one decimal index in [0, 2^56)");
        return 0;
    }
    OPENSSL_cleanse(text, sizeof(text));
    return 1;
}

int des_read_file(const char *path, unsigned char **data, size_t *length,
                  char *error, size_t error_size)
{
    FILE *file;
    long size;
    unsigned char *buffer;
    if (path == NULL || data == NULL || length == NULL) {
        set_error(error, error_size, "Missing file argument");
        return 0;
    }
    *data = NULL;
    *length = 0;
    file = fopen(path, "rb");
    if (file == NULL) {
        set_error(error, error_size, "Cannot open '%s': %s", path, strerror(errno));
        return 0;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        set_error(error, error_size, "Cannot determine file size for '%s'", path);
        (void)fclose(file);
        return 0;
    }
    if ((unsigned long)size > (unsigned long)DES_MAX_FILE_SIZE) {
        set_error(error, error_size, "File exceeds maximum of %zu bytes", DES_MAX_FILE_SIZE);
        (void)fclose(file);
        return 0;
    }
    buffer = malloc(size > 0 ? (size_t)size : 1);
    if (buffer == NULL) {
        set_error(error, error_size, "Cannot allocate file buffer");
        (void)fclose(file);
        return 0;
    }
    if (fread(buffer, 1, (size_t)size, file) != (size_t)size ||
        fgetc(file) != EOF || ferror(file)) {
        set_error(error, error_size, "Cannot read complete file '%s' (or size changed)", path);
        free(buffer);
        (void)fclose(file);
        return 0;
    }
    if (fclose(file) != 0) {
        set_error(error, error_size, "Cannot close '%s': %s", path, strerror(errno));
        free(buffer);
        return 0;
    }
    *data = buffer;
    *length = (size_t)size;
    return 1;
}

int des_write_file(const char *path, const unsigned char *data, size_t length,
                   char *error, size_t error_size)
{
    char *temporary;
    size_t path_length;
    int fd, ok = 1;
    FILE *file;
    if (path == NULL || (data == NULL && length != 0)) {
        set_error(error, error_size, "Missing output argument");
        return 0;
    }
    path_length = strlen(path);
    if (path_length > SIZE_MAX - 12) {
        set_error(error, error_size, "Output path is too long");
        return 0;
    }
    temporary = malloc(path_length + 12);
    if (temporary == NULL) {
        set_error(error, error_size, "Cannot allocate output path");
        return 0;
    }
    (void)snprintf(temporary, path_length + 12, "%s.tmp.XXXXXX", path);
    fd = mkstemp(temporary);
    if (fd < 0) {
        set_error(error, error_size, "Cannot create output '%s': %s", path, strerror(errno));
        free(temporary);
        return 0;
    }
    file = fdopen(fd, "wb");
    if (file == NULL) {
        set_error(error, error_size, "Cannot open output stream: %s", strerror(errno));
        (void)close(fd);
        (void)unlink(temporary);
        free(temporary);
        return 0;
    }
    if (length != 0 && fwrite(data, 1, length, file) != length) {
        set_error(error, error_size, "Cannot write output '%s': %s", path, strerror(errno));
        ok = 0;
    }
    if (fclose(file) != 0) {
        set_error(error, error_size, "Cannot finish output '%s': %s", path, strerror(errno));
        ok = 0;
    }
    if (ok && rename(temporary, path) != 0) {
        set_error(error, error_size, "Cannot replace output '%s': %s", path, strerror(errno));
        ok = 0;
    }
    if (!ok) (void)unlink(temporary);
    free(temporary);
    return ok;
}

void des_index_to_key(uint64_t index, unsigned char key[DES_BLOCK_SIZE])
{
    for (unsigned i = 0; i < DES_BLOCK_SIZE; ++i) {
        unsigned group = (unsigned)((index >> (7u * (7u - i))) & UINT64_C(0x7f));
        unsigned ones = 0;
        for (unsigned bit = 0; bit < 7; ++bit) ones += (group >> bit) & 1u;
        key[i] = (unsigned char)((group << 1) | ((ones & 1u) ^ 1u));
    }
}

int des_encrypt_buffer(const des_crypto *crypto, uint64_t index,
                       const unsigned char *input, size_t input_length,
                       unsigned char **output, size_t *output_length,
                       char *error, size_t error_size)
{
    EVP_CIPHER_CTX *ctx = NULL;
    unsigned char key[DES_BLOCK_SIZE];
    unsigned char *buffer = NULL;
    int first = 0, last = 0, success = 0;
    if (output == NULL || output_length == NULL) {
        set_error(error, error_size, "Missing encryption output");
        return 0;
    }
    *output = NULL;
    *output_length = 0;
    if (crypto == NULL || crypto->cipher == NULL || index >= DES_KEY_LIMIT ||
        (input == NULL && input_length != 0) ||
        input_length > DES_MAX_PLAINTEXT_SIZE) {
        set_error(error, error_size, "Invalid encryption parameters or oversized input");
        return 0;
    }
    buffer = malloc(input_length + DES_BLOCK_SIZE);
    ctx = EVP_CIPHER_CTX_new();
    if (buffer == NULL || ctx == NULL) {
        set_error(error, error_size, "Cannot allocate encryption context");
        goto cleanup;
    }
    des_index_to_key(index, key);
    ERR_clear_error();
    if (EVP_EncryptInit_ex2(ctx, crypto->cipher, key, NULL, NULL) != 1 ||
        EVP_CIPHER_CTX_set_padding(ctx, 1) != 1 ||
        EVP_EncryptUpdate(ctx, buffer, &first, input, (int)input_length) != 1 ||
        EVP_EncryptFinal_ex(ctx, buffer + first, &last) != 1) {
        crypto_error(error, error_size, "DES encryption failed");
        goto cleanup;
    }
    *output = buffer;
    *output_length = (size_t)first + (size_t)last;
    buffer = NULL;
    success = 1;
cleanup:
    OPENSSL_cleanse(key, sizeof(key));
    EVP_CIPHER_CTX_free(ctx);
    free(buffer);
    return success;
}

int des_candidate_init(des_candidate *candidate, const des_crypto *crypto,
                       size_t ciphertext_length, char *error, size_t error_size)
{
    if (candidate == NULL) {
        set_error(error, error_size, "Missing candidate context");
        return 0;
    }
    memset(candidate, 0, sizeof(*candidate));
    if (crypto == NULL || crypto->cipher == NULL || !valid_ciphertext_length(ciphertext_length)) {
        set_error(error, error_size, "Ciphertext must contain complete eight-byte blocks and fit the file limit");
        return 0;
    }
    candidate->crypto = crypto;
    candidate->capacity = ciphertext_length + DES_BLOCK_SIZE;
    candidate->ctx = EVP_CIPHER_CTX_new();
    candidate->plaintext = malloc(candidate->capacity);
    if (candidate->ctx == NULL || candidate->plaintext == NULL) {
        set_error(error, error_size, "Cannot allocate candidate context");
        des_candidate_cleanup(candidate);
        return 0;
    }
    ERR_clear_error();
    if (EVP_DecryptInit_ex2(candidate->ctx, crypto->cipher, NULL, NULL, NULL) != 1 ||
        EVP_CIPHER_CTX_set_padding(candidate->ctx, 0) != 1) {
        crypto_error(error, error_size, "Cannot initialize candidate DES context");
        des_candidate_cleanup(candidate);
        return 0;
    }
    return 1;
}

void des_candidate_cleanup(des_candidate *candidate)
{
    if (candidate == NULL) return;
    EVP_CIPHER_CTX_free(candidate->ctx);
    if (candidate->plaintext != NULL) {
        OPENSSL_cleanse(candidate->plaintext, candidate->capacity);
        free(candidate->plaintext);
    }
    memset(candidate, 0, sizeof(*candidate));
}

static int decrypt_candidate(des_candidate *candidate, uint64_t index,
                             const unsigned char *ciphertext, size_t ciphertext_length,
                             size_t *plaintext_length, char *error, size_t error_size)
{
    unsigned char key[DES_BLOCK_SIZE];
    int first = 0, last = 0;
    unsigned pad;
    *plaintext_length = 0;
    if (candidate == NULL || candidate->ctx == NULL || ciphertext == NULL ||
        !valid_ciphertext_length(ciphertext_length) || index >= DES_KEY_LIMIT ||
        ciphertext_length + DES_BLOCK_SIZE > candidate->capacity) {
        set_error(error, error_size, "Invalid candidate parameters");
        return -1;
    }
    des_index_to_key(index, key);
    ERR_clear_error();
    /* Reuse the cipher and allocated output; only the candidate key changes. */
    if (EVP_DecryptInit_ex2(candidate->ctx, NULL, key, NULL, NULL) != 1 ||
        EVP_DecryptUpdate(candidate->ctx, candidate->plaintext, &first,
                          ciphertext, (int)ciphertext_length) != 1 ||
        EVP_DecryptFinal_ex(candidate->ctx, candidate->plaintext + first, &last) != 1) {
        OPENSSL_cleanse(key, sizeof(key));
        crypto_error(error, error_size, "DES candidate decryption failed");
        return -1;
    }
    OPENSSL_cleanse(key, sizeof(key));
    if ((size_t)first + (size_t)last != ciphertext_length) {
        set_error(error, error_size, "Unexpected DES plaintext length");
        return -1;
    }
    /* Verify PKCS#7 ourselves: invalid padding rejects a key, not the search. */
    pad = candidate->plaintext[ciphertext_length - 1];
    if (pad == 0 || pad > DES_BLOCK_SIZE) return 0;
    for (unsigned i = 0; i < pad; ++i) {
        if (candidate->plaintext[ciphertext_length - 1 - i] != pad) return 0;
    }
    *plaintext_length = ciphertext_length - pad;
    return 1;
}

int des_contains_bytes(const unsigned char *data, size_t length,
                       const unsigned char *keyword, size_t keyword_length)
{
    if (data == NULL || keyword == NULL || keyword_length == 0 || keyword_length > length) return 0;
    for (size_t i = 0; i <= length - keyword_length; ++i) {
        if (memcmp(data + i, keyword, keyword_length) == 0) return 1;
    }
    return 0;
}

int des_candidate_try(des_candidate *candidate, uint64_t index,
                      const unsigned char *ciphertext, size_t ciphertext_length,
                      const unsigned char *keyword, size_t keyword_length,
                      size_t *plaintext_length, char *error, size_t error_size)
{
    int result;
    if (keyword == NULL || keyword_length == 0 || plaintext_length == NULL) {
        set_error(error, error_size, "Search requires a nonempty keyword and length output");
        return -1;
    }
    result = decrypt_candidate(candidate, index, ciphertext, ciphertext_length,
                               plaintext_length, error, error_size);
    if (result != 1) return result;
    return des_contains_bytes(candidate->plaintext, *plaintext_length, keyword, keyword_length);
}

int des_decrypt_buffer(const des_crypto *crypto, uint64_t index,
                       const unsigned char *input, size_t input_length,
                       unsigned char **output, size_t *output_length,
                       char *error, size_t error_size)
{
    des_candidate candidate;
    int result;
    if (output == NULL || output_length == NULL) {
        set_error(error, error_size, "Missing decryption output");
        return 0;
    }
    *output = NULL;
    *output_length = 0;
    if (!des_candidate_init(&candidate, crypto, input_length, error, error_size)) return 0;
    result = decrypt_candidate(&candidate, index, input, input_length, output_length, error, error_size);
    if (result == 1) {
        *output = candidate.plaintext;
        candidate.plaintext = NULL;
    } else if (result == 0) {
        set_error(error, error_size, "Invalid PKCS#7 padding (wrong key or damaged ciphertext)");
    }
    des_candidate_cleanup(&candidate);
    return result == 1;
}

double des_monotonic_seconds(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return -1.0;
    return (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
}
