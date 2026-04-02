#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "audio.h"
#include "video.h"

static void append_callback(void* userdata, const uint8_t* data, int len) {
    if (!userdata || !data || len <= 0) {
        return;
    }

    auto* out = static_cast<std::vector<uint8_t>*>(userdata);
    out->insert(out->end(), data, data + len);
}

static std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: android_runtime_smoke <audio|video> <file>" << std::endl;
        return 2;
    }

    const std::string mode = argv[1];
    const std::string path = argv[2];
    const auto bytes = read_file(path);
    if (bytes.empty()) {
        std::cerr << "failed to read input file: " << path << std::endl;
        return 3;
    }

    if (mode == "audio") {
        std::vector<uint8_t> pcm;
        const int ret = audio_to_pcm(const_cast<uint8_t*>(bytes.data()), static_cast<int>(bytes.size()), append_callback, &pcm);
        std::cout << "audio_to_pcm returned: " << ret << ", pcm bytes: " << pcm.size() << std::endl;
        return ret == 0 ? 0 : 1;
    }

    if (mode == "video") {
        VideoInfo info = {};
        const int ret = video_get_size(const_cast<uint8_t*>(bytes.data()), static_cast<int>(bytes.size()), &info);
        std::cout << "video_get_size returned: " << ret << ", width=" << info.width << ", height=" << info.height << ", duration=" << info.duration << std::endl;
        return ret == 0 ? 0 : 1;
    }

    std::cerr << "unknown mode: " << mode << std::endl;
    return 4;
}
