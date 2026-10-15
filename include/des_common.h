#ifndef DES_COMMON_H
#define DES_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <openssl/evp.h>
#include <openssl/provider.h>

#define DES_KEY_LIMIT (UINT64_C(1) << 56)
#define DES_BLOCK_SIZE 8u
#define DES_ERROR_SIZE 256u
#define DES_MAX_FILE_SIZE ((size_t)INT_MAX - ((size_t)INT_MAX % DES_BLOCK_SIZE))
#define DES_MAX_PLAINTEXT_SIZE (DES_MAX_FILE_SIZE - 1u)

typedef enum {
    DES_FORMAT_PKCS7 = 0,
    DES_FORMAT_COURSE = 1
} des_format;

typedef struct {
    OSSL_PROVIDER *legacy;
    EVP_CIPHER *cipher;
} des_crypto;

typedef struct {
    const des_crypto *crypto;
    EVP_CIPHER_CTX *ctx;
    unsigned char *plaintext;
    size_t capacity;
    des_format format;
} des_candidate;

int des_crypto_init(des_crypto *crypto, char *error, size_t error_size);
void des_crypto_cleanup(des_crypto *crypto);
int des_parse_index(const char *text, uint64_t maximum, uint64_t *value);
int des_read_key_file(const char *path, uint64_t *value,
                      char *error, size_t error_size);
int des_read_file(const char *path, unsigned char **data, size_t *length,
                  char *error, size_t error_size);
int des_write_file(const char *path, const unsigned char *data, size_t length,
                   char *error, size_t error_size);
void des_index_to_key(uint64_t index, des_format format,
                      unsigned char key[DES_BLOCK_SIZE]);
int des_encrypt_buffer(const des_crypto *crypto, uint64_t index, des_format format,
                       const unsigned char *input, size_t input_length,
                       unsigned char **output, size_t *output_length,
                       char *error, size_t error_size);
int des_decrypt_buffer(const des_crypto *crypto, uint64_t index, des_format format,
                       const unsigned char *input, size_t input_length,
                       unsigned char **output, size_t *output_length,
                       char *error, size_t error_size);
int des_candidate_init(des_candidate *candidate, const des_crypto *crypto,
                       size_t ciphertext_length, des_format format,
                       char *error, size_t error_size);
void des_candidate_cleanup(des_candidate *candidate);
/* Returns 1 for a matching plaintext; 0 for a rejected key;
 * -1 for an operational error. The ciphertext is never modified. */
int des_candidate_try(des_candidate *candidate, uint64_t index,
                      const unsigned char *ciphertext, size_t ciphertext_length,
                      const unsigned char *keyword, size_t keyword_length,
                      size_t *plaintext_length, char *error, size_t error_size);
int des_contains_bytes(const unsigned char *data, size_t length,
                       const unsigned char *keyword, size_t keyword_length);
double des_monotonic_seconds(void);

#endif
