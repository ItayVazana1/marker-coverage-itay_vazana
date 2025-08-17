// src/detect_and_compute.cpp — FAST + 5-PATH GRID VALIDATION + ROI warp + OpenMP
#include "mce/detect_and_compute.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <algorithm>
#include <numeric>
#include <vector>
#include <cmath>
#include <iostream>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace mce
{
    namespace
    {

        // ============================== Tunables ==============================
        struct Params
        {
            // Adaptive HSV clamps (computed from percentiles)
            int Smin_floor = 35, Smin_ceil = 80;
            int Vmin_floor = 40, Vmin_ceil = 90;
            int Vmax_floor = 180, Vmax_ceil = 255;

            // Morphology kernel sizing (relative to image size)
            int close_div = 55; // kernel ~ min(H,W)/close_div
            int open_div = 110;

            // Component size filters (fraction of full image)
            double min_comp_frac = 0.0002;
            double max_comp_frac = 0.95;

            // Angle scan (faster coarse sweep, tighter fine sweep)
            int coarse_step_deg = 2;
            int coarse_range_deg = 25; // was 35
            int fine_step_deg = 1;
            int fine_range_deg = 6; // was 10

            // Tightened box validity inside rotated ROI
            double min_occupancy = 0.30;
            double max_aspect = 3.00;

            // 3×3 grid verification (after warp)
            int warpSize = 360; // was 480
            double min_hue_score = 0.25;
            double min_line_peak = 0.12; // was 0.15
            double min_peak_sep = 0.12;  // was 0.15
            double thirds_tol = 0.15;    // ±15% tolerance around 1/3, 2/3

            // Very large rectangles (we don't early-reject; only log)
            double max_quad_area_frac = 0.99;
        };

        // ============================== Utilities ==============================
        static int percentile_u8(const cv::Mat &ch, double p01_99)
        {
            CV_Assert(ch.type() == CV_8U);
            int hist[256] = {0};
            for (int y = 0; y < ch.rows; ++y)
            {
                const uchar *row = ch.ptr<uchar>(y);
                for (int x = 0; x < ch.cols; ++x)
                    hist[row[x]]++;
            }
            const int total = ch.rows * ch.cols;
            const int target = (int)std::round(std::clamp(p01_99, 0.0, 100.0) / 100.0 * total);
            int acc = 0;
            for (int v = 0; v < 256; ++v)
            {
                acc += hist[v];
                if (acc >= target)
                    return v;
            }
            return 255;
        }

        static cv::Mat build_color_mask_adaptive(const cv::Mat &bgr, const Params &P,
                                                 cv::Mat &hsv_out,
                                                 int &Smin, int &Vmin, int &Vmax)
        {
            cv::Mat hsv;
            cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);
            hsv_out = hsv;
            std::vector<cv::Mat> ch;
            cv::split(hsv, ch);
            const cv::Mat &S = ch[1], &V = ch[2];

            Smin = std::clamp(percentile_u8(S, 85.0) - 10, P.Smin_floor, P.Smin_ceil);
            Vmin = std::clamp(percentile_u8(V, 60.0), P.Vmin_floor, P.Vmin_ceil);
            Vmax = std::clamp(percentile_u8(V, 99.0), P.Vmax_floor, P.Vmax_ceil);

            struct Band
            {
                int h1, s1, v1, h2, s2, v2;
            };
            const Band bands[] = {
                {0, Smin, Vmin, 10, 255, Vmax}, {170, Smin, Vmin, 180, 255, Vmax}, // red (wrap)
                {20, Smin, Vmin, 35, 255, Vmax},                                   // yellow
                {40, Smin, Vmin, 85, 255, Vmax},                                   // green
                {86, Smin, Vmin, 100, 255, Vmax},                                  // cyan-ish
                {101, Smin, Vmin, 130, 255, Vmax},                                 // blue
                {131, Smin, Vmin, 169, 255, Vmax},                                 // magenta/purple
            };

            cv::Mat mask;
            for (const auto &b : bands)
            {
                cv::Mat m;
                cv::inRange(hsv, cv::Scalar(b.h1, b.s1, b.v1), cv::Scalar(b.h2, b.s2, b.v2), m);
                if (mask.empty())
                    mask = m;
                else
                    cv::bitwise_or(mask, m, mask);
            }

            int kClose = std::max(3, (std::min(bgr.rows, bgr.cols) / P.close_div) | 1);
            int kOpen = std::max(3, (std::min(bgr.rows, bgr.cols) / P.open_div) | 1);
            cv::morphologyEx(mask, mask, cv::MORPH_CLOSE,
                             cv::getStructuringElement(cv::MORPH_RECT, {kClose, kClose}));
            cv::morphologyEx(mask, mask, cv::MORPH_OPEN,
                             cv::getStructuringElement(cv::MORPH_RECT, {kOpen, kOpen}));
            return mask;
        }

        static bool largest_component(const cv::Mat &mask, cv::Mat &compMask, cv::Rect &bbox)
        {
            cv::Mat labels, stats, centroids;
            int num = cv::connectedComponentsWithStats(mask, labels, stats, centroids, 8);
            if (num <= 1)
                return false;

            int best = -1;
            double bestScore = -1.0;
            for (int i = 1; i < num; ++i)
            {
                int area = stats.at<int>(i, cv::CC_STAT_AREA);
                int w = stats.at<int>(i, cv::CC_STAT_WIDTH);
                int h = stats.at<int>(i, cv::CC_STAT_HEIGHT);
                if (area < 100)
                    continue;
                double ar = (double)std::max(w, h) / std::max(1, std::min(w, h));
                double compact = 1.0 / ar;
                double score = (double)area * compact;
                if (score > bestScore)
                {
                    bestScore = score;
                    best = i;
                }
            }
            if (best < 0)
                return false;

            bbox = cv::Rect(stats.at<int>(best, cv::CC_STAT_LEFT),
                            stats.at<int>(best, cv::CC_STAT_TOP),
                            stats.at<int>(best, cv::CC_STAT_WIDTH),
                            stats.at<int>(best, cv::CC_STAT_HEIGHT));
            compMask = (labels == best);
            compMask.convertTo(compMask, CV_8U, 255);
            return true;
        }

        static bool rotate_and_tighten(const cv::Mat &binMask,
                                       const cv::RotatedRect &rr,
                                       double angle_deg,
                                       cv::RotatedRect &tightRect,
                                       double &occupancy,
                                       cv::Mat *outRotMask = nullptr,
                                       cv::Mat *outROI = nullptr)
        {
            const cv::Point2f center = rr.center;
            cv::Mat M = cv::getRotationMatrix2D(center, angle_deg, 1.0);
            cv::Mat rot;
            cv::warpAffine(binMask, rot, M, binMask.size(), cv::INTER_NEAREST, cv::BORDER_CONSTANT, 0);

            const int RW = std::max(1, (int)std::round(rr.size.width));
            const int RH = std::max(1, (int)std::round(rr.size.height));
            int x0 = (int)std::round(center.x - RW / 2.0);
            int y0 = (int)std::round(center.y - RH / 2.0);
            x0 = std::clamp(x0, 0, rot.cols - 1);
            y0 = std::clamp(y0, 0, rot.rows - 1);
            int x1 = std::clamp(x0 + RW, 0, rot.cols);
            int y1 = std::clamp(y0 + RH, 0, rot.rows);

            cv::Mat roi = rot(cv::Rect(x0, y0, x1 - x0, y1 - y0));
            if (roi.empty())
                return false;

            std::vector<std::vector<cv::Point>> cnts;
            cv::findContours(roi.clone(), cnts, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
            if (cnts.empty())
                return false;
            size_t bestIdx = 0;
            double bestA = 0.0;
            for (size_t i = 0; i < cnts.size(); ++i)
            {
                double a = std::fabs(cv::contourArea(cnts[i]));
                if (a > bestA)
                {
                    bestA = a;
                    bestIdx = i;
                }
            }
            cv::Rect tight = cv::boundingRect(cnts[bestIdx]);
            occupancy = (double)cv::countNonZero(roi) / std::max(1, roi.rows * roi.cols);

            cv::Mat Minv;
            cv::invertAffineTransform(M, Minv);
            cv::Point2f tightCenter((float)(x0 + tight.x + tight.width / 2.0),
                                    (float)(y0 + tight.y + tight.height / 2.0));
            cv::Point2f tightCenterBack(
                (float)(Minv.at<double>(0, 0) * tightCenter.x + Minv.at<double>(0, 1) * tightCenter.y + Minv.at<double>(0, 2)),
                (float)(Minv.at<double>(1, 0) * tightCenter.x + Minv.at<double>(1, 1) * tightCenter.y + Minv.at<double>(1, 2)));

            tightRect = cv::RotatedRect(tightCenterBack,
                                        cv::Size2f((float)tight.width, (float)tight.height),
                                        (float)angle_deg);
            if (outRotMask)
                *outRotMask = rot;
            if (outROI)
                *outROI = roi.clone();
            return true;
        }

        // Order quad points as TL, TR, BR, BL
        static void order_quad_tl_tr_br_bl(const cv::Point2f in[4],
                                           cv::Point2f &tl, cv::Point2f &tr,
                                           cv::Point2f &br, cv::Point2f &bl)
        {
            auto sumf = [](const cv::Point2f &p)
            { return p.x + p.y; };
            auto diff = [](const cv::Point2f &p)
            { return p.x - p.y; };
            tl = *std::min_element(in, in + 4, [&](auto a, auto b)
                                   { return sumf(a) < sumf(b); });
            br = *std::max_element(in, in + 4, [&](auto a, auto b)
                                   { return sumf(a) < sumf(b); });
            tr = *std::max_element(in, in + 4, [&](auto a, auto b)
                                   { return diff(a) < diff(b); });
            bl = *std::min_element(in, in + 4, [&](auto a, auto b)
                                   { return diff(a) < diff(b); });
        }

        // ============================== Common helpers for validators ==============================
        struct GridCheckResult
        {
            double hue_score = 0.0;
            bool line_ok = false;
        };

        static void compute_hue_score(const cv::Mat &warpedBGR, int warpSize, double &hue_score_out)
        {
            cv::Mat hsv;
            cv::cvtColor(warpedBGR, hsv, cv::COLOR_BGR2HSV);
            std::vector<cv::Mat> ch;
            cv::split(hsv, ch);
            const cv::Mat &H = ch[0], &S = ch[1];
            int bins = 18;
            std::vector<int> hist(bins, 0);
            for (int y = 0; y < H.rows; ++y)
            {
                const uchar *hrow = H.ptr<uchar>(y);
                const uchar *srow = S.ptr<uchar>(y);
                for (int x = 0; x < H.cols; ++x)
                    if (srow[x] > 40)
                        hist[hrow[x] * bins / 180]++;
            }
            int thr = std::max(10, (int)std::round(0.002 * warpSize * warpSize));
            int distinct = 0;
            for (int h : hist)
                if (h >= thr)
                    distinct++;
            hue_score_out = std::min(1.0, distinct / 9.0);
        }

        static void smooth5(cv::Mat &p)
        {
            int n = (int)p.total();
            if (n < 5)
                return;
            cv::Mat c = p.clone();
            for (int i = 2; i < n - 2; ++i)
            {
                float s = c.at<float>(i - 2) + c.at<float>(i - 1) + c.at<float>(i) + c.at<float>(i + 1) + c.at<float>(i + 2);
                p.at<float>(i) = s / 5.f;
            }
        }

        static bool two_peaks_prominence(const cv::Mat &proj, double min_prom, double min_sep_frac,
                                         bool anchor_thirds, double tol_frac)
        {
            int n = (int)proj.total();
            if (n < 8)
                return false;
            cv::Mat p32;
            proj.convertTo(p32, CV_32F);
            smooth5(p32);
            double mn, mx;
            cv::minMaxLoc(p32, &mn, &mx);
            if (mx - mn < 1e-6)
                return false;
            p32 = (p32 - mn) / (float)(mx - mn);

            std::vector<float> v(n);
            for (int i = 0; i < n; ++i)
                v[i] = p32.at<float>(i);
            std::nth_element(v.begin(), v.begin() + n / 2, v.end());
            float med = v[n / 2];

            int i1 = -1, i2 = -1;
            float pr1 = -1.f, pr2 = -1.f;
            for (int i = 0; i < n; ++i)
            {
                float pr = p32.at<float>(i) - med;
                if (pr > pr1)
                {
                    pr2 = pr1;
                    i2 = i1;
                    pr1 = pr;
                    i1 = i;
                }
                else if (pr > pr2)
                {
                    pr2 = pr;
                    i2 = i;
                }
            }
            if (i1 < 0 || i2 < 0)
                return false;

            bool strong = (pr1 > min_prom) && (pr2 > min_prom);
            bool sep = (std::abs(i1 - i2) > min_sep_frac * n);

            if (!anchor_thirds)
                return strong && sep;

            float a = n / 3.f, b = 2 * n / 3.f, tol = (float)tol_frac * n;
            bool near_thirds = (std::abs(i1 - a) < tol || std::abs(i1 - b) < tol) &&
                               (std::abs(i2 - a) < tol || std::abs(i2 - b) < tol);
            return strong && sep && near_thirds;
        }

        static cv::Mat strip_barcode_like(const cv::Mat &inBGR)
        {
            // Detect very bright low-sat band at top; if found, crop top ~12%
            cv::Mat hsv;
            cv::cvtColor(inBGR, hsv, cv::COLOR_BGR2HSV);
            std::vector<cv::Mat> ch;
            cv::split(hsv, ch);
            cv::Scalar meanTopV = cv::mean(ch[2](cv::Rect(0, 0, inBGR.cols, std::max(1, inBGR.rows / 10))));
            cv::Scalar meanMidV = cv::mean(ch[2](cv::Rect(0, inBGR.rows / 4, inBGR.cols, std::max(1, inBGR.rows / 2))));
            cv::Scalar meanTopS = cv::mean(ch[1](cv::Rect(0, 0, inBGR.cols, std::max(1, inBGR.rows / 10))));
            if (meanTopV[0] > 1.15 * meanMidV[0] && meanTopS[0] < 60)
            {
                int cut = std::max(1, (int)std::round(0.12 * inBGR.rows));
                return inBGR(cv::Rect(0, cut, inBGR.cols, inBGR.rows - cut)).clone();
            }
            return inBGR;
        }

        // ============================== Validators (5 paths) ==============================
        // 1) LinePeaks + CLAHE (adaptive bin + projections + prominence)
        static bool validator_linepeaks_CLAHE(const cv::Mat &warpedBGR, const Params &P, bool smallMode)
        {
            cv::Mat gray;
            cv::cvtColor(warpedBGR, gray, cv::COLOR_BGR2GRAY);
            if (!smallMode)
            {
                cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(2.0, cv::Size(8, 8));
                clahe->apply(gray, gray);
            }
            cv::GaussianBlur(gray, gray, cv::Size(3, 3), 0.0);

            cv::Mat bin;
            cv::adaptiveThreshold(gray, bin, 255, cv::ADAPTIVE_THRESH_MEAN_C,
                                  cv::THRESH_BINARY_INV, 21, 5);

            cv::dilate(bin, bin, cv::getStructuringElement(cv::MORPH_RECT, {3, 1}), cv::Point(-1, -1), 1);
            cv::dilate(bin, bin, cv::getStructuringElement(cv::MORPH_RECT, {1, 3}), cv::Point(-1, -1), 1);

            cv::Mat px, py;
            cv::reduce(bin, px, 0, cv::REDUCE_SUM, CV_32S);
            cv::reduce(bin, py, 1, cv::REDUCE_SUM, CV_32S);

            double prom = smallMode ? std::min(0.12, P.min_line_peak) : P.min_line_peak;
            double sep = smallMode ? std::min(0.12, P.min_peak_sep) : P.min_peak_sep;

            return two_peaks_prominence(px, prom, sep, /*anchor_thirds*/ true, P.thirds_tol) &&
                   two_peaks_prominence(py, prom, sep, /*anchor_thirds*/ true, P.thirds_tol);
        }

        // 2) ColorGradient + Sobel (Hue on unit circle + V)
        static bool validator_colorgrad_Sobel(const cv::Mat &warpedBGR, const Params &P, bool smallMode)
        {
            (void)smallMode;
            cv::Mat hsv;
            cv::cvtColor(warpedBGR, hsv, cv::COLOR_BGR2HSV);
            std::vector<cv::Mat> ch;
            cv::split(hsv, ch);
            cv::Mat H = ch[0], S = ch[1], V = ch[2];

            cv::Mat Hf;
            H.convertTo(Hf, CV_32F);
            cv::Mat Hrad = Hf * (float)(CV_PI / 180.0f);
            cv::Mat ones = cv::Mat::ones(Hrad.size(), CV_32F);
            cv::Mat Hcos, Hsin;
            cv::polarToCart(ones, Hrad, Hcos, Hsin, /*angleInDegrees*/ false);
            cv::Mat Sf;
            S.convertTo(Sf, CV_32F, 1.0 / 255.0);
            Hcos = Hcos.mul(Sf);
            Hsin = Hsin.mul(Sf);
            cv::Mat Vf;
            V.convertTo(Vf, CV_32F, 1.0 / 255.0);

            auto sobelAbs = [](const cv::Mat &m, bool alongX)
            {
                cv::Mat g;
                if (alongX)
                    cv::Sobel(m, g, CV_32F, 1, 0, 3);
                else
                    cv::Sobel(m, g, CV_32F, 0, 1, 3);
                return cv::abs(g);
            };
            const float alpha = 0.35f;
            cv::Mat gradHx = sobelAbs(Hcos, true) + sobelAbs(Hsin, true) + alpha * sobelAbs(Vf, true);
            cv::Mat gradHy = sobelAbs(Hcos, false) + sobelAbs(Hsin, false) + alpha * sobelAbs(Vf, false);

            cv::Mat px, py;
            cv::reduce(gradHx, px, 0, cv::REDUCE_SUM, CV_32F);
            cv::reduce(gradHy, py, 1, cv::REDUCE_SUM, CV_32F);

            double prom = P.min_line_peak;
            double sep = P.min_peak_sep;

            return two_peaks_prominence(px, prom, sep, /*anchor_thirds*/ true, P.thirds_tol) &&
                   two_peaks_prominence(py, prom, sep, /*anchor_thirds*/ true, P.thirds_tol);
        }

        // 3) MaxGap2Cuts: pick two cuts maximizing profile sum with min-separation
        static bool validator_maxgap_2cuts(const cv::Mat &warpedBGR, const Params &P, bool smallMode)
        {
            cv::Mat gray;
            cv::cvtColor(warpedBGR, gray, cv::COLOR_BGR2GRAY);
            cv::Mat gx, gy;
            cv::Sobel(gray, gx, CV_32F, 1, 0, 3);
            cv::Sobel(gray, gy, CV_32F, 0, 1, 3);
            cv::Mat mag = cv::abs(gx) + cv::abs(gy);

            cv::Mat px, py;
            cv::reduce(mag, px, 0, cv::REDUCE_SUM, CV_32F);
            cv::reduce(mag, py, 1, cv::REDUCE_SUM, CV_32F);
            smooth5(px);
            smooth5(py);

            auto best_pair = [&](const cv::Mat &p, int &a, int &b) -> bool
            {
                int n = (int)p.total();
                if (n < 8)
                    return false;
                int minsep = (int)std::round((smallMode ? std::min(0.12, P.min_peak_sep) : P.min_peak_sep) * n);
                const float *pp = p.ptr<float>();
                float best = -1.f;
                a = -1;
                b = -1;
                for (int i = 0; i < n; ++i)
                {
                    for (int j = i + minsep; j < n; ++j)
                    {
                        float s = pp[i] + pp[j];
                        if (s > best)
                        {
                            best = s;
                            a = i;
                            b = j;
                        }
                    }
                }
                return (a >= 0 && b >= 0);
            };

            int ix1, ix2, iy1, iy2;
            if (!best_pair(px, ix1, ix2))
                return false;
            if (!best_pair(py, iy1, iy2))
                return false;

            auto near_thirds_ok = [&](int n, int i1, int i2) -> bool
            {
                float a = n / 3.f, b = 2 * n / 3.f, tol = (float)P.thirds_tol * n;
                return ((std::abs(i1 - a) < tol || std::abs(i1 - b) < tol) &&
                        (std::abs(i2 - a) < tol || std::abs(i2 - b) < tol));
            };

            bool okX = near_thirds_ok((int)px.total(), ix1, ix2);
            bool okY = near_thirds_ok((int)py.total(), iy1, iy2);
            return okX && okY;
        }

        // 4) KMeans Color (K=6) on subsample + check label transitions near thirds
        static bool validator_kmeans_color(const cv::Mat &warpedBGR, const Params &P, bool smallMode)
        {
            int stride = smallMode ? 8 : 6; // faster
            int rows = warpedBGR.rows, cols = warpedBGR.cols;
            int nsamp = (rows / stride) * (cols / stride);
            if (nsamp < 64)
                return false;

            // Build feature: [Hcos,Hsin,S,V]
            cv::Mat hsv;
            cv::cvtColor(warpedBGR, hsv, cv::COLOR_BGR2HSV);
            std::vector<cv::Mat> ch;
            cv::split(hsv, ch);
            cv::Mat H = ch[0], S = ch[1], V = ch[2];
            cv::Mat Hf;
            H.convertTo(Hf, CV_32F);
            cv::Mat Hrad = Hf * (float)(CV_PI / 180.0f);
            cv::Mat ones = cv::Mat::ones(Hrad.size(), CV_32F);
            cv::Mat Hcos, Hsin;
            cv::polarToCart(ones, Hrad, Hcos, Hsin, false);
            cv::Mat Sf, Vf;
            S.convertTo(Sf, CV_32F, 1.0 / 255.0);
            V.convertTo(Vf, CV_32F, 1.0 / 255.0);

            cv::Mat samples(nsamp, 4, CV_32F);
            int r = 0;
            for (int y = 0; y < rows; y += stride)
            {
                for (int x = 0; x < cols; x += stride)
                {
                    samples.at<float>(r, 0) = Hcos.at<float>(y, x) * (Sf.at<float>(y, x));
                    samples.at<float>(r, 1) = Hsin.at<float>(y, x) * (Sf.at<float>(y, x));
                    samples.at<float>(r, 2) = Sf.at<float>(y, x);
                    samples.at<float>(r, 3) = Vf.at<float>(y, x);
                    ++r;
                }
            }

            int K = 6;
            cv::Mat labels, centers;
            cv::kmeans(samples, K, labels,
                       cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::MAX_ITER, 10, 1e-3),
                       1, cv::KMEANS_PP_CENTERS, centers);

            if (centers.rows < 5)
                return false;

            // Map labels back to full grid at stride positions
            cv::Mat lbl(rows / stride, cols / stride, CV_32S);
            r = 0;
            for (int y = 0; y < lbl.rows; ++y)
                for (int x = 0; x < lbl.cols; ++x)
                    lbl.at<int>(y, x) = labels.at<int>(r++);

            auto transitions_near_thirds = [&](const cv::Mat &L, bool alongX) -> bool
            {
                if (alongX)
                {
                    int y = L.rows / 2;
                    int n = L.cols;
                    int last = L.at<int>(y, 0);
                    std::vector<int> pos;
                    for (int x = 1; x < n; ++x)
                    {
                        int cur = L.at<int>(y, x);
                        if (cur != last)
                        {
                            pos.push_back(x);
                            last = cur;
                        }
                    }
                    float a = n / 3.f, b = 2 * n / 3.f, tol = (float)P.thirds_tol * n;
                    bool hitA = false, hitB = false;
                    for (int p : pos)
                    {
                        if (std::abs(p - a) < tol)
                            hitA = true;
                        if (std::abs(p - b) < tol)
                            hitB = true;
                    }
                    return hitA && hitB;
                }
                else
                {
                    int x = L.cols / 2;
                    int n = L.rows;
                    int last = L.at<int>(0, x);
                    std::vector<int> pos;
                    for (int y = 1; y < n; ++y)
                    {
                        int cur = L.at<int>(y, x);
                        if (cur != last)
                        {
                            pos.push_back(y);
                            last = cur;
                        }
                    }
                    float a = n / 3.f, b = 2 * n / 3.f, tol = (float)P.thirds_tol * n;
                    bool hitA = false, hitB = false;
                    for (int p : pos)
                    {
                        if (std::abs(p - a) < tol)
                            hitA = true;
                        if (std::abs(p - b) < tol)
                            hitB = true;
                    }
                    return hitA && hitB;
                }
            };

            return transitions_near_thirds(lbl, /*alongX*/ true) &&
                   transitions_near_thirds(lbl, /*alongX*/ false);
        }

        // 5) Template correlation against ideal 3×3 edge map (normalized)
        static bool validator_template_corr(const cv::Mat &warpedBGR, const Params & /*P*/, bool /*smallMode*/)
        {
            cv::Mat gray;
            cv::cvtColor(warpedBGR, gray, cv::COLOR_BGR2GRAY);
            cv::Mat gx, gy;
            cv::Sobel(gray, gx, CV_32F, 1, 0, 3);
            cv::Sobel(gray, gy, CV_32F, 0, 1, 3);
            cv::Mat mag = cv::abs(gx) + cv::abs(gy);
            cv::normalize(mag, mag, 0, 1, cv::NORM_MINMAX);

            // Build template with grid lines at 1/3 and 2/3 (thickness 2)
            cv::Mat templ = cv::Mat::zeros(mag.size(), CV_32F);
            int W = templ.cols, H = templ.rows;
            auto draw_v = [&](int x)
            { cv::line(templ, {x, 0}, {x, H - 1}, 1.0f, 2, cv::LINE_AA); };
            auto draw_h = [&](int y)
            { cv::line(templ, {0, y}, {W - 1, y}, 1.0f, 2, cv::LINE_AA); };
            draw_v(W / 3);
            draw_v(2 * W / 3);
            draw_h(H / 3);
            draw_h(2 * H / 3);

            // Single-value normalized correlation
            cv::Mat res;
            cv::matchTemplate(mag, templ, res, cv::TM_CCOEFF_NORMED);
            double minv, maxv;
            cv::minMaxLoc(res, &minv, &maxv);
            return maxv > 0.25; // threshold
        }

        // Master: run cascade of 5 validators
        static void grid_checks_cascade(const cv::Mat &warpedBGR,
                                        GridCheckResult &out,
                                        const Params &P)
        {
            // Hue richness once
            compute_hue_score(warpedBGR, P.warpSize, out.hue_score);
            // Pre-strip barcode/glare if exists
            cv::Mat W = strip_barcode_like(warpedBGR);

            bool smallMode = std::min(W.rows, W.cols) < 60;

            if (validator_linepeaks_CLAHE(W, P, smallMode))
            {
                out.line_ok = true;
                return;
            }
            if (validator_colorgrad_Sobel(W, P, smallMode))
            {
                out.line_ok = true;
                return;
            }
            if (validator_maxgap_2cuts(W, P, smallMode))
            {
                out.line_ok = true;
                return;
            }
            if (validator_kmeans_color(W, P, smallMode))
            {
                out.line_ok = true;
                return;
            }
            out.line_ok = validator_template_corr(W, P, smallMode);
        }

        // ============================== Drawing ==============================
        static void draw_box(cv::Mat &img, const cv::RotatedRect &rr, int pct)
        {
            cv::Point2f ptsf[4];
            rr.points(ptsf);
            std::vector<cv::Point> poly(4);
            for (int i = 0; i < 4; ++i)
                poly[i] = ptsf[i];
            cv::polylines(img, poly, true, {0, 255, 0}, 3, cv::LINE_AA);
            if (pct >= 0)
            {
                cv::putText(img, cv::format("Coverage: %d%%", pct), {20, 40},
                            cv::FONT_HERSHEY_SIMPLEX, 1.0, {0, 255, 0}, 2, cv::LINE_AA);
            }
        }

    } // namespace (anon)

    // ============================== Public API ==============================
    bool detect_and_compute(const cv::Mat &bgr,
                            DetectOutput &out,
                            bool debug,
                            bool saveDebug,
                            const std::string &debugBase)
    {
        out = DetectOutput{};
        if (bgr.empty())
            return true;

        Params P;

        // (1) Adaptive color mask
        cv::Mat hsv;
        int Smin = 0, Vmin = 0, Vmax = 255;
        cv::Mat mask = build_color_mask_adaptive(bgr, P, hsv, Smin, Vmin, Vmax);
        out.Smin = Smin;
        out.Vmin = Vmin;
        out.Vmax = Vmax;
        if (saveDebug)
        {
            out.debug_mask_path = debugBase + "_debug_mask.png";
            cv::imwrite(out.debug_mask_path, mask);
        }

        // (2) Best connected component
        cv::Mat comp;
        cv::Rect compBox;
        if (!largest_component(mask, comp, compBox))
        {
            if (debug)
                std::cout << "[DBG] No component\n";
            return true;
        }

        double compFrac = (double)cv::countNonZero(comp) / std::max(1, bgr.rows * bgr.cols);
        if (debug)
            std::cout << "[DBG] compFrac=" << compFrac
                      << " (min=" << P.min_comp_frac << ", max=" << P.max_comp_frac << ")\n";
        if (compFrac < P.min_comp_frac || compFrac > P.max_comp_frac)
        {
            if (debug)
                std::cout << "[DBG] Component frac out of range: " << compFrac << "\n";
            return true;
        }

        // (3) Base orientation
        std::vector<std::vector<cv::Point>> cnts;
        cv::findContours(comp, cnts, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        if (cnts.empty())
            return true;
        cv::RotatedRect rr = cv::minAreaRect(cnts[0]);
        double baseAngle = rr.angle;

        double baseArea = rr.size.width * rr.size.height;
        double baseFrac = baseArea / (double)(bgr.cols * bgr.rows);
        if (debug)
            std::cout << "[DBG] baseFrac=" << baseFrac
                      << " (max_quad_area_frac=" << P.max_quad_area_frac << ")\n";
        if (baseFrac > P.max_quad_area_frac && debug)
            std::cout << "[DBG] Base rect very large; continuing with scan anyway\n";

        // (4) Angle scan: coarse→fine, keep best (OpenMP)
        struct Best
        {
            double angle = 0, cov = 0, occ = 0, hue = 0;
            bool line_ok = false;
            cv::RotatedRect tight;
        } best;
        bool earlyStop = false;

        auto evaluate_angle = [&](double ang, Best &localBest)
        {
            cv::RotatedRect tight;
            double occ = 0.0;
            if (!rotate_and_tighten(comp, rr, ang, tight, occ))
                return;

            double w = tight.size.width, h = tight.size.height;
            if (w <= 0 || h <= 0)
                return;
            double ar = std::max(w, h) / std::max(1.0, std::min(w, h));
            if (occ < P.min_occupancy || ar > P.max_aspect)
                return;

            // --- ROI crop סביב ה-tightRect על המקור:
            cv::Point2f tpts[4];
            tight.points(tpts);
            cv::Point2f TL, TR, BR, BL;
            order_quad_tl_tr_br_bl(tpts, TL, TR, BR, BL);
            std::vector<cv::Point2f> src = {TL, TR, BR, BL};

            cv::Rect fullRoi = cv::boundingRect(src);
            int pad = std::max(2, (int)std::round(0.10 * std::max(fullRoi.width, fullRoi.height)));
            fullRoi.x = std::max(0, fullRoi.x - pad);
            fullRoi.y = std::max(0, fullRoi.y - pad);
            fullRoi.width = std::min(bgr.cols - fullRoi.x, fullRoi.width + 2 * pad);
            fullRoi.height = std::min(bgr.rows - fullRoi.y, fullRoi.height + 2 * pad);

            // הזז את הנקודות לקואורדינטות של ה-ROI
            std::vector<cv::Point2f> srcR(4);
            for (int i = 0; i < 4; ++i)
                srcR[i] = cv::Point2f(src[i].x - fullRoi.x, src[i].y - fullRoi.y);

            cv::Mat roiBGR = bgr(fullRoi);
            std::vector<cv::Point2f> dst = {{0, 0}, {(float)P.warpSize - 1, 0}, {(float)P.warpSize - 1, (float)P.warpSize - 1}, {0, (float)P.warpSize - 1}};
            cv::Mat H = cv::getPerspectiveTransform(srcR, dst);
            cv::Mat warped;
            cv::warpPerspective(roiBGR, warped, H, cv::Size(P.warpSize, P.warpSize));

            // 5-path cascade
            GridCheckResult gcr;
            grid_checks_cascade(warped, gcr, P);
            if (gcr.hue_score < P.min_hue_score || !gcr.line_ok)
                return;

            double area = w * h;
            double cov = 100.0 * area / (double)(bgr.cols * bgr.rows);
            double score = occ * (0.5 + 0.5 * gcr.hue_score);

            double bestScore = localBest.occ * (0.5 + 0.5 * localBest.hue);
            if (score > bestScore)
            {
                localBest.angle = ang;
                localBest.cov = cov;
                localBest.occ = occ;
                localBest.hue = gcr.hue_score;
                localBest.line_ok = gcr.line_ok;
                localBest.tight = tight;
            }
        };

        auto scan = [&](int stepDeg, int rangeDeg) -> bool
        {
            // נכין את רשימת הזוויות מראש
            std::vector<int> deltas;
            for (int d = -rangeDeg; d <= rangeDeg; d += stepDeg)
                deltas.push_back(d);

            // Best מקומי לכל ת’רד
            std::vector<Best> locals(
#ifdef _OPENMP
                std::max(1, omp_get_max_threads())
#else
                1
#endif
            );

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
            for (int i = 0; i < (int)deltas.size(); ++i)
            {
#ifdef _OPENMP
                if (earlyStop)
                    continue; // בדיקה רופפת
                int tid = omp_get_thread_num();
#else
                int tid = 0;
#endif
                double ang = (best.cov > 0.0 ? best.angle : baseAngle) + deltas[i];
                evaluate_angle(ang, locals[tid]);
            }

            // מיזוג best מקומי לגלובלי
            for (const auto &lb : locals)
            {
                double sLb = lb.occ * (0.5 + 0.5 * lb.hue);
                double sGl = best.occ * (0.5 + 0.5 * best.hue);
                if (sLb > sGl)
                    best = lb;
            }

            // early stop if we have a good enough result
            if (best.occ > 0.78 && best.hue > 0.85 && best.line_ok)
            {
                earlyStop = true;
                return true;
            }
            return false;
        };

        if (!scan(P.coarse_step_deg, P.coarse_range_deg))
            scan(P.fine_step_deg, P.fine_range_deg);

        if (best.cov <= 0.0)
        {
            if (debug)
            {
                std::cout << "[DBG] No angle passed validation\n"
                          << "      (trying direct warp from minAreaRect as fallback)\n";
            }
            // Fallback: warp rr as-is
            cv::Point2f rrPts[4];
            rr.points(rrPts);
            cv::Point2f TL, TR, BR, BL;
            order_quad_tl_tr_br_bl(rrPts, TL, TR, BR, BL);
            std::vector<cv::Point2f> src2 = {TL, TR, BR, BL};
            std::vector<cv::Point2f> dst2 = {{0, 0}, {(float)P.warpSize - 1, 0}, {(float)P.warpSize - 1, (float)P.warpSize - 1}, {0, (float)P.warpSize - 1}};

            // ROI Fallback: crop around the minAreaRect
            cv::Rect fullRoi = cv::boundingRect(src2);
            int pad = std::max(2, (int)std::round(0.10 * std::max(fullRoi.width, fullRoi.height)));
            fullRoi.x = std::max(0, fullRoi.x - pad);
            fullRoi.y = std::max(0, fullRoi.y - pad);
            fullRoi.width = std::min(bgr.cols - fullRoi.x, fullRoi.width + 2 * pad);
            fullRoi.height = std::min(bgr.rows - fullRoi.y, fullRoi.height + 2 * pad);

            std::vector<cv::Point2f> src2R(4);
            for (int i = 0; i < 4; ++i)
                src2R[i] = cv::Point2f(src2[i].x - fullRoi.x, src2[i].y - fullRoi.y);

            cv::Mat roiBGR = bgr(fullRoi);
            cv::Mat H2 = cv::getPerspectiveTransform(src2R, dst2);
            cv::Mat warped2;
            cv::warpPerspective(roiBGR, warped2, H2, cv::Size(P.warpSize, P.warpSize));

            GridCheckResult gcr2;
            grid_checks_cascade(warped2, gcr2, P);
            if (gcr2.hue_score >= P.min_hue_score && gcr2.line_ok)
            {
                double cov2 = 100.0 * (rr.size.width * rr.size.height) / (double)(bgr.cols * bgr.rows);
                int pct2 = (int)std::lround(std::clamp(cov2, 0.0, 100.0));
                out.coverage_percent = pct2;
                out.found = true;
                out.best_angle_deg = baseAngle;
                out.occupancy = 1.0;
                out.hue_score = gcr2.hue_score;
                out.line_ok = true;

                if (saveDebug)
                {
                    out.debug_warp_path = debugBase + "_debug_warp.png";
                    cv::imwrite(out.debug_warp_path, warped2);
                    cv::Mat vis = bgr.clone();
                    draw_box(vis, rr, pct2);
                    out.debug_quad_path = debugBase + "_debug_quad.png";
                    cv::imwrite(out.debug_quad_path, vis);
                }
                out.quad = {TL, TR, BR, BL};
                return true;
            }
            if (debug)
                std::cout << "[DBG] Fallback also failed (hue=" << gcr2.hue_score << ", line=no)\n";
            return true;
        }

        // (5) Emit result
        int pct = (int)std::lround(std::clamp(best.cov, 0.0, 100.0));
        out.coverage_percent = pct;
        out.found = true;

        out.best_angle_deg = best.angle;
        out.occupancy = best.occ;
        out.hue_score = best.hue;
        out.line_ok = best.line_ok;

        if (saveDebug)
        {
            cv::Mat vis = bgr.clone();
            draw_box(vis, best.tight, pct);
            out.debug_quad_path = debugBase + "_debug_quad.png";
            cv::imwrite(out.debug_quad_path, vis);

            // perspective-corrected crop (natural size)
            cv::Point2f pts[4];
            best.tight.points(pts);
            cv::Point2f TL, TR, BR, BL;
            order_quad_tl_tr_br_bl(pts, TL, TR, BR, BL);
            int dstW = std::max(20, (int)std::lround(best.tight.size.width));
            int dstH = std::max(20, (int)std::lround(best.tight.size.height));
            std::vector<cv::Point2f> srcVec = {TL, TR, BR, BL};
            std::vector<cv::Point2f> dst = {{0.0f, 0.0f}, {(float)dstW - 1, 0.0f}, {(float)dstW - 1, (float)dstH - 1}, {0.0f, (float)dstH - 1}};
            cv::Mat Hnat = cv::getPerspectiveTransform(srcVec, dst);
            cv::Mat crop;
            cv::warpPerspective(bgr, crop, Hnat, cv::Size(dstW, dstH), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
            out.debug_crop_path = debugBase + "_debug_crop.png";
            cv::imwrite(out.debug_crop_path, crop);

            cv::Mat polyMask = cv::Mat::zeros(bgr.size(), CV_8U);
            std::vector<cv::Point> q = {
                cv::Point((int)std::lround(TL.x), (int)std::lround(TL.y)),
                cv::Point((int)std::lround(TR.x), (int)std::lround(TR.y)),
                cv::Point((int)std::lround(BR.x), (int)std::lround(BR.y)),
                cv::Point((int)std::lround(BL.x), (int)std::lround(BL.y))};
            cv::fillConvexPoly(polyMask, q, 255);
            cv::Mat clipped;
            bgr.copyTo(clipped, polyMask);
            out.debug_clip_path = debugBase + "_debug_clip.png";
            cv::imwrite(out.debug_clip_path, clipped);
        }

        cv::Point2f pts[4];
        best.tight.points(pts);
        cv::Point2f TL, TR, BR, BL;
        order_quad_tl_tr_br_bl(pts, TL, TR, BR, BL);
        out.quad = {TL, TR, BR, BL};

        return true;
    }

} // namespace mce
