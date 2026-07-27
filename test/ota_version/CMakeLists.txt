add_executable(OtaVersionTest
  OtaVersionTest.cpp
  ${REPO_ROOT}/lib/OtaVersion/OtaVersion.cpp
)

target_link_libraries(OtaVersionTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(OtaVersionTest)
