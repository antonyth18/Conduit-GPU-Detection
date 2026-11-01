#include <stdexcept>

#include "conduit/gpu.hpp"

namespace conduit {

GpuBackend parse_gpu_backend(const std::string& s) {
    if (s == "cuda") return GpuBackend::Cuda;
    if (s == "emu") return GpuBackend::Emu;
    throw std::runtime_error("unknown gpu backend " + s + " (cuda | emu)");
}

DetectMode parse_detect(const std::string& s) {
    if (s == "group") return DetectMode::Group;
    if (s == "balanced") return DetectMode::Balanced;
    throw std::runtime_error("unknown detect mode " + s + " (group | balanced)");
}

GpuLevel parse_gpu_level(const std::string& s) {
    if (s == "sweep") return GpuLevel::Sweep;
    if (s == "kahn") return GpuLevel::Kahn;
    if (s == "jp-index" || s == "jp") return GpuLevel::JpIndex;
    if (s == "jp-random") return GpuLevel::JpRandom;
    throw std::runtime_error("unknown gpu level " + s + " (sweep | kahn | jp-index | jp-random)");
}

const char* gpu_backend_name(GpuBackend b) { return b == GpuBackend::Cuda ? "cuda" : "emu"; }
const char* detect_name(DetectMode d) { return d == DetectMode::Group ? "group" : "balanced"; }
const char* gpu_level_name(GpuLevel l) {
    switch (l) {
        case GpuLevel::Sweep: return "sweep";
        case GpuLevel::Kahn: return "kahn";
        case GpuLevel::JpIndex: return "jp-index";
        case GpuLevel::JpRandom: return "jp-random";
    }
    return "?";
}

#ifndef CONDUIT_HAVE_CUDA
bool have_cuda() { return false; }
std::string cuda_device_info() { return "built without CUDA (CONDUIT_HAVE_CUDA undefined)"; }
namespace gpu_detail {
Result run_cuda(const Tx*, size_t, Policy, const Selectors&, const GpuOptions&, GpuTimings*) {
    throw std::runtime_error("this build has no CUDA backend; reconfigure with the CUDA toolkit "
                             "installed (or use --gpu-backend emu)");
}
}  // namespace gpu_detail
#endif

Result run_pipeline_gpu(const Tx* tx, size_t n, Policy policy, const Selectors& sels,
                        const GpuOptions& opts, GpuTimings* timings) {
    if (opts.backend == GpuBackend::Emu)
        return gpu_detail::run_emu(tx, n, policy, sels, opts, timings);
    return gpu_detail::run_cuda(tx, n, policy, sels, opts, timings);
}

}  // namespace conduit
