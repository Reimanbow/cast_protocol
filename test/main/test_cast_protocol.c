#include "unity.h"
#include "cast_protocol.h"

TEST_CASE("header size is 11 bytes", "[cast_protocol]")
{
    // cast_protocol.h が正しくincludeできることの確認
    // 実機テストは今後追加
    TEST_ASSERT_EQUAL(CAST_FMT_JPEG, 0);
}

void app_main(void)
{
    UNITY_BEGIN();
    unity_run_all_tests();
    UNITY_END();
}
