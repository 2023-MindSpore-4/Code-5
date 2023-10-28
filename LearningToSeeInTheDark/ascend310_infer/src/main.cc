/**
 * Copyright 2021 Huawei Technologies Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <sys/time.h>
#include <gflags/gflags.h>
#include <dirent.h>
#include <iostream>
#include <string>
#include <algorithm>
#include <iosfwd>
#include <vector>
#include <fstream>
#include <sstream>

#include "include/api/types.h"
#include "include/api/model.h"
#include "include/api/serialization.h"
#include "include/api/context.h"
#include "include/dataset/vision_ascend.h"
#include "include/dataset/execute.h"
#include "include/dataset/transforms.h"
#include "include/dataset/vision.h"
#include "inc/utils.h"

using mindspore::dataset::TensorTransform;
using mindspore::Context;
using mindspore::Serialization;
using mindspore::Model;
using mindspore::Status;
using mindspore::ModelType;
using mindspore::Graph;
using mindspore::DataType;
using mindspore::GraphCell;
using mindspore::kSuccess;
using mindspore::MSTensor;
using mindspore::dataset::Execute;

DEFINE_string(mindir_path, "", "mindir path");
DEFINE_string(dataset_path, ".", "dataset path");
DEFINE_int32(device_id, 0, "device id");

int main(int argc, char **argv) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    if (RealPath(FLAGS_mindir_path).empty()) {
        std::cout << "Invalid mindir path" << std::endl;
        return 1;
    }

    auto context = std::make_shared<Context>();
    auto ascend310_info = std::make_shared<mindspore::Ascend310DeviceInfo>();
    ascend310_info->SetDeviceID(FLAGS_device_id);
    context->MutableDeviceInfo().push_back(ascend310_info);

    mindspore::Graph graph;
    Status ret = Serialization::Load(FLAGS_mindir_path, ModelType::kMindIR, &graph);
    if (ret != kSuccess) {
        std::cout << "ERROR: Graph failed." << std::endl;
        return 1;
    }
    Model model;
    ret = model.Build(GraphCell(graph), context);
    if (ret != kSuccess) {
        std::cout << "ERROR: Build failed." << std::endl;
        return 1;
    }

    std::vector<MSTensor> modelInputs = model.GetInputs();

    auto all_files = GetAllFiles(FLAGS_dataset_path);
    if (all_files.empty()) {
        std::cout << "ERROR: No input data." << std::endl;
        return 1;
    }

    std::map<double, double> costTime_map;
    size_t size = all_files.size();

    for (size_t i = 0; i < size; ++i) {
        struct timeval start = {0};
        struct timeval end = {0};
        double startTime_ms;
        double endTime_ms;
        std::vector<MSTensor> inputs;
        std::vector<MSTensor> outputs;
        std::cout << "Start predict input files:" << all_files[i]<< std::endl;

        mindspore::MSTensor input = ReadFileToTensor(all_files[i]);
        inputs.emplace_back(modelInputs[0].Name(), modelInputs[0].DataType(), modelInputs[0].Shape(),
                            input.Data().get(), input.DataSize());

        gettimeofday(&start, NULL);
        ret = model.Predict(inputs, &outputs);
        gettimeofday(&end, NULL);
        if (ret != kSuccess) {
            std::cout << "Predict failed" << std::endl;
        }

        startTime_ms = (1.0 * start.tv_sec * 1000000 + start.tv_usec) / 1000;
        endTime_ms = (1.0 * end.tv_sec * 1000000 + end.tv_usec) / 1000;
        costTime_map.insert(std::pair<double, double>(startTime_ms, endTime_ms));

        WriteResult(all_files[i], outputs);
    }
    double average = 0.0;
    int infer_cnt = 0;
    for (auto iter = costTime_map.begin(); iter != costTime_map.end(); iter++) {
        double diff = 0.0;
        diff = iter->second - iter->first;
        average += diff;
        infer_cnt++;
    }

    average = average / infer_cnt;
    std::stringstream timeCost;
    timeCost << "NN inference cost average time: "<< average << "ms of infer_count " << infer_cnt << std::endl;
    std::cout << "NN inference cost average time: "<< average << "ms of infer_count " << infer_cnt << std::endl;
    std::string file_name = "./time_Result" + std::string("/test_perform_static.txt");
    std::ofstream file_stream(file_name.c_str(), std::ios::trunc);
    file_stream << timeCost.str();
    file_stream.close();
    costTime_map.clear();
  return 0;
}