#include <mvlcc_wrap.h>

#include "minunit.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

MU_TEST(test_mvlcc_command_t_good)
{
    mvlcc_command_t cmd = {};
    int res = mvlcc_command_from_string(&cmd, "vme_read 0x09 d16 0x12345678");
    mu_assert_int_eq(0, res);
    mu_check(strlen(mvlcc_command_strerror(cmd)) == 0);

    char *str = mvlcc_command_to_string(cmd);
    mu_assert_string_eq("vme_read 0x09 d16 0x12345678", str);
    free(str);

    uint32_t vmeAddress = mvlcc_command_get_vme_address(cmd);
    mu_assert_uint_eq(0x12345678u, vmeAddress);

    mvlcc_command_set_vme_address(cmd, 0x87654321);
    vmeAddress = mvlcc_command_get_vme_address(cmd);
    mu_assert_uint_eq(0x87654321u, vmeAddress);

    mvlcc_command_add_to_vme_address(cmd, 0x100);
    vmeAddress = mvlcc_command_get_vme_address(cmd);
    mu_assert_uint_eq(0x87654421u, vmeAddress);

    mvlcc_command_destroy(cmd);
}

MU_TEST(test_mvlcc_command_t_bad)
{
    mvlcc_command_t cmd;
    int res = mvlcc_command_from_string(&cmd, "foobar 0x09 d16 0x12345678");
    mu_check(res != 0);
    mu_check(strlen(mvlcc_command_strerror(cmd)) > 0);
    // Still have to destroy it! The error string buffer is kept in there.
    mvlcc_command_destroy(cmd);
}

void test_mvlcc_command_list_t()
{
    mvlcc_command_list_t cmdList = mvlcc_command_list_create();
    mu_check(cmdList.d != 0);
    mu_assert_int_eq(0, mvlcc_command_list_total_size(cmdList));

    mu_assert_uint_eq(0, mvlcc_command_list_begin_module_group(cmdList, "my_module0"));
    const char *groupName = mvlcc_command_list_get_module_group_name(cmdList, 0);
    mu_assert_string_eq("my_module0", groupName);
    mu_assert_uint_eq(1, mvlcc_command_list_get_module_group_count(cmdList));
    mu_assert_int_eq(0, mvlcc_command_list_total_size(cmdList));

    int res = mvlcc_command_list_add_command(cmdList, "vme_read 0x09 d16 0x12345678");
    mu_assert_int_eq(0, res);
    mu_assert_int_eq(1, mvlcc_command_list_total_size(cmdList));
    mu_check(strlen(mvlcc_command_list_strerror(cmdList)) == 0);

    res = mvlcc_command_list_add_command(cmdList, "foobar 0x09 d16 0x12345678");
    mu_check(res != 0);
    mu_assert_int_eq(1, mvlcc_command_list_total_size(cmdList));
    mu_check(strlen(mvlcc_command_list_strerror(cmdList)) > 0);

    char *yaml = mvlcc_command_list_to_yaml(cmdList);
    char *json = mvlcc_command_list_to_json(cmdList);
    char *text = mvlcc_command_list_to_text(cmdList);

    mu_check(strlen(yaml) > 0);
    mu_check(strlen(json) > 0);
    mu_check(strlen(text) > 0);

    //fprintf(stderr, "YAML:\n%s\n", yaml);
    //fprintf(stderr, "JSON:\n%s\n", json);
    //fprintf(stderr, "TEXT:\n%s\n", text);

    free(yaml);
    free(json);
    free(text);

    mvlcc_command_list_destroy(cmdList);
}

void test_mvlcc_command_list_t_text()
{
    mvlcc_command_list_t cmdList;

    int res = mvlcc_command_list_from_text(&cmdList, "vme_read 0x09 d16 0x12345678\nvme_read 0x0a d32 0x87654321");
    mu_assert_int_eq(0, res);
    mu_check(strlen(mvlcc_command_list_strerror(cmdList)) == 0);
    mu_assert_int_eq(1, mvlcc_command_list_get_module_group_count(cmdList));
    mu_assert_uint_eq(2, mvlcc_command_list_total_size(cmdList));
    mvlcc_command_list_destroy(cmdList);

    res = mvlcc_command_list_from_text(&cmdList, "foobar 0x09 d16 0x12345678\nvme_read 0x0a d32 0x87654321");
    mu_check(res != 0);
    mu_check(strlen(mvlcc_command_list_strerror(cmdList)) > 0);
    mu_assert_int_eq(0, mvlcc_command_list_get_module_group_count(cmdList));
    mu_assert_uint_eq(0, mvlcc_command_list_total_size(cmdList));
    // Still have to destroy it! The error string buffer is kept in there.
    mvlcc_command_list_destroy(cmdList);
}

