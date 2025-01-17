"""
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
"""
import os
import unittest
from collections import OrderedDict

import numpy as np
from oneflow.test_utils.test_util import GenArgList

import oneflow as flow
import oneflow.unittest


@flow.unittest.skip_unless_1n2d()
@unittest.skipIf(os.getenv("ONEFLOW_TEST_CPU_ONLY"), "only test cpu cases")
class TestModuleToCosistent(flow.unittest.TestCase):
    def test_module_to_global(test_case):
        rank = flow.env.get_rank()
        P = flow.placement("cuda", ranks=[0, 1])
        B = flow.sbp.broadcast

        class ReuseVarModule(flow.nn.Module):
            def __init__(self):
                super().__init__()
                self.linear1 = flow.nn.Linear(3, 4)
                self.linear2 = flow.nn.Linear(3, 4)
                self.linear2.weight = self.linear1.weight

        reuse_var_m = ReuseVarModule()

        test_case.assertTrue(reuse_var_m.linear1.weight is reuse_var_m.linear2.weight)
        test_case.assertEqual(
            reuse_var_m.linear1.weight.device, flow.device("cpu", rank)
        )

        test_case.assertTrue(reuse_var_m.linear1.bias is not reuse_var_m.linear2.bias)
        test_case.assertEqual(reuse_var_m.linear1.bias.device, flow.device("cpu", rank))

        reuse_var_m.to_global(placement=P, sbp=B)

        test_case.assertTrue(reuse_var_m.linear1.weight is reuse_var_m.linear2.weight)
        test_case.assertEqual(reuse_var_m.linear1.weight.placement, P)
        test_case.assertEqual(reuse_var_m.linear1.weight.sbp[0], B)

        test_case.assertTrue(reuse_var_m.linear1.bias is not reuse_var_m.linear2.bias)
        test_case.assertEqual(reuse_var_m.linear1.bias.placement, P)
        test_case.assertEqual(reuse_var_m.linear1.bias.sbp[0], B)


if __name__ == "__main__":
    unittest.main()
