//==============================================================================
// Copyright (c) 2019, Peter Weissig, TU Chemnitz
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//     * Neither the name of the Flight Systems and Automatic Control group,
//       TU Darmstadt, nor the names of its contributors may be used to
//       endorse or promote products derived from this software without
//       specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//==============================================================================

// This file is an addon from TUC-ProAut (https://github.com/TUC-ProAut/).
// It was created to log all data from the ublox to a logfile and to publish
// the data as ros messages. This is used by our group to also evaluate the
// measured data with the rtklib.

#include <algorithm>
#include <cmath>
#include <ctime>
#include <string>
#include <sstream>

#include <sys/types.h>
#include <sys/stat.h>

#include <rclcpp/rclcpp.hpp>

#include <std_msgs/msg/multi_array_dimension.hpp>
#include <std_msgs/msg/u_int8_multi_array.hpp>

#include <ublox_gps/raw_data_pa.hpp>

//
// ublox_node namespace
//

namespace ublox_node {

RawDataStreamPa::RawDataStreamPa(bool is_ros_subscriber) : rclcpp::Node("raw_data_pa"),
  flag_publish_(false),
  is_ros_subscriber_(is_ros_subscriber) {

  raw_pub_ = this->create_publisher<std_msgs::msg::UInt8MultiArray>("~/raw_data_stream", 100);

  this->declare_parameter("dir", "");
  this->declare_parameter("raw_data_stream.dir", "");
  this->declare_parameter("raw_data_stream.publish", false);
}

void RawDataStreamPa::getRosParams() {

  if (is_ros_subscriber_) {
    file_dir_ = this->get_parameter("dir").get_value<std::string>();
  } else {
    file_dir_ = this->get_parameter("raw_data_stream.dir").get_value<std::string>();
    flag_publish_ = this->get_parameter("raw_data_stream.publish").get_value<bool>();
  }
}

bool RawDataStreamPa::isEnabled() {

  if (is_ros_subscriber_) {
    return !file_dir_.empty();
  }

  return flag_publish_ || !file_dir_.empty();
}

void RawDataStreamPa::initialize() {

  if (is_ros_subscriber_) {
    RCLCPP_INFO(this->get_logger(), "Subscribing to raw data stream.");
    raw_data_stream_sub_ =
        this->create_subscription<std_msgs::msg::UInt8MultiArray>("raw_data_stream", rclcpp::QoS(100),
          std::bind(&RawDataStreamPa::msgCallback, this, std::placeholders::_1));
  } else if (flag_publish_) {
    RCLCPP_INFO(this->get_logger(), "Publishing raw data stream.");
    RawDataStreamPa::publishMsg(std::string());
  }

  if (!file_dir_.empty()) {
    struct stat stat_info{};
    if (::stat(file_dir_.c_str(), &stat_info) != 0) {
      RCLCPP_ERROR(this->get_logger(), "Can't log raw data to file. "
                   "Directory \"%s\" does not exist.", file_dir_.c_str());

    } else if ((stat_info.st_mode & S_IFDIR) != S_IFDIR) {
      RCLCPP_ERROR(this->get_logger(), "Can't log raw data to file. "
                   "\"%s\" exists, but is not a directory.", file_dir_.c_str());

    } else {
      if (file_dir_.back() != '/') {
        file_dir_ += '/';
      }

      time_t t = time(nullptr);
      struct tm time_struct = *localtime(&t);

      std::stringstream filename;
      filename.width(4); filename.fill('0');
      filename << time_struct.tm_year + 1900;
      filename.width(0); filename << '_';
      filename.width(2); filename.fill('0');
      filename << time_struct.tm_mon  + 1;
      filename.width(0); filename << '_';
      filename.width(2); filename.fill('0');
      filename << time_struct.tm_mday;
      filename.width(0); filename << '_';
      filename.width(2); filename.fill('0');
      filename << time_struct.tm_hour;
      filename.width(2); filename.fill('0');
      filename << time_struct.tm_min ;
      filename.width(0); filename << ".log";
      file_name_ = file_dir_ + filename.str();

      try {
        file_handle_.open(file_name_);
        RCLCPP_INFO(this->get_logger(), "Logging raw data to file \"%s\"",
                    file_name_.c_str());
      } catch (const std::exception& e) {
        RCLCPP_ERROR(this->get_logger(), "Can't log raw data to file. "
                     "Can't create file \"%s\".", file_name_.c_str());
      }
    }
  }
}

void RawDataStreamPa::ubloxCallback(const unsigned char* data,
  std::size_t size) {

  std::string str(reinterpret_cast<const char*>(data), size);

  if (flag_publish_) {
    publishMsg(str);
  }

  saveToFile(str);
}

void RawDataStreamPa::msgCallback(
  const std_msgs::msg::UInt8MultiArray::SharedPtr msg) {

  std::string str(msg->data.size(), ' ');
  std::copy(msg->data.begin(), msg->data.end(), str.begin());
  saveToFile(str);
}

std_msgs::msg::UInt8MultiArray RawDataStreamPa::str2uint8(
 const std::string & str) {

  std_msgs::msg::UInt8MultiArray msg;

  msg.layout.data_offset = 0;
  msg.layout.dim.push_back(std_msgs::msg::MultiArrayDimension());
  msg.layout.dim[0].size   = str.length();
  msg.layout.dim[0].stride = 1;
  msg.layout.dim[0].label  = "raw_data_stream";

  msg.data.resize(str.length());
  std::copy(str.begin(), str.end(), msg.data.begin());

  return msg;
}

void RawDataStreamPa::publishMsg(const std::string & str) {

  raw_pub_->publish(RawDataStreamPa::str2uint8(str));
}

void RawDataStreamPa::saveToFile(const std::string & str) {

  if (file_handle_.is_open()) {
    try {
      file_handle_ << str;
      // file_handle_.flush();
    } catch (const std::exception& e) {
      RCLCPP_WARN(this->get_logger(), "Error writing to file \"%s\"", file_name_.c_str());
    }
  }
}

}  // namespace ublox_node
