#pragma once
#include <opencv2/core.hpp>
#include <vector>

namespace mce
{
    inline double polygon_area(const std::vector<cv::Point2f> &P)
    {
        double A = 0;
        int n = (int)P.size();
        for (int i = 0; i < n; i++)
        {
            int j = (i + 1) % n;
            A += P[i].x * P[j].y - P[j].x * P[i].y;
        }
        return std::abs(A) * 0.5;
    }
    inline int coverage_percent(const std::vector<cv::Point2f> &quad, const cv::Size &sz)
    {
        double areaQ = polygon_area(quad);
        double areaI = (double)sz.width * sz.height;
        return (int)std::lround(100.0 * areaQ / areaI);
    }
}