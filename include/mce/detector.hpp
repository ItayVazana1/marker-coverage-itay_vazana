#pragma once
#include <opencv2/core.hpp>
#include <string>
#include <vector>

namespace mce
{
    // Detects a single 3x3 color marker polygon (quadrilateral) in BGR image.
    // Returns true on success and fills outQuad (image coordinates, clockwise).
    bool detect_marker_polygon(const cv::Mat &bgr,
                               std::vector<cv::Point2f> &outQuad,
                               bool debug = false,
                               bool saveDebug = false,
                               const std::string &debugBase = "");
}