#include "mce/detector.hpp"
#include "mce/log.hpp"

#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <set>
#include <string>
#include <vector>

namespace mce
{

    // ========================= Tunables (global defaults) =========================
    // These are the strong defaults that gave you 8/10. We will adapt per-image
    // inside detect_marker_polygon() without changing these globals.
    struct Params
    {
        // preprocess
        int CLAHE_CLIP = 2;
        int CLAHE_TILE = 8;
        int RESIZE_MAX = 720;

        // HSV mask
        int S_MIN = 28;
        int V_MIN = 25;
        int MORPH_K = 3; // 3×3

        // contour candidates
        int PERIMETER_MIN_MASK = 30;
        int PERIMETER_MIN_EDGES = 35;
        double APPROX_EPS_FRAC = 0.035;

        // edges
        int CANNY_LOW = 30;
        int CANNY_HIGH = 90;
        int DILATE_ITERS = 2;

        // size gates / scoring
        double MIN_QUAD_AREA_FRAC = 0.00006;
        int DENSITY_MIN_PCT = 8;
        double DENSITY_GAIN = 2000.0;
        double SQUARE_PENALTY = 500.0;

        // warp/grid
        int WARP_SIZE = 480;
        double CELL_INSET = 0.10;
        int CELL_MAJ_PCT = 26;
        int VALID_CELLS_REQ = 6;
        int DISTINCT_REQ = 2;

        // second-chance & fallback
        double SECOND_CHANCE_SCALE = 2.0;
        bool ADAPTIVE_FALLBACK = true;
        int ADAPTIVE_BLOCK = 11;
        int ADAPTIVE_C = 2;
        int CANNY2_LOW = 20;
        int CANNY2_HIGH = 60;
        int DILATE2_ITERS = 1;

        // debug
        int TOP_K = 25;
        bool SAVE_CANDIDATES_ON_FAIL = false; // can set true while tuning
        int SAVE_CANDS_LIMIT = 10;
    };
    static Params P;

    // palette indices
    enum Color
    {
        RED = 0,
        YELLOW = 1,
        GREEN = 2,
        CYAN = 3,
        BLUE = 4,
        MAGENTA = 5,
        NONE = 6
    };
    static const char *COLOR_NAME[] = {"R", "Y", "G", "C", "B", "M", "_"};

    // HSV → palette (OpenCV hue 0..179)
    static inline Color classify_hsv_pixel(const cv::Vec3b &hsv)
    {
        const int h = hsv[0], s = hsv[1], v = hsv[2];
        if (s < P.S_MIN || v < P.V_MIN)
            return NONE;

        if (h <= 12 || h >= 170)
            return RED; // wrap
        if (h >= 16 && h <= 42)
            return YELLOW;
        if (h >= 43 && h <= 88)
            return GREEN;
        if (h >= 89 && h <= 102)
            return CYAN;
        if (h >= 103 && h <= 138)
            return BLUE;
        if (h >= 139 && h <= 169)
            return MAGENTA;
        return NONE;
    }

    // Fix -Wmisleading-indentation by splitting returns.
    static void order_quad_tl_tr_br_bl(std::vector<cv::Point2f> &q)
    {
        std::sort(q.begin(), q.end(), [](const cv::Point2f &a, const cv::Point2f &b)
                  {
        if (a.y != b.y)
            return a.y < b.y;
        return a.x < b.x; });
        cv::Point2f tl = (q[0].x <= q[1].x) ? q[0] : q[1];
        cv::Point2f tr = (q[0].x <= q[1].x) ? q[1] : q[0];
        cv::Point2f bl = (q[2].x <= q[3].x) ? q[2] : q[3];
        cv::Point2f br = (q[2].x <= q[3].x) ? q[3] : q[2];
        q = {tl, tr, br, bl};
    }

    static double poly_area(const std::vector<cv::Point2f> &Pp)
    {
        double A = 0;
        int n = (int)Pp.size();
        for (int i = 0; i < n; i++)
        {
            int j = (i + 1) % n;
            A += Pp[i].x * Pp[j].y - Pp[j].x * Pp[i].y;
        }
        return std::abs(A) * 0.5;
    }

