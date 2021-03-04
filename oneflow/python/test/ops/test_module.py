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
import unittest
from typing import Tuple

import numpy as np

import oneflow as flow
import oneflow.typing as tp


_counter = 0


def get_var_helper(shape):
    global _counter
    var = flow.get_variable(
        "x_" + str(_counter), shape=shape, initializer=flow.kaiming_initializer(shape)
    )
    _counter += 1
    return var


@unittest.skipIf(
    not flow.unittest.env.eager_execution_enabled(),
    ".numpy() doesn't work in eager mode",
)
class TestModule(flow.unittest.TestCase):
    def test_x(test_case):
        class CustomModule(flow.nn.Module):
            def __init__(self):
                super().__init__()
                self.w = flow.nn.Parameter(flow.Tensor((2, 3)))

            def forward(self, x):
                return x + self.w

        m = CustomModule()
        print(m.state_dict())

        @flow.global_function()
        def job() -> None:
            x = flow.Tensor((2, 3))
            print(m(x).numpy())

        job()
        m.load_state_dict({"x_6": np.ones((2, 3), dtype=np.float32)})
        job()

    def test_parameter(test_case):
        shape = (3, 4)
        t = flow.Tensor(shape)
        p = flow.nn.Parameter(t)
        test_case.assertEqual(type(p), flow.nn.Parameter)
        test_case.assertEqual(p.shape, shape)

    def test_module_forward(test_case):
        class CustomModule(flow.nn.Module):
            def __init__(self, w):
                super().__init__()
                self.w = w

            def forward(self, x):
                return x + self.w

        m = CustomModule(5)
        test_case.assertEqual(m(1), 6)

        m = CustomModule(4)
        test_case.assertEqual(m(3), 7)

    # def test_forward_with_variable(test_case):
    #     class AddTo(flow.nn.Module):
    #         def __init__(self):
    #             super().__init__()
    #             self.w = flow.nn.Parameter(flow.Tensor(2, 3))
    #
    #         def forward(self, x):
    #             return x + self.w()
    #
    #     @flow.global_function()
    #     def job() -> Tuple[tp.Numpy, tp.Numpy]:
    #         x = get_var_helper((2, 3))
    #         m = AddTo()
    #         return m(x), m.w() + x
    #
    #     res1, res2 = job()
    #     test_case.assertTrue(np.array_equal(res1, res2))

    def test_forward_with_sbp(test_case):
        class AddTo(flow.nn.Module):
            def __init__(self, w):
                super().__init__()
                self.w = w

            def forward(self, x, *args):
                return x + self.w

        @flow.global_function()
        def job() -> Tuple[tp.Numpy, tp.Numpy]:
            w = get_var_helper((2, 3))
            x = get_var_helper((2, 3))
            m = AddTo(w)
            m.input_configs[0] = flow.distribute.split(0)
            return m(x), w + x

        res1, res2 = job()
        test_case.assertTrue(np.array_equal(res1, res2))

    def test_train_eval(test_case):
        m = flow.nn.Module()
        test_case.assertEqual(m.training, True)
        m.train()
        test_case.assertEqual(m.training, True)
        m.eval()
        test_case.assertEqual(m.training, False)

    def test_module_setattr(test_case):
        class CustomModule(flow.nn.Module):
            def __init__(self, param1, param2):
                super().__init__()
                self.param1 = param1
                self.param2 = param2

        param0 = flow.nn.Parameter((2, 3))
        param1 = flow.nn.Parameter((2, 3))
        param2 = CustomModule(param0, param1)
        m = CustomModule(param1, param2)

        # m.parameters() contains param0 + param1 in submodule param2
        # and param1 in m
        params = list(m.parameters())
        test_case.assertEqual(len(params), 2)
        test_case.assertEqual(params[0], param1)
        test_case.assertEqual(params[1], param0)

        children = list(m.children())
        test_case.assertEqual(len(children), 1)
        child = children[0]
        test_case.assertEqual(child, param2)

        child_params = list(child.parameters())
        test_case.assertEqual(len(child_params), 2)
        test_case.assertEqual(child_params[0], param0)
        test_case.assertEqual(child_params[1], param1)

    def test_state_dict(test_case):
        class CustomModule(flow.nn.Module):
            def __init__(self, param1, param2):
                super().__init__()
                self.param1 = param1
                self.param2 = param2

        param0 = flow.nn.Parameter((2, 3))
        param1 = flow.nn.Parameter((2, 3))
        param2 = CustomModule(param0, param1)
        m = CustomModule(param1, param2)

        state_dict = m.state_dict()
        print(state_dict)
        test_case.assertEqual(len(state_dict), 3)

    def test_consistent_mirrored(test_case):
        flow.config.gpu_device_num(flow.unittest.env.device_num())

        @flow.global_function()
        def job():
            x1 = get_var_helper((4, 4))
            x2 = get_var_helper((4, 4))
            x3 = x1 + x2
            x4 = flow.advanced.distribute_split(x3)
            parallel_desc_symbol = flow.current_scope().device_parallel_desc_symbol
            device_tag = parallel_desc_symbol.device_tag
            x_list = []
            parallel_id = 0
            for (
                machine_id,
                device_ids,
            ) in parallel_desc_symbol.machine_id2device_id_list.items():
                for device_id in device_ids:
                    with flow.scope.placement(
                        device_tag, str(machine_id) + ":" + str(device_id)
                    ):
                        x5 = x4[parallel_id]
                        if parallel_id == 1:
                            x6 = x5 + 100
                        else:
                            x6 = flow.identity(x5)
                        print(x6.numpy())
                        x_list.append(x6)
                        parallel_id += 1
            x8 = flow.advanced.distribute_concat(x_list)
            flow.watch(x8, lambda x: print(x.numpy()))

        job()

    # TODO: add more tests about module api

    def test_parameter_and_buffer(test_case):
        class CustomModule(flow.nn.Module):
            def __init__(self):
                super().__init__()
                self.param1 = flow.nn.Parameter((1, 3))
                self.b1 = flow.Tensor((1,))
                self.register_parameter("param2", self.param1)
                self.register_parameter("param3", flow.nn.Parameter((2, 3)))
                self.register_buffer("buffer1", self.b1)
                self.register_buffer("buffer2", flow.nn.Parameter((3, 2)))

            def forward(self, x):
                return x
        

        @flow.global_function()
        def job() -> None:
            x = flow.Tensor((2,3))
            print("\n>>>>>>>>>>>>>>>>>test_parameter_and_buffer<<<<<<<<<<<<<<<<<<<\n", m(x).numpy())

        m = CustomModule()
        print("\n=================m.state_dict()=================\n",m.state_dict())
        print("=================m._parameters=================\n",m._parameters)
        print("=================m._buffers=================\n",m._buffers)
        print("=================m.named_parameters()=================\n",m.named_parameters())
        print("=================m.named_buffers()=================\n",m.named_buffers())
        print("=================m.__getattr__()=================\n",m.__getattr__("param3"))
        job()


    
    def test_module_and_children(test_case):
        class CustomModule1(flow.nn.Module):
            def __init__(self):
                super().__init__()
                self.m1_param1 = flow.nn.Parameter((1, 3))
                self.register_buffer("m1_buffer1", flow.nn.Parameter((3, 2)))

            def forward(self, x):
                print("CustomModule1 floward >>>>>>>>>>>>>>>>>>>>>>>>> ", x.numpy)
                return x

        class CustomModule2(flow.nn.Module):
            def __init__(self):
                super().__init__()
                self.m2_param1 = flow.nn.Parameter((1, 3))
                self.module1 = CustomModule1()
                self.register_buffer("m2_buffer1", flow.nn.Parameter((3, 2)))

            def forward(self, x):
                y = self.module1(x)
                print("CustomModule2 floward >>>>>>>>>>>>>>>>>>>>>>>>> ", y.numpy)
                return y
        

        @flow.global_function()
        def job() -> None:
            x = flow.Tensor((2,3))
            y = model(x)
            print("\n>>>>>>>>>>>>>>>> test_module_and_children <<<<<<<<<<<<<<<\n", y.numpy())
        
        model = CustomModule2()
        print("\n=================model.state_dict()=================\n",model.state_dict())
        print("=================model.modules()=================\n",model.modules())
        print("=================model.named_modules()=================\n",model.named_modules())
        print("=================model.children()=================\n",model.children())
        print("=================model.named_children()=================\n",model.named_children())
        job()

    
    def test_lenet(test_case):
        class LeNet(flow.nn.Module):
            def __init__(self):
                super().__init__()
                self.m1_param1 = flow.nn.Parameter((1, 3))
                self.register_buffer("m1_buffer1", flow.nn.Parameter((3, 2)))

            def forward(self, x):
                initializer = flow.truncated_normal(0.1)
                conv1 = flow.layers.conv2d(
                    x,
                    32,
                    5,
                    padding="SAME",
                    activation=flow.nn.relu,
                    name="conv1",
                    kernel_initializer=initializer,
                )
                pool1 = flow.nn.max_pool2d(
                    conv1, ksize=2, strides=2, padding="SAME", name="pool1", data_format="NCHW"
                )
                conv2 = flow.layers.conv2d(
                    pool1,
                    64,
                    5,
                    padding="SAME",
                    activation=flow.nn.relu,
                    name="conv2",
                    kernel_initializer=initializer,
                )
                pool2 = flow.nn.max_pool2d(
                    conv2, ksize=2, strides=2, padding="SAME", name="pool2", data_format="NCHW"
                )
                reshape = flow.reshape(pool2, [pool2.shape[0], -1])
                hidden = flow.layers.dense(
                    reshape,
                    512,
                    activation=flow.nn.relu,
                    kernel_initializer=initializer,
                    name="dense1",
                )
                if self.training == True:
                    hidden = flow.nn.dropout(hidden, rate=0.5, name="dropout")
                return flow.layers.dense(hidden, 10, kernel_initializer=initializer, name="dense2")


            
        @flow.global_function(type="train")
        def train_job(
            images: tp.Numpy.Placeholder((100, 1, 28, 28), dtype=flow.float),
            labels: tp.Numpy.Placeholder((100,), dtype=flow.int32),
        ) -> tp.Numpy:
            with flow.scope.placement("gpu", "0:0"):
                logits = model(images)
                loss = flow.nn.sparse_softmax_cross_entropy_with_logits(
                    labels, logits, name="softmax_loss"
                )
            lr_scheduler = flow.optimizer.PiecewiseConstantScheduler([], [0.1])
            flow.optimizer.SGD(lr_scheduler, momentum=0).minimize(loss)
            return loss
        
        model = LeNet()
        (train_images, train_labels), (test_images, test_labels) = flow.data.load_mnist(100,100)
        for epoch in range(1):
            for i, (images, labels) in enumerate(zip(train_images, train_labels)):
                loss = train_job(images, labels)
                if i % 20 == 0:
                    print(loss.mean())
        flow.checkpoint.save("./lenet_models_1")  # need remove the existed folder
        print("model saved")


if __name__ == "__main__":
    unittest.main()
