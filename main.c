#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include "connectivity/conn_http.h"
#include "http_parser.h"
#include "utils/crypto_utils.h"
#include "utils/serializer.h"
#include "utils/tryte_byte_conv.h"
#include "utils/uart_utils.h"

#define HOST "tangle-accel.puyuma.org"
#define PORT "443"
#define API "transaction/"
#define SSL_SEED "nonce"
#define REQ_BODY                                                           \
  "{\"value\": 0, \"tag\": \"POWEREDBYTANGLEACCELERATOR9\", \"message\": " \
  "\"%s\", \"address\":\"%s\"}\r\n\r\n"
#define ADDRESS                                                                \
  "POWEREDBYTANGLEACCELERATOR999999999999999999999999999999999999999999999999" \
  "999999A"
#define ADDR_LEN 81

#ifndef DEBUG
#define MSG "%s:%s"
#else
#define MSG "%s:THISISMSG9THISISMSG9THISISMSG"
#define ADDR_LOG_PATH "addr_log.log"
#endif

static char addr_log_template[] = "\n%s\n";

// Test mode only, should not use on deployment
void gen_trytes(uint16_t len, char *out) {
  const char tryte_alphabet[] = "9ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  uint8_t rand_index;
  for (int i = 0; i < len; i++) {
    rand_index = rand() % 27;
    out[i] = tryte_alphabet[rand_index];
  }
}

int send_https_msg(char const *const host, char const *const port, char const *const api, char const *const tryte_msg,
                   char const *const addr) {
  char req_body[1024] = {}, res[4096] = {0};
  char *req = NULL;
  int ret = 0;
  sprintf(req_body, REQ_BODY, tryte_msg, addr);
  set_post_request(api, host, atoi(port), req_body, &req);

#ifdef DEBUG
  printf("req packet = \n%s", req);
#endif

  http_parser_settings settings;
  settings.on_body = parser_body_callback;
  http_parser *parser = malloc(sizeof(http_parser));

  connect_info_t info = {.https = true};
  http_open(&info, SSL_SEED, host, port);
  http_send_request(&info, req);
  http_read_response(&info, res, sizeof(res) / sizeof(char));
  http_close(&info);
  http_parser_init(parser, HTTP_RESPONSE);
  http_parser_execute(parser, &settings, res, strlen(res));
  printf("HTTP Response: %s\n", http_res_body);
  free(http_res_body);
  http_res_body = NULL;

  ret = parser->status_code;
  free(parser);
  free(req);
  return ret;
}

int log_address(char *next_addr) {
  FILE *fp;
  char addr_log[ADDR_LEN + 3];
  // Append the next address to the address log file
  fp = fopen(ADDR_LOG_PATH, "a");
  if (!fp) {
    perror("open addr_log.log failed:");
    fclose(fp);
    return -1;
  }
  snprintf(addr_log, 83, addr_log_template, next_addr);
  fputs(addr_log, fp);
  fclose(fp);
  return 0;
}

int main(int argc, char *argv[]) {
  uint8_t ciphertext[1024] = {0}, iv[16] = {0}, raw_msg[1000] = {0};
  uint32_t raw_msg_len = 1 + ADDR_LEN + 20, ciphertext_len = 0, msg_len;
  char tryte_msg[1024] = {0}, msg[1024] = {0}, addr[ADDR_LEN + 1] = ADDRESS, next_addr[ADDR_LEN + 1] = {0};
  srand(time(NULL));

#ifndef DEBUG
  int fd = uart_init();
  if (fd < 0) {
    printf("Error in initializing UART\n");
    return -1;
  }
#else
  if (log_address(next_addr) != 0) {
    fprintf(stderr, "log address failed\n");
    return -1;
  }
#endif

  char *response = NULL;
  time_t timer;
  char time_str[26];
  struct tm *tm_info;

#ifndef DEBUG
  fd_set rset;
  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = 500;
  while (true) {
    // TODO add select
    FD_ZERO(&rset);
    FD_SET(fd, &rset);
    select(fd + 1, &rset, NULL, NULL, &tv);

    if (FD_ISSET(fd, &rset)) {
#endif
      time(&timer);
      tm_info = localtime(&timer);
      strftime(time_str, 26, "%Y-%m-%d %H:%M:%S", tm_info);
      printf("%s\n", time_str);
      // TODO: not good
      gen_trytes(ADDR_LEN, next_addr);

#ifndef DEBUG
      response = uart_read(fd);
#else
  response = strdup("This is a test");
  printf("next_addr = %s \n", next_addr);
  log_address(next_addr);
#endif
      // real transmitted data
#ifndef DEBUG
      snprintf((char *)raw_msg, raw_msg_len, MSG, next_addr, response);
#else
  snprintf((char *)raw_msg, raw_msg_len, MSG, next_addr);
#endif
      printf("Raw Message: %s\n", raw_msg);
      uint8_t private_key[AES_BLOCK_SIZE * 2] = {0};
      uint8_t id[IMSI_LEN + 1] = {0};
#ifndef DEBUG
      if (get_aes_key(private_key) != 0) {
        fprintf(stderr, "%s\n", "get aes key error");
        return -1;
      }
      // fetch Device_ID (IMSI, len <= 15)
      if (get_device_id(id) != 0) {
        fprintf(stderr, "%s\n", "get device id error");
        return -1;
      }
#else
  /* Setting data to produce predictable results during debugging */
  // init vector for aes
  const uint8_t iv_global[16] = {164, 3, 98, 193, 52, 162, 107, 252, 184, 42, 74, 225, 157, 26, 88, 72};
  // device id
  const char *device_id = "470010171566423";
  // private key
  const uint8_t key[32] = {82,  142, 184, 64,  74, 105, 126, 65,  154, 116, 14,  193, 208, 41,  8,  115,
                           158, 252, 228, 160, 79, 5,   167, 185, 13,  159, 135, 113, 49,  209, 58, 68};

  memcpy(id, device_id, 16);
  memcpy(private_key, key, 16);
  memcpy(iv, iv_global, 16);
#endif
      ciphertext_len = encrypt(raw_msg, strlen((char *)raw_msg), ciphertext, 1024, iv, private_key, id);
      if (ciphertext_len == 0) {
        fprintf(stderr, "%s\n", "encrypt msg error");
        return -1;
      }
      serialize_msg(iv, ciphertext_len, ciphertext, msg, &msg_len);
      bytes_to_trytes((const unsigned char *)msg, msg_len, tryte_msg);

      // Init http session. verify: check the server CA cert.
      if (send_https_msg(HOST, PORT, API, tryte_msg, addr) != HTTP_OK) {
        fprintf(stderr, "Response from ta server failed");
      }

      strncpy(addr, next_addr, ADDR_LEN);
      free(response);
      response = NULL;
      printf(
          "========================Finishing Sending "
          "Transaction========================\n\n");
#ifndef DEBUG
    }
    if (tcflush(fd, TCIOFLUSH) != 0) {
      perror("tcflush error");
    }
  }
#endif

  return 0;
}
