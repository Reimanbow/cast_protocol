#include <gtest/gtest.h>

extern "C" {
#include "cast_protocol.h"
}

TEST(CastProtocol, InitReturnsOk)
{
    EXPECT_EQ(ESP_OK, cast_protocol_init());
}

TEST(CastProtocol, DeinitReturnsOk)
{
    EXPECT_EQ(ESP_OK, cast_protocol_deinit());
}
