#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* TODO: port this to the d-type struct pattern */
typedef void *mvlcc_t;

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

mvlcc_t mvlcc_make_mvlc_from_crate_config(const char *filename);
mvlcc_t mvlcc_make_mvlc(const char *urlstr);
mvlcc_t mvlcc_make_mvlc_eth(const char *host);
mvlcc_t mvlcc_make_mvlc_usb_from_index(int index);
mvlcc_t mvlcc_make_mvlc_usb_from_serial(const char *serial);
void mvlcc_free_mvlc(mvlcc_t a_mvlc);
int mvlcc_connect(mvlcc_t);
int mvlcc_stop(mvlcc_t);
void mvlcc_disconnect(mvlcc_t);
int mvlcc_single_vme_read(mvlcc_t a_mvlc, uint32_t address, uint32_t * value, uint8_t amod, uint8_t dataWidth);
int mvlcc_single_vme_write(mvlcc_t a_mvlc, uint32_t address, uint32_t value, uint8_t amod, uint8_t dataWidth);
int mvlcc_register_read(mvlcc_t a_mvlc, uint16_t address, uint32_t *value);
int mvlcc_register_write(mvlcc_t a_mvlc, uint16_t address, uint32_t value);
const char *mvlcc_strerror(int errnum);
int mvlcc_is_mvlc_valid(mvlcc_t a_mvlc);
int mvlcc_is_ethernet(mvlcc_t a_mvlc);
int mvlcc_is_usb(mvlcc_t a_mvlc);
int mvlcc_set_daq_mode(mvlcc_t, bool enable);

/* Uses the internal mesytec::mvlc::CrateConfig set when
 * mvlcc_make_mvlc_from_crate_config() was used.
 * See mvlcc_init_readout2() below for a variant taking a
 * CrateConfig object. */
int mvlcc_init_readout(mvlcc_t *a_mvlc);

/* Use mvlcc_readout() instead.
   int mvlcc_readout_eth(mvlcc_t, uint8_t **, size_t);
*/

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

/* (flueke): Returns a pointer to the internal MVLC object. Use from C++ only. */
void *mvlcc_get_mvlc_object(mvlcc_t a_mvlc);

typedef struct
{
  intptr_t d;
} mvlcc_command_t;

/* Returns 0 on success, -1 otherwise. Use mvlcc_command_strerror() to
 * get the last error message.
 * Call mvlcc_command_destroy() on the cmd even if an error occurs!
 */
int mvlcc_command_from_string(mvlcc_command_t *cmdp, const char *str);
void mvlcc_command_destroy(mvlcc_command_t cmd);
const char *mvlcc_command_strerror(mvlcc_command_t cmd);
char *mvlcc_command_to_string(mvlcc_command_t cmd);
uint32_t mvlcc_command_get_vme_address(mvlcc_command_t cmd);
void mvlcc_command_set_vme_address(mvlcc_command_t cmd, uint32_t address);
void mvlcc_command_add_to_vme_address(mvlcc_command_t cmd, uint32_t offset);
int mvlcc_run_command(mvlcc_t a_mvlc, mvlcc_command_t cmd, uint32_t *buffer, size_t size_in, size_t *size_out);

/* Wraps mesytec::mvlc::StackCommandBuilder */
typedef struct
{
  intptr_t d;
} mvlcc_command_list_t;

mvlcc_command_list_t mvlcc_command_list_create();
void mvlcc_command_list_destroy(mvlcc_command_list_t cmd_list);
void mvlcc_command_list_clear(mvlcc_command_list_t cmd_list);
size_t mvlcc_command_list_total_size(mvlcc_command_list_t cmd_list);
size_t mvlcc_command_list_begin_module_group(mvlcc_command_list_t cmd_list, const char *name);
size_t mvlcc_command_list_get_module_group_count(mvlcc_command_list_t cmd_list);
const char *mvlcc_command_list_get_module_group_name(mvlcc_command_list_t cmd_list, size_t index);
int mvlcc_command_list_add_command(mvlcc_command_list_t cmd_list, const char *cmd_str);
const char *mvlcc_command_list_strerror(mvlcc_command_list_t cmd_list);
mvlcc_command_t mvlcc_command_list_get_command(mvlcc_command_list_t cmd_list, size_t index);

/* The returned string must be free()'d by the caller. */
char *mvlcc_command_list_to_yaml(mvlcc_command_list_t cmd_list);
char *mvlcc_command_list_to_json(mvlcc_command_list_t cmd_list);
char *mvlcc_command_list_to_text(mvlcc_command_list_t cmd_list);

/* These return 0 on success, -1 otherwise. Use mvlcc_command_list_strerror() to
 * get the last error message.
 * Call mvlcc_command_list_destroy() on the cmd_list even if an error occurs!
 */
int mvlcc_command_list_from_yaml(mvlcc_command_list_t *cmd_listp, const char *str);
int mvlcc_command_list_from_json(mvlcc_command_list_t *cmd_listp, const char *str);
int mvlcc_command_list_from_text(mvlcc_command_list_t *cmd_listp, const char *str);

