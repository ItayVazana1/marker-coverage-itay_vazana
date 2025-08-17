#include <iostream>
#include <vector>
#include <string>
#include <opencv2/imgcodecs.hpp>
#include "mce/detector.hpp"
#include "mce/coverage.hpp"

int main(int argc, char **argv)
{
    bool debug = false, saveDebug = false;
    std::vector<std::string> inputs;
    for (int i = 1; i < argc; i++)
    {
        std::string a = argv[i];
        if (a == "--debug")
            debug = true;
        else if (a == "--save-debug")
            saveDebug = true;
        else
            inputs.push_back(a);
    }
    if (inputs.empty())
    {
        std::cerr << "Usage: marker_coverage [--debug] [--save-debug] IMG [IMG...]\n";
        return 1;
    }

    int notFound = 0;
    for (const auto &path : inputs)
    {
        cv::Mat img = cv::imread(path, cv::IMREAD_COLOR);
        if (img.empty())
        {
            std::cerr << "Failed: " << path << "\n";
            return 1;
        }
        std::vector<cv::Point2f> quad;
        if (!mce::detect_marker_polygon(img, quad, debug, saveDebug, path))
        {
            ++notFound;
            continue; // per spec: no line if not found
        }
        int pct = mce::coverage_percent(quad, img.size());
        std::cout << path << " " << pct << "%\n";
    }
    return (notFound > 0) ? 2 : 0;
}
