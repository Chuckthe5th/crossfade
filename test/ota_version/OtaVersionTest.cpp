#include <gtest/gtest.h>

#include "lib/OtaVersion/OtaVersion.h"

namespace {

TEST(OtaVersionParse, PlainDigits) {
  int major, minor, patch;
  ASSERT_TRUE(ota_version::parse("1.2.3", major, minor, patch));
  EXPECT_EQ(major, 1);
  EXPECT_EQ(minor, 2);
  EXPECT_EQ(patch, 3);
}

TEST(OtaVersionParse, LeadingLowercaseVIsSkipped) {
  int major, minor, patch;
  ASSERT_TRUE(ota_version::parse("v1.2.3", major, minor, patch));
  EXPECT_EQ(major, 1);
  EXPECT_EQ(minor, 2);
  EXPECT_EQ(patch, 3);
}

TEST(OtaVersionParse, LeadingUppercaseVIsSkipped) {
  int major, minor, patch;
  ASSERT_TRUE(ota_version::parse("V2.0.0", major, minor, patch));
  EXPECT_EQ(major, 2);
  EXPECT_EQ(minor, 0);
  EXPECT_EQ(patch, 0);
}

TEST(OtaVersionParse, TrailingSuffixIgnored) {
  int major, minor, patch;
  ASSERT_TRUE(ota_version::parse("1.1.0-rc+abcdef", major, minor, patch));
  EXPECT_EQ(major, 1);
  EXPECT_EQ(minor, 1);
  EXPECT_EQ(patch, 0);
}

TEST(OtaVersionParse, MultiDigitFieldsParseFully) {
  int major, minor, patch;
  ASSERT_TRUE(ota_version::parse("v1.10.0", major, minor, patch));
  EXPECT_EQ(major, 1);
  EXPECT_EQ(minor, 10);
  EXPECT_EQ(patch, 0);
}

TEST(OtaVersionParse, GarbageFailsAndZeroesFields) {
  int major = 42, minor = 42, patch = 42;
  EXPECT_FALSE(ota_version::parse("not-a-version", major, minor, patch));
  EXPECT_EQ(major, 0);
  EXPECT_EQ(minor, 0);
  EXPECT_EQ(patch, 0);
}

TEST(OtaVersionParse, EmptyStringFails) {
  int major, minor, patch;
  EXPECT_FALSE(ota_version::parse("", major, minor, patch));
}

TEST(OtaVersionParse, MissingSegmentFails) {
  int major, minor, patch;
  EXPECT_FALSE(ota_version::parse("v1.2", major, minor, patch));
}

TEST(OtaVersionParse, NullptrFails) {
  int major, minor, patch;
  EXPECT_FALSE(ota_version::parse(nullptr, major, minor, patch));
}

TEST(OtaVersionIsNewer, HigherPatchIsNewer) { EXPECT_TRUE(ota_version::isNewer("1.1.0", "v1.1.1", false)); }

TEST(OtaVersionIsNewer, HigherMinorIsNewer) { EXPECT_TRUE(ota_version::isNewer("1.1.0", "v1.2.0", false)); }

TEST(OtaVersionIsNewer, HigherMajorIsNewer) { EXPECT_TRUE(ota_version::isNewer("1.9.9", "v2.0.0", false)); }

TEST(OtaVersionIsNewer, DoubleDigitMinorComparesNumerically) {
  // The regression this fix targets: v1.10.0 must beat v1.9.0 numerically, not lexicographically
  // (where the strings "1.9.0" > "1.10.0").
  EXPECT_TRUE(ota_version::isNewer("1.9.0", "v1.10.0", false));
  EXPECT_FALSE(ota_version::isNewer("1.10.0", "v1.9.0", false));
}

TEST(OtaVersionIsNewer, EqualVersionIsNotNewer) { EXPECT_FALSE(ota_version::isNewer("1.1.0", "v1.1.0", false)); }

TEST(OtaVersionIsNewer, OlderVersionIsNotNewer) { EXPECT_FALSE(ota_version::isNewer("1.2.0", "v1.1.0", false)); }

TEST(OtaVersionIsNewer, PrereleaseTreatsEqualVersionAsNewer) {
  // An RC build's own embedded version (e.g. "1.1.0-rc+abc123", parsing as 1.1.0) should still be
  // offered the matching real 1.1.0 release once it ships.
  EXPECT_TRUE(ota_version::isNewer("1.1.0-rc+abc123", "v1.1.0", true));
}

TEST(OtaVersionIsNewer, PrereleaseIsNotNewerThanOlderRelease) {
  EXPECT_FALSE(ota_version::isNewer("1.1.0-rc+abc123", "v1.0.0", true));
}

TEST(OtaVersionIsNewer, UnparseableCurrentIsNotNewer) { EXPECT_FALSE(ota_version::isNewer("garbage", "v1.1.0", false)); }

TEST(OtaVersionIsNewer, UnparseableLatestIsNotNewer) { EXPECT_FALSE(ota_version::isNewer("1.1.0", "garbage", false)); }

}  // namespace