    // Build 0/255 mask of “palette-like” pixels in HSV
    static cv::Mat palette_mask_from_hsv(const cv::Mat &hsv)
    {
        cv::Mat mask(hsv.rows, hsv.cols, CV_8U, cv::Scalar(0));
        for (int y = 0; y < hsv.rows; ++y)
        {
            const cv::Vec3b *hr = hsv.ptr<cv::Vec3b>(y);
            uchar *mr = mask.ptr<uchar>(y);
            for (int x = 0; x < hsv.cols; ++x)
                mr[x] = (classify_hsv_pixel(hr[x]) == NONE) ? 0 : 255;
        }
        int ksz = std::max(1, P.MORPH_K);
        if ((ksz & 1) == 0)
            ksz += 1; // ensure odd
        cv::Mat k = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(ksz, ksz));
        cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, k);
        cv::morphologyEx(mask, mask, cv::MORPH_OPEN, k);
        return mask;
    }

    static double mask_density_inside_poly(const cv::Mat &mask, const std::vector<cv::Point> &poly)
    {
        cv::Mat polyMask(mask.size(), CV_8U, cv::Scalar(0));
        std::vector<std::vector<cv::Point>> polys{poly};
        cv::fillPoly(polyMask, polys, cv::Scalar(255));
        int insideTotal = cv::countNonZero(polyMask);
        if (insideTotal == 0)
            return 0.0;

        cv::Mat insideMask;
        cv::bitwise_and(mask, polyMask, insideMask);
        int insideColored = cv::countNonZero(insideMask);
        return 100.0 * double(insideColored) / double(insideTotal);
    }

    // ----- candidate generation A: from palette mask (uses per-call params) -----
    static void find_quads_from_mask(const cv::Mat &mask,
                                     const Params &R,
                                     std::vector<std::vector<cv::Point>> &quads)
    {
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        for (const auto &c : contours)
        {
            double per = cv::arcLength(c, true);
            if (per < R.PERIMETER_MIN_MASK)
                continue;
            std::vector<cv::Point> approx;
            cv::approxPolyDP(c, approx, R.APPROX_EPS_FRAC * per, true);
            if (approx.size() == 4 && cv::isContourConvex(approx))
                quads.push_back(approx);
        }
    }

    // ----- candidate generation B: from edges (uses per-call params) -----
    static void find_quads_from_edges(const cv::Mat &smallBGR,
                                      const Params &R,
                                      std::vector<std::vector<cv::Point>> &quads)
    {
        cv::Mat gray;
        cv::cvtColor(smallBGR, gray, cv::COLOR_BGR2GRAY);
        cv::GaussianBlur(gray, gray, {5, 5}, 1.2);

        cv::Mat edges;
        cv::Canny(gray, edges, R.CANNY_LOW, R.CANNY_HIGH);
        cv::dilate(edges, edges, cv::Mat(), {-1, -1}, R.DILATE_ITERS);

        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(edges, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        for (const auto &c : contours)
        {
            double per = cv::arcLength(c, true);
            if (per < R.PERIMETER_MIN_EDGES)
                continue;
            std::vector<cv::Point> approx;
            cv::approxPolyDP(c, approx, R.APPROX_EPS_FRAC * per, true);
            if (approx.size() == 4 && cv::isContourConvex(approx))
                quads.push_back(approx);
        }
    }

    static bool validate_grid_3x3(const cv::Mat &warpBGR,
                                  int &valid, int &distinct,
                                  std::vector<Color> &cellsOut)
    {
        cv::Mat hsv;
        cv::cvtColor(warpBGR, hsv, cv::COLOR_BGR2HSV);

        const int cellW = P.WARP_SIZE / 3, cellH = P.WARP_SIZE / 3;
        const int insetX = int(P.CELL_INSET * cellW);
        const int insetY = int(P.CELL_INSET * cellH);

        valid = 0;
        std::set<int> kinds;
        cellsOut.assign(9, NONE);

        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
            {
                cv::Rect roi(c * cellW + insetX, r * cellH + insetY,
                             cellW - 2 * insetX, cellH - 2 * insetY);
                roi &= cv::Rect(0, 0, hsv.cols, hsv.rows);
                if (roi.width <= 0 || roi.height <= 0)
                    continue;

                std::array<int, 7> hist{}; // 6 colors + NONE
                for (int y = roi.y; y < roi.y + roi.height; ++y)
                {
                    const cv::Vec3b *pr = hsv.ptr<cv::Vec3b>(y);
                    for (int x = roi.x; x < roi.x + roi.width; ++x)
                        hist[classify_hsv_pixel(pr[x])]++;
                }
                int bestIdx = 6, bestCnt = 0;
                for (int k = 0; k < 7; ++k)
                    if (hist[k] > bestCnt)
                    {
                        bestCnt = hist[k];
                        bestIdx = k;
                    }

                double ratio = 100.0 * double(bestCnt) / double(roi.area());
                if (bestIdx != NONE && ratio >= P.CELL_MAJ_PCT)
                {
                    valid++;
                    kinds.insert(bestIdx);
                    cellsOut[r * 3 + c] = (Color)bestIdx;
                }
                else
                {
                    cellsOut[r * 3 + c] = NONE;
                }
            }
        distinct = (int)kinds.size();
        return (valid >= P.VALID_CELLS_REQ) && (distinct >= P.DISTINCT_REQ);
    }

    bool detect_marker_polygon(const cv::Mat &bgr,
                               std::vector<cv::Point2f> &outQuad,
                               bool debug, bool saveDebug,
                               const std::string &debugBase)
    {
        if (bgr.empty())
            return false;

        // ---- Contrast equalization on V channel (helps glare/low light) ----
        cv::Mat hsv0;
        cv::cvtColor(bgr, hsv0, cv::COLOR_BGR2HSV);
        std::vector<cv::Mat> ch;
        cv::split(hsv0, ch);
        cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE((double)P.CLAHE_CLIP,
                                                   cv::Size(P.CLAHE_TILE, P.CLAHE_TILE));
        clahe->apply(ch[2], ch[2]); // boost V
        cv::merge(ch, hsv0);
        cv::Mat bgr_eq;
        cv::cvtColor(hsv0, bgr_eq, cv::COLOR_HSV2BGR);

        // ---- downscale for speed (keep factor) ----
        cv::Mat small = bgr_eq.clone();
        const int longEdge = std::max(bgr_eq.cols, bgr_eq.rows);
        double scale = (double)P.RESIZE_MAX / (double)longEdge;
        if (scale < 1.0)
            cv::resize(bgr_eq, small, cv::Size(), scale, scale);

        // ---- palette mask on small ----
        cv::Mat hsvSmall;
        cv::cvtColor(small, hsvSmall, cv::COLOR_BGR2HSV);
        cv::Mat palMask = palette_mask_from_hsv(hsvSmall);

        // Save mask upscaled to original size
        if (saveDebug)
        {
            cv::Mat palMaskBig;
            if (scale < 1.0)
                cv::resize(palMask, palMaskBig, bgr.size(), 0, 0, cv::INTER_NEAREST);
            else
                palMaskBig = palMask.clone();
            cv::imwrite(debugBase + "_debug_mask.png", palMaskBig);
        }

        // ---------- Per-image auto-tuning (local copy W) ----------
        Params W = P;

        // Assess how big the color mask is.
        double maskRatio = (double)cv::countNonZero(palMask) /
                           (double)(palMask.cols * palMask.rows);

        bool skipMaskCandidates = false;

        // Heuristic 1: over-segmentation (e.g. warm/yellow scene like 3.png)
        if (maskRatio > 0.25)
        {
            skipMaskCandidates = true;                              // ignore mask contours this frame
            W.APPROX_EPS_FRAC = std::max(W.APPROX_EPS_FRAC, 0.040); // slightly looser quads
            // Keep edges as primary signal; leave Canny as P to avoid over-connecting.
        }

        // Heuristic 2: under-segmentation / glare (like 9.png)
        if (maskRatio < 0.015)
        {
            W.CANNY_LOW = std::min(W.CANNY_LOW, 24);
            W.CANNY_HIGH = std::min(W.CANNY_HIGH, 72);
            W.DILATE_ITERS = 1;
            W.SECOND_CHANCE_SCALE = std::max(W.SECOND_CHANCE_SCALE, 2.5);
            W.ADAPTIVE_FALLBACK = true;
            W.ADAPTIVE_BLOCK = 9;
            W.ADAPTIVE_C = 1;
            W.CANNY2_LOW = 18;
            W.CANNY2_HIGH = 54;
        }
        // -----------------------------------------------------------

        // ---- collect quad candidates from both paths ----
        std::vector<std::vector<cv::Point>> candSmall;
        if (!skipMaskCandidates)
            find_quads_from_mask(palMask, W, candSmall);
        find_quads_from_edges(small, W, candSmall);

        // -- second chance: upscale edges
        if (candSmall.empty() && W.SECOND_CHANCE_SCALE > 1.0)
        {
            cv::Mat small2;
            cv::resize(small, small2, cv::Size(), W.SECOND_CHANCE_SCALE, W.SECOND_CHANCE_SCALE, cv::INTER_LINEAR);
            find_quads_from_edges(small2, W, candSmall);
        }

        // -- adaptive fallback if still empty
        if (candSmall.empty() && W.ADAPTIVE_FALLBACK)
        {
            cv::Mat g, bin, e2;
            cv::cvtColor(small, g, cv::COLOR_BGR2GRAY);
            int blk = (W.ADAPTIVE_BLOCK % 2 == 0) ? W.ADAPTIVE_BLOCK + 1 : W.ADAPTIVE_BLOCK;
            blk = std::max(3, blk);
            cv::adaptiveThreshold(g, bin, 255, cv::ADAPTIVE_THRESH_MEAN_C,
                                  cv::THRESH_BINARY, blk, W.ADAPTIVE_C);
            cv::Canny(bin, e2, W.CANNY2_LOW, W.CANNY2_HIGH);
            cv::dilate(e2, e2, cv::Mat(), {-1, -1}, W.DILATE2_ITERS);

            std::vector<std::vector<cv::Point>> ct;
            cv::findContours(e2, ct, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
            for (const auto &c : ct)
            {
                double per = cv::arcLength(c, true);
                if (per < W.PERIMETER_MIN_EDGES)
                    continue;
                std::vector<cv::Point> approx;
                cv::approxPolyDP(c, approx, W.APPROX_EPS_FRAC * per, true);
                if (approx.size() == 4 && cv::isContourConvex(approx))
                    candSmall.push_back(approx);
            }
        }

        if (candSmall.empty())
        {
            if (debug)
                mce::log::d("No quad candidates after second-chance (and adaptive fallback if enabled)");
            return false;
        }

        // ---- score candidates and try validation (best-first) ----
        const double imgAreaSmall = double(small.cols) * double(small.rows);

        struct Scored
        {
            double score;
            std::vector<cv::Point> poly;
        };
        std::vector<Scored> scored;
        scored.reserve(candSmall.size());

        for (const auto &poly : candSmall)
        {
            double area = std::abs(cv::contourArea(poly));
            if (area < imgAreaSmall * P.MIN_QUAD_AREA_FRAC)
                continue;

            double density = mask_density_inside_poly(palMask, poly); // 0..100
            if (density < P.DENSITY_MIN_PCT)
                continue;

            cv::RotatedRect rr = cv::minAreaRect(poly);
            double w = std::max(1.f, rr.size.width), h = std::max(1.f, rr.size.height);
            double ratio = (w > h) ? w / h : h / w;

            double score = area + P.DENSITY_GAIN * (density / 100.0) - P.SQUARE_PENALTY * std::abs(ratio - 1.0);
            scored.push_back({score, poly});
        }

        if (scored.empty())
        {
            if (saveDebug && P.SAVE_CANDIDATES_ON_FAIL)
            {
                // Draw whatever we had before filtering, up to limit.
                cv::Mat dbg = bgr.clone();
                const double inv = (scale < 1.0) ? (1.0 / scale) : 1.0;
                int limit = 0;
                for (const auto &poly : candSmall)
                {
                    if (++limit > P.SAVE_CANDS_LIMIT)
                        break;
                    std::vector<std::vector<cv::Point>> polys(1);
                    polys[0].reserve(poly.size());
                    for (auto p : poly)
                        polys[0].push_back({int(p.x * inv), int(p.y * inv)});
                    cv::polylines(dbg, polys, true, cv::Scalar(0, 255, 255), 2, cv::LINE_AA);
                }
                cv::imwrite(debugBase + "_debug_candidates.png", dbg);
            }
            return false;
        }

        std::sort(scored.begin(), scored.end(),
                  [](const Scored &a, const Scored &b)
                  { return a.score > b.score; });

        const int K = std::min<int>(P.TOP_K, (int)scored.size());
        for (int k = 0; k < K; ++k)
        {
            const auto &poly = scored[k].poly;

            // map to original coords
            double inv = (scale < 1.0) ? (1.0 / scale) : 1.0;
            std::vector<cv::Point2f> q;
            q.reserve(4);
            for (auto &p : poly)
                q.emplace_back((float)p.x * inv, (float)p.y * inv);
            order_quad_tl_tr_br_bl(q);

            // sanity: area in original
            double A = poly_area(q);
            double imgA = (double)bgr.cols * (double)bgr.rows;
            if (A < imgA * P.MIN_QUAD_AREA_FRAC)
                continue;

            // warp → 3x3 validate
            std::array<cv::Point2f, 4> src{q[0], q[1], q[2], q[3]};
            std::array<cv::Point2f, 4> dst{
                cv::Point2f(0, 0), cv::Point2f(P.WARP_SIZE - 1, 0),
                cv::Point2f(P.WARP_SIZE - 1, P.WARP_SIZE - 1), cv::Point2f(0, P.WARP_SIZE - 1)};
            cv::Mat H = cv::getPerspectiveTransform(src.data(), dst.data());
            cv::Mat warpBGR;
            cv::warpPerspective(bgr, warpBGR, H, {P.WARP_SIZE, P.WARP_SIZE},
                                cv::INTER_LINEAR, cv::BORDER_REPLICATE);

            int valid = 0, distinct = 0;
            std::vector<Color> cells;
            bool ok = validate_grid_3x3(warpBGR, valid, distinct, cells);

            if (debug)
                mce::log::d("cand#" + std::to_string(k) + " score=" + std::to_string(scored[k].score) +
                            " valid=" + std::to_string(valid) + " distinct=" + std::to_string(distinct));

            if (!ok)
                continue;

            // success → save debug & return
            if (saveDebug)
            {
                // original + quad
                cv::Mat dbg = bgr.clone();
                for (int i = 0; i < 4; i++)
                    cv::line(dbg, q[i], q[(i + 1) % 4], {0, 255, 0}, 3, cv::LINE_AA);
                cv::imwrite(debugBase + "_debug_quad.png", dbg);

                // warped + grid + labels
                cv::Mat grid = warpBGR.clone();
                const int cw = P.WARP_SIZE / 3, ch = P.WARP_SIZE / 3;
                const int insetX = int(P.CELL_INSET * cw);
                const int insetY = int(P.CELL_INSET * ch);
                for (int r = 0; r < 3; r++)
                    for (int c = 0; c < 3; c++)
                    {
                        cv::Rect roi(c * cw + insetX, r * ch + insetY, cw - 2 * insetX, ch - 2 * insetY);
                        cv::rectangle(grid, roi, {0, 255, 0}, 2);
                        const char *lab = COLOR_NAME[cells[r * 3 + c]];
                        cv::putText(grid, lab, {roi.x + 8, roi.y + 24},
                                    cv::FONT_HERSHEY_SIMPLEX, 0.7, {0, 0, 255}, 2, cv::LINE_AA);
                    }
                cv::imwrite(debugBase + "_debug_warp.png", grid);
            }

            outQuad = std::move(q);
            return true;
        }

        // If here, no candidate validated. Optionally dump the top candidates.
        if (saveDebug && P.SAVE_CANDIDATES_ON_FAIL)
        {
            cv::Mat dbg = bgr.clone();
            int limit = std::min(P.SAVE_CANDS_LIMIT, (int)scored.size());
            double inv = (scale < 1.0) ? (1.0 / scale) : 1.0;
            for (int i = 0; i < limit; ++i)
            {
                const auto &poly = scored[i].poly;
                std::vector<std::vector<cv::Point>> polys(1);
                polys[0].reserve(poly.size());
                for (auto p : poly)
                    polys[0].push_back({int(p.x * inv), int(p.y * inv)});
                cv::polylines(dbg, polys, true, cv::Scalar(0, 255, 255), 2, cv::LINE_AA);
            }
            cv::imwrite(debugBase + "_debug_candidates.png", dbg);
        }

        return false; // no candidate validated
    }

} // namespace mce
