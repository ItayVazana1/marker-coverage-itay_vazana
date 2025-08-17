#pragma once
#include <opencv2/core.hpp>
#include <string>
#include <vector>

namespace mce
{
    struct DetectOutput
    {
        // final decision
        bool found = false;

        // final quadrilateral (ordered TL,TR,BR,BL)
        std::vector<cv::Point2f> quad;

        // final coverage percentage [0..100], -1 if not found
        int coverage_percent = -1;

        // telemetry (for CSV / debugging)
        double best_angle_deg = 0.0;        // chosen angle after scan
        double occupancy = 0.0;             // mask occupancy inside tightened ROI
        double hue_score = 0.0;             // 0..1 richness of hues after warp
        bool line_ok = false;               // grid divisions detected after warp
        int Smin = 0, Vmin = 0, Vmax = 255; // adaptive HSV thresholds used

        // debug artifact paths (written only when saveDebug=true)
        std::string debug_quad_path; // original image + green box + % text
        std::string debug_warp_path; // canonical warp for grid checks
        std::string debug_mask_path; // adaptive HSV mask (post-morphology)

        // NEW: perspective-corrected crop and clipped original
        std::string debug_crop_path; // "<debugBase>_debug_crop.png"
        std::string debug_clip_path; // "<debugBase>_debug_clip.png"
    };

    // Unified detection+coverage API
    bool detect_and_compute(const cv::Mat &bgr,
                            DetectOutput &out,
                            bool debug,
                            bool saveDebug,
                            const std::string &debugBase);
} // namespace mce
