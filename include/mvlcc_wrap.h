#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h> /* for FILE */

typedef void *mvlcc_t;

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  mvlcc_A16 = 0x29,
  mvlcc_A24 = 0x39,
  mvlcc_A32 = 0x09,
  mvlcc_A_ERR = -1
} mvlcc_addr_width_t;

typedef enum {
  mvlcc_D16 = 0x1,
  mvlcc_D32 = 0x2,
  mvlcc_D_ERR = -1
} mvlcc_data_width_t;

mvlcc_t mvlcc_make_mvlc_from_crate_config(const char *);
mvlcc_t mvlcc_make_mvlc(const char *urlstr);
mvlcc_t mvlcc_make_mvlc_eth(const char *host);
mvlcc_t mvlcc_make_mvlc_usb_from_index(int index);
mvlcc_t mvlcc_make_mvlc_usb_from_serial(const char *serial);
void mvlcc_free_mvlc(mvlcc_t a_mvlc);
int mvlcc_connect(mvlcc_t);
int mvlcc_stop(mvlcc_t);
void mvlcc_disconnect(mvlcc_t);
int mvlcc_init_readout(mvlcc_t);
int mvlcc_readout_eth(mvlcc_t, uint8_t **, size_t);
int mvlcc_single_vme_read(mvlcc_t a_mvlc, uint32_t address, uint32_t * value, uint8_t amod, uint8_t dataWidth);
int mvlcc_single_vme_write(mvlcc_t a_mvlc, uint32_t address, uint32_t value, uint8_t amod, uint8_t dataWidth);
int mvlcc_register_read(mvlcc_t a_mvlc, uint16_t address, uint32_t *value);
int mvlcc_register_write(mvlcc_t a_mvlc, uint16_t address, uint32_t value);
const char *mvlcc_strerror(int errnum);
int mvlcc_is_mvlc_valid(mvlcc_t a_mvlc);
int mvlcc_is_ethernet(mvlcc_t a_mvlc);
int mvlcc_is_usb(mvlcc_t a_mvlc);

struct MvlccBlockReadParams
{
    uint8_t amod; /* amod, must be a valid VME block amod */
    int fifo;  /* if true the read address is not incremented */
    int swap;  /* if true swaps the two 32-bit words for 64-bit MBLT reads */
};

/* Directly executed block read. Uses MVLCs command pipe which has a smaller
 * buffer than the readout pipe. Not recommended to be used for real DAQs!
 * Writes the raw blockread contents, stripped of any MVLC framing, into buffer.
 *
 * Returns 0 on success, non-zero otherwise.
 * The number of words copied into the output buffer is returned in sizeOut.
 * sizeIn and sizeOut in units of 32-bit words.
 */
int mvlcc_vme_block_read(mvlcc_t a_mvlc, uint32_t address, uint32_t *buffer, size_t sizeIn,
  size_t *sizeOut, struct MvlccBlockReadParams params);

/* spdlog level names: error, warn, info, debug, trace */
void mvlcc_set_global_log_level(const char *levelName);

/* Prints stack transaction retry counters and some more stats related to direct
 * cmd execution. */
void mvlcc_print_mvlc_cmd_counters(FILE *out, mvlcc_t a_mvlc);

/* (flueke): Returns a pointer to the internal MVLC object. Used in the daq1 hack. */
void *mvlcc_get_mvlc_object(mvlcc_t a_mvlc);

#ifdef __cplusplus
}
#endif