/* boolean return value. */
int mvlcc_command_list_eq(mvlcc_command_list_t a, mvlcc_command_list_t b);

typedef struct
{
  intptr_t d;
} mvlcc_crateconfig_t;

mvlcc_crateconfig_t mvlcc_createconfig_create();
void mvlcc_crateconfig_destroy(mvlcc_crateconfig_t crateconfig);

/* The returned string must be free()'d by the caller. */
char *mvlcc_crateconfig_to_yaml(mvlcc_crateconfig_t crateconfig);
char *mvlcc_crateconfig_to_json(mvlcc_crateconfig_t crateconfig);

int mvlcc_crateconfig_from_yaml(mvlcc_crateconfig_t *crateconfigp, const char *str);
int mvlcc_crateconfig_from_json(mvlcc_crateconfig_t *crateconfigp, const char *str);

int mvlcc_crateconfig_from_file(mvlcc_crateconfig_t *crateconfigp, const char *filename);

const char *mvlcc_crateconfig_strerror(mvlcc_crateconfig_t crateconfig);

mvlcc_t mvlcc_make_mvlc_from_crateconfig_t(mvlcc_crateconfig_t crateconfig);

/* Returns a copy of the crateconfigs readout stack. */
mvlcc_command_list_t mvlcc_crateconfig_get_readout_stack(
  mvlcc_crateconfig_t crateconfig, unsigned stackId);

/* Replaces the specified readout stack in the crateconfig with the given
 * cmd_list. cmd_list stays valid and needs to be destroyed by the caller.
 * Returns 0 on success, -1 otherwise.
 */
int mvlcc_crateconfig_set_readout_stack(
  mvlcc_crateconfig_t crateconfig, unsigned stackId, mvlcc_command_list_t cmd_list);

mvlcc_command_list_t mvlcc_crateconfig_get_mcst_daq_start(mvlcc_crateconfig_t crateconfig);
mvlcc_command_list_t mvlcc_crateconfig_get_mcst_daq_stop(mvlcc_crateconfig_t crateconfig);

int mvlcc_init_readout2(mvlcc_t a_mvlc, mvlcc_crateconfig_t crateconfig);

typedef struct
{
  intptr_t d;
} mvlcc_readout_context_t;

mvlcc_readout_context_t mvlcc_readout_context_create();
mvlcc_readout_context_t mvlcc_readout_context_create2(mvlcc_t a_mvlc);
void mvlcc_readout_context_destroy(mvlcc_readout_context_t ctx);
void mvlcc_readout_context_set_mvlc(mvlcc_readout_context_t ctx, mvlcc_t a_mvlc);

int mvlcc_readout(mvlcc_readout_context_t ctx,
  uint8_t *dest, size_t bytes_free, size_t *bytes_used, int timeout_ms);

typedef struct
{
  const uint32_t *data;
  size_t size;
} mvlcc_const_span_t;

typedef struct
{
    mvlcc_const_span_t data_span;
    uint32_t prefix_size;
    uint32_t dynamic_size;
    uint32_t suffix_size;
    int has_dynamic;
} mvlcc_module_data_t;

mvlcc_const_span_t mvlcc_module_data_get_prefix(mvlcc_module_data_t md);
mvlcc_const_span_t mvlcc_module_data_get_dynamic(mvlcc_module_data_t md);
mvlcc_const_span_t mvlcc_module_data_get_suffix(mvlcc_module_data_t md);
int mvlcc_module_data_check_consistency(mvlcc_module_data_t md);

#define MVLCC_DEFINE_EVENT_CALLBACK(name) \
  void name(void *userContext, int crateIndex, int eventIndex,\
    const mvlcc_module_data_t *moduleDataList, unsigned moduleCount)

#define MVLCC_DEFINE_SYSTEM_CALLBACK(name) \
  void name(void *userContext, int crateIndex,\
    mvlcc_const_span_t data)

typedef MVLCC_DEFINE_EVENT_CALLBACK(event_data_callback_t);
typedef MVLCC_DEFINE_SYSTEM_CALLBACK(system_event_callback_t);

typedef struct
{
  intptr_t d;
} mvlcc_readout_parser_t;

int mvlcc_readout_parser_create(
  mvlcc_readout_parser_t *parserp,
  mvlcc_crateconfig_t crateconfig,
  void *userContext,
  event_data_callback_t *event_data_callback,
  system_event_callback_t *system_event_callback);

void mvlcc_readout_parser_destroy(mvlcc_readout_parser_t parser);

typedef int mvlcc_parse_result_t;

const char *mvlcc_parse_result_to_string(mvlcc_parse_result_t result);

mvlcc_parse_result_t mvlcc_readout_parser_parse_buffer(
  mvlcc_readout_parser_t parser,
  size_t linear_buffer_number,
  const uint32_t *buffer,
  size_t size);

#ifdef __cplusplus
}
#endif
