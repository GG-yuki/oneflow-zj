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
#ifndef ONEFLOW_CORE_REGISTER_REGISTER_H_
#define ONEFLOW_CORE_REGISTER_REGISTER_H_

#include "oneflow/core/register/blob.h"
#include "oneflow/core/register/runtime_register_desc.h"

namespace oneflow {

struct RegstStatus {
  int64_t regst_desc_id;
  int64_t piece_id;
  int64_t act_id;
};

// 运行时的Regst，被Actor管理
class Regst final {
 public:
  OF_DISALLOW_COPY_AND_MOVE(Regst);
  ~Regst();

  // Getters
  const RegstStatus& status() const { return status_; }
  int64_t piece_id() const { return status_.piece_id; }
  int64_t act_id() const { return status_.act_id; }
  int64_t regst_desc_id() const {
    CHECK_NE(status_.regst_desc_id, -1);
    return status_.regst_desc_id;
  }

  int64_t producer_actor_id() const { return regst_desc_->producer_actor_id(); }
  const std::vector<int64_t>& consumers_actor_id() const;
  const RtRegstDesc* regst_desc() const { return regst_desc_; }
  Blob* GetBlobByOrdinal(int64_t ordinal);
  Blob* GetBlobByLbi(const LogicalBlobId& lbi);
  const Blob* GetSoleBlob() const;
  Blob* GetMutSoleBlob();
  int64_t GetBlobSize() const { return sorted_blob_vec_.size(); }
  void* comm_net_token() const { return comm_net_token_; }

  // Setters
  void set_piece_id(int64_t val) { status_.piece_id = val; }
  void set_act_id(int64_t val) { status_.act_id = val; }

 private:
  // 构造函数私有，只有RegstMgr能够创建Regst实例
  friend class RegstMgr;
  Regst();

  void set_regst_desc(const RtRegstDesc* regst_desc);
  void SetBlobByOrdinal(int64_t ordinal, std::unique_ptr<Blob>&& blob);

  // 在CommNet注册内存后的返回值，用于调用CommNet::Read()
  void* comm_net_token_;
  RegstStatus status_;
  const RtRegstDesc* regst_desc_;
  // 管理真正的Blob内存
  std::vector<std::unique_ptr<Blob>> sorted_blob_vec_;
};

}  // namespace oneflow

#endif  // ONEFLOW_CORE_REGISTER_REGISTER_H_
