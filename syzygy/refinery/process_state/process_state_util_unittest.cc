// Copyright 2015 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "syzygy/refinery/process_state/process_state_util.h"

#include <vector>

#include "base/strings/utf_string_conversions.h"
#include "gtest/gtest.h"
#include "syzygy/refinery/core/address.h"
#include "syzygy/refinery/process_state/process_state.h"
#include "syzygy/refinery/process_state/refinery.pb.h"

namespace refinery {

namespace {

const Address kAddress = 0x0000CAFE;  // Fits 32-bit.
const Size kSize = 42U;
const uint32 kChecksum = 11U;
const uint32 kTimestamp = 22U;
const wchar_t kPath[] = L"c:\\path\\ModuleName";
const char kDataName[] = "data_name";
const char kTypeName[] = "Type::Name*";

}  // namespace

TEST(ModuleLayerAccessorTest, AddModuleRecord) {
  ProcessState state;
  ModuleLayerAccessor accessor(&state);
  accessor.AddModuleRecord(AddressRange(kAddress, kSize), kChecksum, kTimestamp,
                           kPath);

  // Validate a record was added.
  ModuleLayerPtr module_layer;
  ASSERT_TRUE(state.FindLayer(&module_layer));
  std::vector<ModuleRecordPtr> matching_records;
  module_layer->GetRecordsAt(kAddress, &matching_records);
  ASSERT_EQ(1, matching_records.size());

  // Validate the record.
  ModuleRecordPtr record = matching_records[0];
  ASSERT_EQ(AddressRange(kAddress, kSize), record->range());
  const Module& module = matching_records[0]->data();
  ASSERT_NE(kNoModuleId, module.module_id());

  // Validate the layer data contains the module information.
  pe::PEFile::Signature signature;
  ASSERT_TRUE(module_layer->data().Find(module.module_id(), &signature));
  ASSERT_EQ(kPath, signature.path);
  ASSERT_EQ(0U, signature.base_address.value());
  ASSERT_EQ(kSize, signature.module_size);
  ASSERT_EQ(kChecksum, signature.module_checksum);
  ASSERT_EQ(kTimestamp, signature.module_time_date_stamp);

  ASSERT_EQ(module.module_id(), module_layer->data().Find(signature));
}

TEST(ModuleLayerAccessorTest, GetModuleSignatureTest) {
  ProcessState state;
  ModuleLayerAccessor accessor(&state);
  pe::PEFile::Signature signature;

  // Fails when VA doesn't correspond to a module.
  ASSERT_FALSE(accessor.GetModuleSignature(kAddress, &signature));

  // Add a module.
  accessor.AddModuleRecord(AddressRange(kAddress, kSize), kChecksum, kTimestamp,
                           kPath);

  // Fails outside the module's range.
  ASSERT_FALSE(accessor.GetModuleSignature(kAddress - 1, &signature));
  ASSERT_FALSE(accessor.GetModuleSignature(kAddress + kSize, &signature));

  // Succeeds within the module's range.
  ASSERT_TRUE(accessor.GetModuleSignature(kAddress, &signature));
  ASSERT_TRUE(accessor.GetModuleSignature(kAddress + kSize - 1, &signature));

  // Validate signature on the last hit.
  ASSERT_EQ(kAddress, signature.base_address.value());
  ASSERT_EQ(kSize, signature.module_size);
  ASSERT_EQ(kChecksum, signature.module_checksum);
  ASSERT_EQ(kTimestamp, signature.module_time_date_stamp);
  ASSERT_EQ(kPath, signature.path);
}

TEST(AddTypedBlockRecord, BasicTest) {
  ProcessState state;
  AddTypedBlockRecord(AddressRange(kAddress, kSize),
                      base::ASCIIToUTF16(kDataName),
                      base::ASCIIToUTF16(kTypeName), &state);

  // Validate a record was added.
  TypedBlockLayerPtr layer;
  ASSERT_TRUE(state.FindLayer(&layer));
  std::vector<TypedBlockRecordPtr> matching_records;
  layer->GetRecordsAt(kAddress, &matching_records);
  ASSERT_EQ(1, matching_records.size());

  // Validate range.
  TypedBlockRecordPtr record = matching_records[0];
  ASSERT_EQ(AddressRange(kAddress, kSize), record->range());

  // Validate TypedBlock proto.
  TypedBlock* proto = record->mutable_data();
  ASSERT_EQ(kDataName, proto->data_name());
  ASSERT_EQ(kTypeName, proto->type_name());
}

}  // namespace refinery
