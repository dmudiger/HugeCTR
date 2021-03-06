/*
 * Copyright (c) 2019, NVIDIA CORPORATION.
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


#include <cstdlib>
#include <vector>
#include "HugeCTR/include/general_buffer.hpp"
#include "HugeCTR/include/loss.hpp"
#include "gtest/gtest.h"
#include "utest/test_utils.h"
using namespace std;
using namespace HugeCTR;
using namespace HugeCTR::test;

void multi_cross_entropy_loss(int label_dim, int batch_size) {
  GeneralBuffer<float> input_b;
  GeneralBuffer<float> label_b;
  GeneralBuffer<float> loss_b;

  Tensor<float> input_tensor((std::vector<int>){batch_size, label_dim}, input_b,
                             TensorFormat_t::HW);
  Tensor<float> label_tensor((std::vector<int>){batch_size, label_dim}, label_b,
                             TensorFormat_t::HW);
  Tensor<float> loss_tensor(std::vector<int>{1, 1}, loss_b, TensorFormat_t::HW);

  input_b.init(0);
  label_b.init(0);
  loss_b.init(0);

  const std::vector<float> target_weight(label_dim, 1.0);

  MultiCrossEntropyLoss mel(label_tensor, input_tensor, loss_tensor, target_weight, 0);

  float h_input[batch_size * label_dim];
  float h_label[batch_size * label_dim];

  float *d_input = input_b.get_ptr_with_offset(0);
  float *d_label = label_b.get_ptr_with_offset(0);
  float *d_loss = loss_b.get_ptr_with_offset(0);

  for (int i = 0; i < batch_size * label_dim; ++i) h_input[i] = rand() % 100 * 0.01f;
  for (int i = 0; i < batch_size * label_dim; ++i) h_label[i] = rand() % 3 - 1;
  cudaMemcpy(d_input, h_input, sizeof(float) * batch_size * label_dim, cudaMemcpyHostToDevice);
  cudaMemcpy(d_label, h_label, sizeof(float) * batch_size * label_dim, cudaMemcpyHostToDevice);

  mel.fused_loss_computation(cudaStreamDefault);

  int scaler = 1;
#ifdef SCALE_128
  scaler = 128;
#elif SCALE_256
  scaler = 256;
#elif SCALE_512
  scaler = 512;
#elif SCALE_1024
  scaler = 1024;
#endif

  const float MIN_ = 1e-6;
  float cpu_loss = 0.f;
  for (int i = 0; i < batch_size * label_dim; i++) {
    float x = h_input[i];
    float y = h_label[i];
    float val = 1.f / (1.f + exp(-x));
    int target_weight_idx = i % label_dim;
    float loss = y * log(val + MIN_) + (1.0f - y) * log(1.0f - val + MIN_);
    cpu_loss += (h_label[i] < -0.5) ? 0.f : (target_weight[target_weight_idx] * loss);
    float grad = -1.0f * val * (y - val) * exp(-x) / (1.0f - val + MIN_);
    h_input[i] =
        (h_label[i] < -0.5)
            ? 0.f
            : (target_weight[target_weight_idx] * grad / (batch_size * label_dim) * scaler);

    // if(i == 0){
    //   printf("i=%d, x=%f, y=%f, target_weight[target_weight_idx]=%f, loss=%f, h_input=%f\n", i,
    //   x, y, target_weight[target_weight_idx], loss, h_input[i]);
    // }
  }
  cpu_loss = -cpu_loss / (batch_size * label_dim);
  ASSERT_EQ(true, cpu_gpu_cmp(h_input, d_input, batch_size * label_dim))
      << " CSE Gradient calulation failed" << endl;
  ASSERT_EQ(true, cpu_gpu_cmp(&cpu_loss, d_loss, 1)) << " CSE Loss calulation failed" << endl;
}

TEST(loss_test, MultiCrossEntropyLoss11_1024) { multi_cross_entropy_loss(11, 1024); }
