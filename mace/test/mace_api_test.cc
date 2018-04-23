//
// Copyright (c) 2017 XiaoMi All rights reserved.
//

#include <fstream>

#include "mace/core/operator.h"
#include "mace/kernels/conv_pool_2d_util.h"
#include "mace/ops/ops_test_util.h"

namespace mace {
namespace test {

class MaceAPITest  : public ::testing::Test {};

namespace {

void GenerateInputs(const std::vector<std::string> &input_names,
                    const std::vector<int64_t> &input_shape,
                    std::map<std::string, mace::MaceTensor> *inputs) {
  size_t input_size = input_names.size();
  for (size_t i = 0; i < input_size; ++i) {
    // Allocate input and output
    int64_t input_size =
        std::accumulate(input_shape.begin(), input_shape.end(), 1,
                        std::multiplies<int64_t>());
    auto buffer_in = std::shared_ptr<float>(new float[input_size],
                                            std::default_delete<float[]>());
    // load input
    std::vector<float> input_data;
    ops::test::GenerateRandomRealTypeData(input_shape, &input_data);
    memcpy(buffer_in.get(), input_data.data(), input_size * sizeof(float));
    (*inputs)[input_names[i]] = mace::MaceTensor(input_shape, buffer_in);
  }
}

void GenerateOutputs(const std::vector<std::string> &output_names,
                     const std::vector<int64_t> &output_shape,
                     std::map<std::string, mace::MaceTensor> *outputs) {
  size_t output_size = output_names.size();
  for (size_t i = 0; i < output_size; ++i) {
    int64_t output_size =
        std::accumulate(output_shape.begin(), output_shape.end(), 1,
                        std::multiplies<int64_t>());
    auto buffer_out = std::shared_ptr<float>(new float[output_size],
                                             std::default_delete<float[]>());
    (*outputs)[output_names[i]] = mace::MaceTensor(output_shape, buffer_out);
  }
}

template <typename T>
void BufferToImage(const std::string &input_name,
                   const std::string &output_name,
                   const int buffer_type,
                   const std::vector<int> &mem_ids,
                   NetDef *net_def,
                   const int mode = NetMode::NORMAL) {
  OperatorDef operator_def;

  ops::test::OpDefBuilder("BufferToImage", "BufferToImageOp")
      .Input(input_name)
      .Output(output_name)
      .AddIntArg("buffer_type", buffer_type)
      .AddIntArg("T", static_cast<int>(DataTypeToEnum<T>::value))
      .AddIntArg("mode", mode)
      .Finalize(&operator_def);

  operator_def.set_mem_id(mem_ids);

  net_def->add_op()->CopyFrom(operator_def);
}

template <typename T>
void ImageToBuffer(const std::string &input_name,
                   const std::string &output_name,
                   const int buffer_type,
                   NetDef *net_def) {
  OperatorDef operator_def;

  ops::test::OpDefBuilder("ImageToBuffer", "ImageToBufferOp")
      .Input(input_name)
      .Output(output_name)
      .AddIntArg("buffer_type", buffer_type)
      .AddIntArg("T", static_cast<int>(DataTypeToEnum<T>::value))
      .Finalize(&operator_def);

  net_def->add_op()->CopyFrom(operator_def);
}

template <typename T>
void Conv3x3(const std::string &input_name,
             const std::string &filter_name,
             const std::string &output_name,
             const std::vector<int> &mem_ids,
             NetDef *net_def) {
  OperatorDef operator_def;
  ops::test::OpDefBuilder("Conv2D", "Conv2dOp")
      .Input(input_name)
      .Input(filter_name)
      .Output(output_name)
      .AddIntsArg("strides", {1, 1})
      .AddIntArg("padding", Padding::SAME)
      .AddIntsArg("dilations", {1, 1})
      .AddIntArg("T", static_cast<int>(DataTypeToEnum<T>::value))
      .Finalize(&operator_def);

  operator_def.set_mem_id(mem_ids);
  net_def->add_op()->CopyFrom(operator_def);
}

template <typename T>
void Relu(const std::string &input_name,
          const std::string &output_name,
          NetDef *net_def) {
  OperatorDef operator_def;
  ops::test::OpDefBuilder("Activation", "ReluTest")
      .Input(input_name)
      .Output(output_name)
      .AddStringArg("activation", "RELU")
      .AddIntArg("T", static_cast<int>(DataTypeToEnum<T>::value))
      .Finalize(&operator_def);

  net_def->add_op()->CopyFrom(operator_def);
}

template <typename T>
void AddTensor(const std::string &name,
               const std::vector<int64_t> &shape,
               T *data,
               NetDef *net_def) {
  ConstTensor tensor(name,
                     reinterpret_cast<unsigned char *>(data),
                     shape,
                     DataTypeToEnum<T>::value);

  net_def->mutable_tensors().push_back(tensor);
}

template <DeviceType D, typename T>
void CheckOutputs(const NetDef &net_def,
                  const std::map<std::string, mace::MaceTensor> &inputs,
                  const std::map<std::string, mace::MaceTensor> &outputs) {
  ops::test::OpsTestNet net;
  for (auto input : inputs) {
    auto input_shape = input.second.shape();
    const int64_t data_size = std::accumulate(input_shape.begin(),
                                              input_shape.end(), 1,
                                              std::multiplies<int64_t>());
    std::vector<float> input_data(data_size);
    memcpy(input_data.data(), input.second.data().get(),
           data_size * sizeof(float));
    std::string input_name = MakeString("mace_input_node_",
                                        input.first, ":0");
    net.AddInputFromArray<D, float>(input_name, input.second.shape(),
                                    input_data);
  }
  auto tensors = net_def.tensors();
  for (auto tensor : tensors) {
    auto shape = tensor.dims();
    const int64_t data_size = std::accumulate(shape.begin(),
                                              shape.end(), 1,
                                              std::multiplies<int64_t>());
    std::vector<T> data(data_size);
    memcpy(data.data(), reinterpret_cast<const T *>(tensor.data()),
           data_size * sizeof(T));
    net.AddInputFromArray<D, T>(tensor.name(), shape, data);
  }
  net.RunNet(net_def, D);

  for (auto output : outputs) {
    std::unique_ptr<Tensor> tmp_tensor(
        new Tensor(GetDeviceAllocator(DeviceType::CPU),
                   DataTypeToEnum<float>::v()));
    auto output_shape = output.second.shape();
    const int64_t data_size = std::accumulate(output_shape.begin(),
                                              output_shape.end(), 1,
                                              std::multiplies<float>());
    tmp_tensor->Resize(output.second.shape());
    float *data = tmp_tensor->mutable_data<float>();
    memcpy(data, output.second.data().get(), data_size * sizeof(float));
    std::string output_name = MakeString("mace_output_node_",
                                         output.first, ":0");
    ops::test::ExpectTensorNear<float>(*tmp_tensor,
                                       *net.GetOutput(output_name.data()),
                                       1e-5);
  }
}

std::map<std::string, int> AddMemoryOptimization(
    const std::vector<std::string> &input_names,
    const std::vector<std::string> &output_names,
    const std::vector<std::vector<int64_t>> &input_shapes,
    const std::vector<std::vector<int64_t>> &output_shapes,
    NetDef *net_def) {
  std::map<std::string, int> res;
  int mem_id = 0;
  size_t input_shape_size = input_shapes.size();
  uint32_t in_mem_block_x = 0;
  uint32_t in_mem_block_y = 0;
  for (size_t i = 0; i < input_shape_size; ++i) {
    in_mem_block_x = std::max<uint32_t>(in_mem_block_x,
                                        input_shapes[i][2] *
                                            RoundUpDiv4(input_shapes[i][3]));
    in_mem_block_y = std::max<uint32_t>(in_mem_block_y,
                                        input_shapes[i][0] *
                                            input_shapes[i][1]);
  }
  size_t input_size = input_names.size();
  for (size_t i = 0; i < input_size; ++i) {
    net_def->mutable_mem_arena().mutable_mem_block().push_back(
        MemoryBlock(mem_id, in_mem_block_x, in_mem_block_y));
    res[input_names[i]] = mem_id;
    mem_id++;
  }
  size_t output_shape_size = output_shapes.size();
  uint32_t out_mem_block_x = 0;
  uint32_t out_mem_block_y = 0;
  for (size_t i = 0; i < output_shape_size; ++i) {
    out_mem_block_x = std::max<uint32_t>(out_mem_block_x,
                                         output_shapes[i][2] *
                                             RoundUpDiv4(output_shapes[i][3]));
    out_mem_block_y = std::max<uint32_t>(out_mem_block_y,
                                         output_shapes[i][0] *
                                             output_shapes[i][1]);
  }
  size_t output_size = output_names.size();
  for (size_t i = 0; i < output_size; ++i) {
    net_def->mutable_mem_arena().mutable_mem_block().push_back(
        MemoryBlock(mem_id, out_mem_block_x, out_mem_block_y));
    res[output_names[i]] = mem_id;
    mem_id++;
  }
  return res;
}

// The height and width of input and output must be equal.
template <typename T>
void MaceRun(const int in_out_size,
             const std::vector<std::vector<int64_t>> &input_shapes,
             const std::vector<std::vector<int64_t>> &output_shapes,
             const std::vector<int64_t> &filter_shape) {
  std::vector<std::string> input_names;
  std::vector<std::string> output_names;
  for (int i = 0; i < in_out_size; ++i) {
    input_names.push_back(MakeString("input", i));
    output_names.push_back(MakeString("output", i));
  }
  std::string filter_tensor_name = "filter";
  std::string filter_tensor_img_name = filter_tensor_name + "_image";

  const DeviceType device = DeviceType::OPENCL;

  NetDef net_def;

  // Add memory optimization
  auto mem_map = AddMemoryOptimization(input_names, output_names,
                                       input_shapes, output_shapes,
                                       &net_def);

  std::vector<T> data;
  ops::test::GenerateRandomRealTypeData<T>(filter_shape, &data);
  AddTensor<T>(filter_tensor_name, filter_shape, data.data(), &net_def);

  for (size_t i = 0; i < input_names.size(); ++i) {
    std::string input_name = MakeString("mace_input_node_",
                                        input_names[i], ":0");
    BufferToImage<half>(input_name, input_names[i],
                        mace::kernels::IN_OUT_CHANNEL,
                        {mem_map[input_names[i]]},
                        &net_def);
  }
  BufferToImage<half>(filter_tensor_name, filter_tensor_img_name,
                      mace::kernels::CONV2D_FILTER, {},
                      &net_def, NetMode::INIT);
  for (size_t i = 0; i < output_names.size(); ++i) {
    Conv3x3<half>(input_names[i], filter_tensor_img_name,
                  output_names[i], {mem_map[output_names[i]]},
                  &net_def);
  }
  for (size_t i = 0; i < output_names.size(); ++i) {
    std::string output_name = MakeString("mace_output_node_",
                                         output_names[i], ":0");
    ImageToBuffer<float>(output_names[i], output_name,
                         mace::kernels::IN_OUT_CHANNEL, &net_def);
  }

  MaceEngine engine(&net_def, device, input_names, output_names);

  std::map<std::string, mace::MaceTensor> inputs;
  std::map<std::string, mace::MaceTensor> outputs;

  for (int i = 0; i < 5; ++i) {
    size_t input_shape_size = input_shapes.size();
    for (size_t j = 0; j < input_shape_size; ++j) {
      inputs.clear();
      outputs.clear();
      GenerateInputs(input_names, input_shapes[j], &inputs);
      GenerateOutputs(output_names, output_shapes[j], &outputs);
      engine.Run(inputs, &outputs);
    }
  }

  CheckOutputs<DeviceType::OPENCL, T>(net_def, inputs, outputs);
}

}  // namespace

TEST_F(MaceAPITest, GPUSingleInputOutput) {
  MaceRun<float>(1, {{1, 32, 32, 16}}, {{1, 32, 32, 16}}, {3, 3, 16, 16});
  MaceRun<half>(1, {{1, 32, 32, 16}}, {{1, 32, 32, 16}}, {3, 3, 16, 16});
}

TEST_F(MaceAPITest, GPUMultipleInputOutput) {
  MaceRun<float>(2,
                 {{1, 16, 32, 16}},
                 {{1, 16, 32, 16}},
                 {3, 3, 16, 16});
  MaceRun<half>(2,
                {{1, 16, 32, 16}},
                {{1, 16, 32, 16}},
                {3, 3, 16, 16});
}

TEST_F(MaceAPITest, GPUVariableInputShape) {
  MaceRun<float>(1,
                 {{1, 16, 32, 16}, {1, 32, 64, 16}},
                 {{1, 16, 32, 16}, {1, 32, 64, 16}},
                 {3, 3, 16, 16});
  MaceRun<float>(2,
                 {{1, 16, 32, 16}, {1, 32, 64, 16}},
                 {{1, 16, 32, 16}, {1, 32, 64, 16}},
                 {3, 3, 16, 16});
}

}  // namespace test
}  // namespace mace