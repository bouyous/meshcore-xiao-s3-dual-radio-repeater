#include <gtest/gtest.h>

#include <helpers/AlertRecipientList.h>

namespace {

constexpr char KEY_A[] =
    "de9b4a1bedf5c3022f6dd65d7ecd56a5b2ee1b127922b6d6aeba20985777f12a";
constexpr char KEY_B[] =
    "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff";
constexpr char KEY_C[] =
    "102132435465768798a9bacbdcedfe0f102132435465768798a9bacbdcedfe0f";

TEST(AlertRecipientList, AcceptsAndRoundTripsPublicKey) {
  mesh::AlertRecipientList<4> recipients;
  ASSERT_TRUE(recipients.addHex(KEY_A));
  ASSERT_EQ(recipients.count(), 1U);

  char encoded[65];
  mesh::AlertRecipientList<4>::encodeHexKey(recipients.keyAt(0), encoded);
  EXPECT_STREQ(encoded, KEY_A);
}

TEST(AlertRecipientList, AcceptsUppercaseAndRejectsMalformedInput) {
  mesh::AlertRecipientList<4> recipients;
  EXPECT_TRUE(recipients.addHex(
      "00112233445566778899AABBCCDDEEFF00112233445566778899AABBCCDDEEFF"));
  EXPECT_FALSE(recipients.addHex("0011"));
  EXPECT_FALSE(recipients.addHex(
      "g0112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"));
}

TEST(AlertRecipientList, RejectsDuplicatesAndCapacityOverflow) {
  mesh::AlertRecipientList<2> recipients;
  EXPECT_TRUE(recipients.addHex(KEY_A));
  EXPECT_FALSE(recipients.addHex(KEY_A));
  EXPECT_TRUE(recipients.addHex(KEY_B));
  EXPECT_FALSE(recipients.addHex(KEY_C));
  EXPECT_EQ(recipients.count(), 2U);
}

TEST(AlertRecipientList, RemovesByIndexAndKeyWithoutLeavingHoles) {
  mesh::AlertRecipientList<4> recipients;
  ASSERT_TRUE(recipients.addHex(KEY_A));
  ASSERT_TRUE(recipients.addHex(KEY_B));
  ASSERT_TRUE(recipients.addHex(KEY_C));

  EXPECT_TRUE(recipients.removeAt(1));
  EXPECT_EQ(recipients.count(), 2U);
  char encoded[65];
  mesh::AlertRecipientList<4>::encodeHexKey(recipients.keyAt(1), encoded);
  EXPECT_STREQ(encoded, KEY_C);
  EXPECT_TRUE(recipients.removeHex(KEY_A));
  EXPECT_EQ(recipients.count(), 1U);
  EXPECT_FALSE(recipients.removeAt(1));
}

TEST(AlertRecipientList, ClearRemovesEveryRecipient) {
  mesh::AlertRecipientList<4> recipients;
  ASSERT_TRUE(recipients.addHex(KEY_A));
  recipients.clear();
  EXPECT_EQ(recipients.count(), 0U);
  EXPECT_EQ(recipients.keyAt(0), nullptr);
}

}  // namespace
