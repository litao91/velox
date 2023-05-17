/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "velox/dwio/common/BufferedInput.h"

using namespace facebook::velox::dwio::common;
using namespace ::testing;

namespace {

class ReadFileMock : public ::facebook::velox::ReadFile {
 public:
  virtual ~ReadFileMock() override = default;

  MOCK_METHOD(
      std::string_view,
      pread,
      (uint64_t offset, uint64_t length, void* FOLLY_NONNULL buf),
      (const, override));

  MOCK_METHOD(bool, shouldCoalesce, (), (const, override));
  MOCK_METHOD(uint64_t, size, (), (const, override));
  MOCK_METHOD(uint64_t, memoryUsage, (), (const, override));
  MOCK_METHOD(std::string, getName, (), (const, override));
  MOCK_METHOD(uint64_t, getNaturalReadSize, (), (const, override));
  MOCK_METHOD(
      void,
      preadv,
      (const std::vector<Segment>& segments),
      (const, override));
};

} // namespace

TEST(TestBufferedInput, ZeroLengthStream) {
  auto readFile =
      std::make_shared<facebook::velox::InMemoryReadFile>(std::string());
  auto pool = facebook::velox::memory::addDefaultLeafMemoryPool();
  BufferedInput input(readFile, *pool);
  auto ret = input.enqueue({0, 0});
  EXPECT_NE(ret, nullptr);
  const void* buf = nullptr;
  int32_t size = 1;
  EXPECT_FALSE(ret->Next(&buf, &size));
  EXPECT_EQ(size, 0);
}

TEST(TestBufferedInput, UseRead) {
  std::string content = "hello";
  auto readFileMock = std::make_shared<ReadFileMock>();
  EXPECT_CALL(*readFileMock, getName()).WillRepeatedly(Return("mock_name"));
  EXPECT_CALL(*readFileMock, size()).WillRepeatedly(Return(content.size()));
  EXPECT_CALL(*readFileMock, pread(0, 5, _))
      .Times(1)
      .WillOnce(
          [&](uint64_t offset, uint64_t length, void* buf) -> std::string_view {
            memcpy(buf, content.data() + offset, length);
            return content;
          });
  auto pool = facebook::velox::memory::addDefaultLeafMemoryPool();
  // Use read: by default
  BufferedInput input(readFileMock, *pool);
  auto ret = input.enqueue({0, 5});
  ASSERT_NE(ret, nullptr);
  input.load(LogType::TEST);
  const void* buf = nullptr;
  int32_t size;
  EXPECT_TRUE(ret->Next(&buf, &size));
  EXPECT_EQ(size, 5);
  EXPECT_EQ(std::string(static_cast<const char*>(buf), size), content);
}

TEST(TestBufferedInput, UseVRead) {
  std::string content = "hello";
  auto readFileMock = std::make_shared<ReadFileMock>();
  EXPECT_CALL(*readFileMock, getName()).WillRepeatedly(Return("mock_name"));
  EXPECT_CALL(*readFileMock, size()).WillRepeatedly(Return(content.size()));
  EXPECT_CALL(*readFileMock, preadv(_))
      .Times(1)
      .WillOnce([&](const std::vector<::facebook::velox::ReadFile::Segment>&
                        segments) {
        ASSERT_EQ(segments.size(), 1);
        ASSERT_LE(
            segments[0].offset + segments[0].buffer.size(), content.size());
        memcpy(
            segments[0].buffer.data(),
            content.data() + segments[0].offset,
            segments[0].buffer.size());
      });
  auto pool = facebook::velox::memory::addDefaultLeafMemoryPool();
  // Use vread
  BufferedInput input(
      readFileMock,
      *pool,
      MetricsLog::voidLog(),
      nullptr,
      10,
      /* wsVRLoad = */ true);
  auto ret = input.enqueue({0, 5});
  ASSERT_NE(ret, nullptr);
  input.load(LogType::TEST);
  const void* buf = nullptr;
  int32_t size;
  EXPECT_TRUE(ret->Next(&buf, &size));
  EXPECT_EQ(size, 5);
  EXPECT_EQ(std::string(static_cast<const char*>(buf), size), content);
}

