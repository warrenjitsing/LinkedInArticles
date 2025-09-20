#include <gtest/gtest.h>

int main(int argc, char **argv) {
    // Initializes Google Test
    ::testing::InitGoogleTest(&argc, argv);

    // Runs all tests and returns the result
    return RUN_ALL_TESTS();
}
