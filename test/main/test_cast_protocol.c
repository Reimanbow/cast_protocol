#include "unity.h"
#include "cast_protocol.h"

TEST_CASE("init and deinit", "[cast_protocol]")
{
    TEST_ASSERT_EQUAL(ESP_OK, cast_protocol_init());
    TEST_ASSERT_EQUAL(ESP_OK, cast_protocol_deinit());
}

// TODO: Add more test cases here
// TEST_CASE("test description", "[cast_protocol]")
// {
//     TEST_ASSERT_TRUE(condition);
// }

void app_main(void)
{
    UNITY_BEGIN();
    unity_run_all_tests();
    UNITY_END();
}