TEST(TestBufferedInput, WillMerge) {
  std::string content = "hello world";
  auto readFileMock = std::make_shared<ReadFileMock>();
  EXPECT_CALL(*readFileMock, getName()).WillRepeatedly(Return("mock_name"));
  EXPECT_CALL(*readFileMock, size()).WillRepeatedly(Return(content.size()));
  // Will merge because the distance is 1 and max distance to merge is 10.
  // Expect only one call.
  EXPECT_CALL(*readFileMock, pread(0, 11, _))
      .Times(1)
      .WillOnce(
          [&](uint64_t offset, uint64_t length, void* buf) -> std::string_view {
            memcpy(buf, content.data() + offset, length);
            return {content.data() + offset, length};
          });
  auto pool = facebook::velox::memory::addDefaultLeafMemoryPool();
  BufferedInput input(
      readFileMock,
      *pool,
      MetricsLog::voidLog(),
      nullptr,
      10, // Will merge if distance <= 10
      /* wsVRLoad = */ false);

  auto ret1 = input.enqueue({0, 5});
  auto ret2 = input.enqueue({6, 5});
  ASSERT_NE(ret1, nullptr);
  ASSERT_NE(ret2, nullptr);
  input.load(LogType::TEST);
  const void* buf = nullptr;
  int32_t size;

  EXPECT_TRUE(ret1->Next(&buf, &size));
  EXPECT_EQ(size, 5);
  EXPECT_EQ(std::string(static_cast<const char*>(buf), size), "hello");

  EXPECT_TRUE(ret2->Next(&buf, &size));
  EXPECT_EQ(size, 5);
  EXPECT_EQ(std::string(static_cast<const char*>(buf), size), "world");
}

TEST(TestBufferedInput, WontMerge) {
  std::string content = "hello  world"; // two spaces
  auto readFileMock = std::make_shared<ReadFileMock>();
  EXPECT_CALL(*readFileMock, getName()).WillRepeatedly(Return("mock_name"));
  EXPECT_CALL(*readFileMock, size()).WillRepeatedly(Return(content.size()));

  // Won't merge because the distance is 2 and max distance to merge is 1.
  // Expect two calls
  EXPECT_CALL(*readFileMock, pread(0, 5, _))
      .Times(1)
      .WillOnce(
          [&](uint64_t offset, uint64_t length, void* buf) -> std::string_view {
            memcpy(buf, content.data() + offset, length);
            return {content.data() + offset, length};
          });

  EXPECT_CALL(*readFileMock, pread(7, 5, _))
      .Times(1)
      .WillOnce(
          [&](uint64_t offset, uint64_t length, void* buf) -> std::string_view {
            memcpy(buf, content.data() + offset, length);
            return {content.data() + offset, length};
          });

  auto pool = facebook::velox::memory::addDefaultLeafMemoryPool();
  BufferedInput input(
      readFileMock,
      *pool,
      MetricsLog::voidLog(),
      nullptr,
      1, // Will merge if distance <= 1
      /* wsVRLoad = */ false);

  auto ret1 = input.enqueue({0, 5});
  auto ret2 = input.enqueue({7, 5});
  ASSERT_NE(ret1, nullptr);
  ASSERT_NE(ret2, nullptr);
  input.load(LogType::TEST);
  const void* buf = nullptr;
  int32_t size;

  EXPECT_TRUE(ret1->Next(&buf, &size));
  EXPECT_EQ(size, 5);
  EXPECT_EQ(std::string(static_cast<const char*>(buf), size), "hello");

  EXPECT_TRUE(ret2->Next(&buf, &size));
  EXPECT_EQ(size, 5);
  EXPECT_EQ(std::string(static_cast<const char*>(buf), size), "world");
}
