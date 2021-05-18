/*
Copyright 2020 The OneFlow Authors. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#include "oneflow/core/register/runtime_register_desc.h"
#include "oneflow/core/common/protobuf.h"

namespace oneflow {

// 根据RegstDescProto信息构造RtRegstDesc
RtRegstDesc::RtRegstDesc(const RegstDescProto& proto) {
  regst_desc_id_ = proto.regst_desc_id();
  producer_actor_id_ = proto.producer_task_id();
  consumers_actor_id_ = PbRf2StdVec(proto.consumer_task_id());
  register_num_ = proto.register_num();
  mem_case_ = proto.mem_case();
  regst_desc_type_ = proto.regst_desc_type();
  // 1. 若为数据型Regst，则根据data_regst_desc信息初始化成员变量
  if (proto.regst_desc_type().has_data_regst_desc()) {
    const DataRegstDesc& data_regst_desc = proto.regst_desc_type().data_regst_desc();
    std::vector<LbiBlobDescPair> lbi_pairs(
        {data_regst_desc.lbi2blob_desc().cbegin(), data_regst_desc.lbi2blob_desc().cend()});
    // 按LogicalBlobId值进行排序
    std::sort(lbi_pairs.begin(), lbi_pairs.end(), &CompareLbiBlobDescPair);
    CHECK_EQ(lbi_pairs.size(), 1);
    sorted_blob_desc_vec_.reserve(lbi_pairs.size());
    sorted_lbi_vec_.reserve(lbi_pairs.size());
    for (int64_t i = 0; i < lbi_pairs.size(); ++i) {
      const LbiBlobDescPair& pair = lbi_pairs.at(i);
      sorted_blob_desc_vec_.push_back(std::make_unique<RtBlobDesc>(pair.blob_desc()));
      sorted_lbi_vec_.push_back(pair.lbi());
      lbi2blob_desc_ordinal_.emplace(pair.lbi(), i);
    }
    CHECK(data_regst_desc.has_time_shape());
    data_regst_time_shape_.reset(new Shape(data_regst_desc.time_shape()));
  // 2. 若为控制型Regst，创建一个包含信息较少的RtBlobDesc
  } else {
    sorted_blob_desc_vec_.push_back(std::make_unique<RtBlobDesc>(BlobDesc(DataType::kChar)));
  }
}

int64_t RtRegstDesc::GetOrdinalForLbi(const LogicalBlobId& lbi) const {
  auto it = lbi2blob_desc_ordinal_.find(lbi);
  if (it != lbi2blob_desc_ordinal_.cend()) {
    return it->second;
  } else {
    return -1;
  }
}

const RtBlobDesc* RtRegstDesc::GetRtBlobDescFromLbi(const LogicalBlobId& lbi) const {
  auto it = lbi2blob_desc_ordinal_.find(lbi);
  if (it == lbi2blob_desc_ordinal_.end()) {
    return nullptr;
  } else {
    return GetRtBlobDescByOrdinal(it->second);
  }
}

const RtBlobDesc* RtRegstDesc::GetRtBlobDescByOrdinal(int64_t ordinal) const {
  return sorted_blob_desc_vec_.at(ordinal).get();
}

const LogicalBlobId& RtRegstDesc::GetLbiByOrdinal(int64_t ordinal) const {
  return sorted_lbi_vec_.at(ordinal);
}

const RtBlobDesc* RtRegstDesc::GetSoleRtBlobDesc() const {
  CHECK_EQ(sorted_blob_desc_vec_.size(), 1);
  return sorted_blob_desc_vec_.at(0).get();
}

size_t RtRegstDesc::TotalByteSize4AllRegst() const {
  return GetSoleRtBlobDesc()->AlignedTotalByteSize() * register_num_;
}

size_t RtRegstDesc::TotalMainByteSize4AllRegst() const {
  return MainByteSize4OneRegst() * register_num_;
}

size_t RtRegstDesc::MainByteSize4OneRegst() const {
  if (mem_case_.has_device_cuda_mem()) {
    return GetSoleRtBlobDesc()->AlignedByteSizeOfBlobBody();
  } else {
    return GetSoleRtBlobDesc()->AlignedTotalByteSize();
  }
}

size_t RtRegstDesc::TotalSeparatedHeaderByteSize4AllRegst() const {
  return SeparatedHeaderByteSize4OneRegst() * register_num_;
}

size_t RtRegstDesc::SeparatedHeaderByteSize4OneRegst() const {
  if (mem_case_.has_device_cuda_mem()) {
    return GetSoleRtBlobDesc()->ByteSizeOfBlobHeader();
  } else {
    return 0;
  }
}

const Shape& RtRegstDesc::data_regst_time_shape() const {
  CHECK(regst_desc_type_.has_data_regst_desc());
  CHECK(data_regst_time_shape_);
  return *data_regst_time_shape_;
}

// 计算每个Blob的内存偏移，执行回调函数
void RtRegstDesc::ForEachBlobDescOffsetInOnRegst(
    const std::function<void(int64_t ordinal, const LogicalBlobId& lbi, const RtBlobDesc* desc,
                             int64_t body_offset, int64_t header_offset)>& Handler) const {
  int64_t cur_body_offset = 0;
  int64_t cur_header_offset = 0;
  for (int64_t i = 0; i < sorted_blob_desc_vec_.size(); ++i) {
    const RtBlobDesc* blob_desc = sorted_blob_desc_vec_.at(i).get();
    const LogicalBlobId& lbi = sorted_lbi_vec_.at(i);
    Handler(i, lbi, blob_desc, cur_body_offset, cur_header_offset);
    cur_body_offset += blob_desc->AlignedByteSizeOfBlobBody();
    cur_header_offset += blob_desc->ByteSizeOfBlobHeader();
  }
}

}  // namespace oneflow
