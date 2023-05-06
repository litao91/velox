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

#include "velox/dwio/common/tests/utils/DataFiles.h"
#include <boost/algorithm/string.hpp>
#include "velox/common/base/Fs.h"

namespace facebook::velox::test {

std::string getDataFilePath(
    const std::string& baseDir,
    const std::string& filePath) {
  std::string current_path = fs::current_path().c_str();
  if (boost::algorithm::ends_with(current_path, "fbcode")) {
    return current_path + "/" + baseDir + "/" + filePath;
  }
  return current_path + "/" + filePath;
}

} // namespace facebook::velox::test
