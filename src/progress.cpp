#include "mce/progress.hpp"
#include "mce/ansi.hpp"
#include "mce/log.hpp"

// unified detection+coverage API
#include "mce/detect_and_compute.hpp"

#include <opencv2/imgcodecs.hpp>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace
{
    fs::path resolve_output_root()
    {
        const char *env = std::getenv("MCE_OUTPUT_ROOT");
        if (env && *env)
            return fs::path(env);
        return fs::current_path() / "mce_output";
    }

    std::string now_stamp()
    {
        using clock = std::chrono::system_clock;
        auto t = clock::to_time_t(clock::now());
        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y%m%d-%H%M%S");
        return oss.str();
    }

    void ensure_dir(const fs::path &p)
    {
        std::error_code ec;
        fs::create_directories(p, ec);
    }

    std::string stem_of(const fs::path &p)
    {
        return p.stem().string();
    }

} // namespace

namespace app::progress
{

    void process_and_report(const std::vector<std::string> &images,
                            const app::State &state)
    {
        using clock = std::chrono::steady_clock;
        using std::chrono::duration_cast;
        using std::chrono::milliseconds;

        mce::log::set(state.debug, state.saveDebug);

        const fs::path root = resolve_output_root();
        const std::string ts = now_stamp();
        const fs::path resultsDir = root / "results";
        const fs::path debugDir = root / "debug" / ts;

        ensure_dir(resultsDir);
        if (state.saveDebug)
            ensure_dir(debugDir);

        const fs::path csvPath = resultsDir / (ts + ".csv");
        std::ofstream csv(csvPath);

        // CSV header: telemetry + all debug artifacts (incl. crop/clip)
        csv << "index,input_path,found,percent,angle_deg,occupancy,hue_score,line_ok,"
               "debug_quad,debug_warp,debug_mask,debug_crop,debug_clip,"
               "elapsed_ms,Smin,Vmin,Vmax\n";

        const int N = static_cast<int>(images.size());
        std::cout << mce::ansi::title << "Running detection on " << N
                  << " image(s)" << mce::ansi::reset << "\n\n";
        std::cout << mce::ansi::muted << "Results CSV: " << csvPath.string()
                  << mce::ansi::reset << "\n";
        if (state.saveDebug)
            std::cout << mce::ansi::muted << "Debug dir : " << debugDir.string()
                      << mce::ansi::reset << "\n";
        std::cout << "\n";

        long long total_ms_accum = 0;
        int foundCount = 0;
        int i = 0;

        auto run_t0 = clock::now();

        for (const auto &path : images)
        {
            ++i;
            std::cout << mce::ansi::muted << "(" << i << "/" << N << ")"
                      << mce::ansi::reset << " Processing: " << path << "\n";

            auto t0 = clock::now();

            cv::Mat img = cv::imread(path, cv::IMREAD_COLOR);
            if (img.empty())
            {
                auto t1 = clock::now();
                long long ms = duration_cast<milliseconds>(t1 - t0).count();
                total_ms_accum += ms;

                std::cout << mce::ansi::err << "Failed to read image"
                          << mce::ansi::reset
                          << mce::ansi::muted << " [" << ms << " ms]"
                          << mce::ansi::reset << "\n";

                // Write a row with the right number of columns (empty fields)
                csv << i << "," << '"' << path << '"' << ",0,,,,," // found..line_ok
                    << ",,,,,"                                     // debug paths (5) incl. crop/clip
                    << ms << ",,,"                                 // elapsed + S/V thresholds
                    << "\n";
                continue;
            }

            // Build debug base under our organized debug dir: .../debug/<ts>/<i>_<name>
            std::string prefix = std::to_string(i) + "_" + stem_of(path);
            fs::path debugBasePath = debugDir / prefix;
            std::string debugBase = debugBasePath.string();

            // Precompute crop/clip paths (we know the suffixes used inside detect_and_compute)
            const std::string cropPath = debugBase + "_debug_crop.png";
            const std::string clipPath = debugBase + "_debug_clip.png";

            // ---- Single call to unified detector+coverage ----
            mce::DetectOutput out;
            bool ok = mce::detect_and_compute(img, out, state.debug, state.saveDebug, debugBase);

            if (ok && out.found)
            {
                // Console line with telemetry
                std::cout << path << "  "
                          << out.coverage_percent << "%  "
                          << mce::ansi::muted
                          << "(angle=" << std::fixed << std::setprecision(1) << out.best_angle_deg
                          << "°, occ=" << std::setprecision(2) << out.occupancy
                          << ", hue=" << std::setprecision(2) << out.hue_score
                          << ", line=" << (out.line_ok ? "ok" : "no") << ")"
                          << mce::ansi::reset << "\n";

                if (state.saveDebug)
                {
                    std::cout << mce::ansi::ok << "        Saved result."
                              << mce::ansi::reset << "\n";
                }
                ++foundCount;
            }
            else
            {
                std::cout << mce::ansi::warn << "No marker found"
                          << mce::ansi::reset << "\n";
            }

            auto t1 = clock::now();
            long long ms = duration_cast<milliseconds>(t1 - t0).count();
            total_ms_accum += ms;

            // inline timing
            std::cout << mce::ansi::muted << "        [" << ms << " ms]"
                      << mce::ansi::reset << "\n";

            // ---- CSV row ----
            if (!ok || !out.found)
            {
                csv << i << "," << '"' << path << '"' << ",0," // index,input,found
                    << ","                                     // percent
                    << ","                                     // angle_deg
                    << ","                                     // occupancy
                    << ","                                     // hue_score
                    << ",";                                    // line_ok

                if (state.saveDebug)
                {
                    csv << "," << "," << "," // debug_quad, debug_warp, debug_mask
                        << "," << ",";       // debug_crop, debug_clip
                }
                else
                {
                    csv << "," << "," << "," << "," << ","; // 5 empty debug columns
                }

                csv << ms << "," // elapsed_ms
                    << out.Smin << "," << out.Vmin << "," << out.Vmax
                    << "\n";
            }
            else
            {
                // found == 1
                csv << i << "," << '"' << path << '"' << ",1,"
                    << out.coverage_percent << ","
                    << std::fixed << std::setprecision(2) << out.best_angle_deg << ","
                    << out.occupancy << ","
                    << out.hue_score << ","
                    << (out.line_ok ? 1 : 0) << ",";

                if (state.saveDebug)
                {
                    csv << '"' << out.debug_quad_path << '"' << ","
                        << '"' << out.debug_warp_path << '"' << ","
                        << '"' << out.debug_mask_path << '"' << ","
                        << '"' << cropPath << '"' << ","
                        << '"' << clipPath << '"';
                }
                else
                {
                    csv << ",,,,"; // five empty debug columns
                }

                csv << "," << ms << ","
                    << out.Smin << ","
                    << out.Vmin << ","
                    << out.Vmax << "\n";
            }
        }

        auto run_t1 = clock::now();
        long long run_ms = duration_cast<milliseconds>(run_t1 - run_t0).count();
        double avg_ms = (N > 0) ? (double)total_ms_accum / (double)N : 0.0;
        double ips = (run_ms > 0) ? (1000.0 * (double)N / (double)run_ms) : 0.0;

        std::cout << "\n"
                  << mce::ansi::bold << "Found " << foundCount << "/"
                  << N << mce::ansi::reset << " images with a valid marker.\n"
                  << mce::ansi::muted
                  << "Total: " << run_ms << " ms, "
                  << "Avg: " << std::fixed << std::setprecision(1) << avg_ms << " ms/img, "
                  << std::setprecision(2) << ips << " img/s"
                  << mce::ansi::reset << "\n\n";
    }

} // namespace app::progress