void test_mvlcc_command_list_t_yaml()
{
    static const char *yaml =
    "name: \"\"\n"
    "groups:\n"
    "  - name: my_module0\n"
    "    contents:\n"
    "      - vme_read 0x09 d16 0x12345678\n"
    "      - vme_read 0x0a d32 0x87654321\n"
    ;

    mvlcc_command_list_t cmdList;
    int res = mvlcc_command_list_from_yaml(&cmdList, yaml);
    mu_check(res == 0);
    mu_check(strlen(mvlcc_command_list_strerror(cmdList)) == 0);
    mu_assert_int_eq(1, mvlcc_command_list_get_module_group_count(cmdList));
    mu_assert_uint_eq(2, mvlcc_command_list_total_size(cmdList));
    mvlcc_command_list_destroy(cmdList);
}

void test_mvlcc_command_list_t_json()
{
    static const char *json =
    "{\n"
    " \"groups\": [\n"
    "  {\n"
    "   \"contents\": [\n"
    "    \"vme_read 0x09 d16 0x12345678\",\n"
    "    \"vme_read 0x0a d32 0x87654321\"\n"
    "   ],\n"
    "   \"name\": \"my_module0\"\n"
    "  }\n"
    " ],\n"
    " \"name\": \"\"\n"
    "}\n"
    ;

    mvlcc_command_list_t cmdList;
    mvlcc_command_t cmd;
    int res = mvlcc_command_list_from_json(&cmdList, json);
    mu_check(res == 0);
    mu_check(strlen(mvlcc_command_list_strerror(cmdList)) == 0);
    mu_assert_int_eq(1, mvlcc_command_list_get_module_group_count(cmdList));
    mu_assert_uint_eq(2, mvlcc_command_list_total_size(cmdList));
    mvlcc_command_list_destroy(cmdList);
}

void test_mvlcc_crateconfig_t()
{
    mvlcc_crateconfig_t crateConfig = mvlcc_createconfig_create();
    mu_check(crateConfig.d != 0);

    mvlcc_command_list_t cmdList1;
    int res = mvlcc_command_list_from_text(&cmdList1, "vme_read 0x09 d16 0x12345678\nvme_read 0x0a d32 0x87654321");
    mu_assert_int_eq(0, res);

    res = mvlcc_crateconfig_set_readout_stack(crateConfig, 2, cmdList1);
    mu_assert_int_eq(0, res);

    mvlcc_command_list_t cmdList2 = mvlcc_crateconfig_get_readout_stack(crateConfig, 2);

    mu_check(mvlcc_command_list_eq(cmdList1, cmdList2));

    mvlcc_command_list_destroy(cmdList1);
    mvlcc_command_list_destroy(cmdList2);
    mvlcc_crateconfig_destroy(crateConfig);
}

void test_mvlcc_module_data_t()
{
    mvlcc_module_data_t md;
    md.data_span.data = (const uint32_t *)0xaabbccddu;
    md.data_span.size = 500;
    md.prefix_size = 100;
    md.dynamic_size = 200;
    md.suffix_size = 300;
    md.has_dynamic = 1;

    mvlcc_const_span_t prefix = mvlcc_module_data_get_prefix(md);
    mu_check(prefix.data == md.data_span.data);
    mu_check(prefix.size == md.prefix_size);
}

MU_TEST_SUITE(test_mvlcc_wrap)
{
    MU_RUN_TEST(test_mvlcc_command_t_good);
    MU_RUN_TEST(test_mvlcc_command_t_bad);
    MU_RUN_TEST(test_mvlcc_command_list_t);
    MU_RUN_TEST(test_mvlcc_command_list_t_text);
    MU_RUN_TEST(test_mvlcc_command_list_t_yaml);
    MU_RUN_TEST(test_mvlcc_command_list_t_json);
    MU_RUN_TEST(test_mvlcc_crateconfig_t);
    MU_RUN_TEST(test_mvlcc_module_data_t);
}

int main()
{
    MU_RUN_SUITE(test_mvlcc_wrap);
    MU_REPORT();
    return MU_EXIT_CODE;
}
