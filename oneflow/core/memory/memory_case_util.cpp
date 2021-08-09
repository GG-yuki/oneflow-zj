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
#include "oneflow/core/memory/memory_case_util.h"

namespace oneflow {

bool MemoryCaseUtil::GetCommonMemoryCase(const MemoryCase& a, const MemoryCase& b,
                                         MemoryCase* common) {
  // 返回描述同一个device的MemoryCase
  if (a.has_device_cuda_mem() && b.has_device_cuda_mem()) {
    if (a.device_cuda_mem().device_id() == b.device_cuda_mem().device_id()) {
      *common = a;
      return true;
    } else {
      return false;
    }
  // 返回描述host的MemoryCase
  } else if (a.has_host_mem() && b.has_host_mem()) {
    *common = a;
    // 对是否为host与device的交换区取并集
    if (b.host_mem().has_cuda_pinned_mem()) {
      *common->mutable_host_mem()->mutable_cuda_pinned_mem() = b.host_mem().cuda_pinned_mem();
    }
    // 对是否用于CommNet取并集
    if (b.host_mem().has_used_by_network()) {
      common->mutable_host_mem()->set_used_by_network(true);
    }
    return true;
  } else {
    return false;
  }
}

// 根据输入mem_case，设置host与device的交换区信息
MemoryCase MemoryCaseUtil::GetHostPinnedMemoryCaseForRegstSeparatedHeader(
    const MemoryCase& mem_case) {
  CHECK(mem_case.has_device_cuda_mem());
  MemoryCase ret;
  ret.mutable_host_mem()->mutable_cuda_pinned_mem()->set_device_id(
      mem_case.device_cuda_mem().device_id());
  return ret;
}

// 获取MemoryCase对应分区（host/某一个device/主机与某一个device的交换区）的id
int64_t MemoryCaseUtil::GenMemZoneId(const MemoryCase& mem_case) {
  // [0, 127] = GPU device mem
  // [128] = CPU host mem
  // [129, 256] = CPU host mem used by CUDA with device id
  // [257, ...] Other Device
  if (mem_case.has_device_cuda_mem()) {
    return mem_case.device_cuda_mem().device_id();  // GPU device mem
  }
  if (mem_case.has_host_mem()) {
    if (mem_case.host_mem().has_cuda_pinned_mem()) {
      return 129 + mem_case.host_mem().cuda_pinned_mem().device_id();  // Host mem used by GPU
    }
    return 128;  // CPU host mem
  }
  UNIMPLEMENTED();
  return -1;
}

// 获取带节点信息的MemoryCase所描述的区域对应的id
int64_t MemoryCaseUtil::GenMemZoneUniqueId(int64_t machine_id, const MemoryCase& mem_case) {
  return (machine_id << 32) | (MemoryCaseUtil::GenMemZoneId(mem_case));
}

bool MemoryCaseUtil::IsHostUnPinnedMemoryCase(const MemoryCase& mem_case) {
  return mem_case.has_host_mem() && !mem_case.host_mem().has_cuda_pinned_mem()
         && !mem_case.host_mem().used_by_network();
}

std::shared_ptr<MemoryCase> MemoryCaseUtil::MakeMemCase(const DeviceType device_type,
                                                        const int64_t device_id) {
  const auto& mem_case = std::make_shared<MemoryCase>();
  if (device_type == DeviceType::kCPU) {
    mem_case->mutable_host_mem();
  } else if (device_type == DeviceType::kGPU) {
    mem_case->mutable_device_cuda_mem()->set_device_id(device_id);
  } else {
    UNIMPLEMENTED();
  }
  return mem_case;
}

}  // namespace oneflow
