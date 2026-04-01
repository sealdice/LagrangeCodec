//
// Created by pk5ls on 2025/2/25.
//

#include <gtest/gtest.h>
#include <vector>
#include <string>
#include <cstdint>
#include <memory>
#include <fstream>
#include <iostream>
#include <filesystem>

#include "audio.h"
#include "silk.h"
#include "video.h"

#ifndef LAGRANGECODEC_TEST_DATA_DIR
#define LAGRANGECODEC_TEST_DATA_DIR ""
#endif

void testCallback(void* userdata, const uint8_t* data, int len) {
    if (userdata && data && len > 0) {
        auto buffer = static_cast<std::vector<uint8_t> *>(userdata);
        buffer->insert(buffer->end(), data, data + len);
    }
}

class LagrangeCodecTest : public testing::Test {
protected:
    std::vector<uint8_t> audioData;
    std::vector<uint8_t> videoData;
    bool hasAudioData = false;
    bool hasVideoData = false;

    static std::vector<uint8_t> loadTestData(const std::string&file_name) {
        using namespace std;
        try {
            filesystem::path dataPath = filesystem::path(LAGRANGECODEC_TEST_DATA_DIR) / file_name;
            std::cout << "Attempting to load file: " << dataPath << std::endl;

            std::ifstream file(dataPath, std::ios::binary);
            if (!file.is_open()) {
                std::cerr << "Failed to open test data file: " << dataPath << std::endl;
                return {};
            }

            file.seekg(0, std::ios::end);
            std::streamsize size = file.tellg();
            file.seekg(0, std::ios::beg);

            std::vector<uint8_t> buffer(size);
            file.read(reinterpret_cast<char *>(buffer.data()), size);

            if (file.gcount() != size) {
                std::cerr << "Failed to read entire file: " << dataPath << std::endl;
                return {};
            }

            std::cout << "Successfully loaded " << size << " bytes from " << dataPath << std::endl;
            return buffer;
        }
        catch (const std::exception&e) {
            std::cerr << "Exception while loading test data: " << e.what() << std::endl;
            return {};
        }
    }

    void SetUp() override {
        std::cout << "Loading audio test data..." << std::endl;
        audioData = loadTestData("test_audio.mp3");
        hasAudioData = !audioData.empty();
        if (hasAudioData) {
            std::cout << "Audio data loaded successfully: " << audioData.size() << " bytes" << std::endl;
        }
        else {
            std::cout << "Failed to load audio data!" << std::endl;
        }

        std::cout << "Loading video test data..." << std::endl;
        videoData = loadTestData("test_video.mp4");
        hasVideoData = !videoData.empty();
        if (hasVideoData) {
            std::cout << "Video data loaded successfully: " << videoData.size() << " bytes" << std::endl;
        }
        else {
            std::cout << "Failed to load video data!" << std::endl;
        }
    }
};

class LagrangeAudioCodecTest : public LagrangeCodecTest {
protected:
    std::vector<uint8_t> pcmData;
    std::vector<uint8_t> silkData;
    std::vector<uint8_t> decodedPcmData;
};

TEST_F(LagrangeAudioCodecTest, TestAudioEncodingChain) {
    ASSERT_TRUE(hasAudioData) << "Audio test data not available";

    std::cout << "Step 1: Converting MP3 to PCM..." << std::endl;
    int result = audio_to_pcm(audioData.data(), static_cast<int>(audioData.size()), testCallback, &pcmData);
    ASSERT_EQ(result, 0) << "audio_to_pcm function failed";
    ASSERT_FALSE(pcmData.empty()) << "No PCM data was generated";
    std::cout << "PCM data size: " << pcmData.size() << " bytes" << std::endl;

    std::cout << "Step 2: Encoding PCM to SILK..." << std::endl;
    result = silk_encode(pcmData.data(), static_cast<int>(pcmData.size()), testCallback, &silkData);
    ASSERT_EQ(result, 0) << "silk_encode function failed";
    ASSERT_FALSE(silkData.empty()) << "No SILK data was generated";
    std::cout << "SILK encoded data size: " << silkData.size() << " bytes" << std::endl;

    std::cout << "Step 3: Decoding SILK to PCM..." << std::endl;
    result = silk_decode(silkData.data(), static_cast<int>(silkData.size()), testCallback, &decodedPcmData);
    ASSERT_EQ(result, 0) << "silk_decode function failed";
    ASSERT_FALSE(decodedPcmData.empty()) << "No PCM data was decoded";
    std::cout << "Decoded PCM data size: " << decodedPcmData.size() << " bytes" << std::endl;

    EXPECT_GT(pcmData.size(), 0);
    EXPECT_GT(decodedPcmData.size(), 0);
}

TEST_F(LagrangeCodecTest, TestVideoFirstFrame) {
    ASSERT_TRUE(hasVideoData) << "Video test data not available";

    uint8_t* frameData = nullptr;
    int frameLen = 0;
    std::cout << "Extracting first frame from video..." << std::endl;
    const int result = video_first_frame(videoData.data(), static_cast<int>(videoData.size()), frameData, frameLen);
    std::cout << "Extraction completed with result: " << result << std::endl;
    EXPECT_EQ(result, 0) << "video_first_frame function failed";
    EXPECT_TRUE(frameData != nullptr) << "No frame data was generated";
    EXPECT_GT(frameLen, 0) << "Frame length is invalid";
    if (frameData != nullptr) {
        std::cout << "First frame data size: " << frameLen << " bytes" << std::endl;
    }
    // // save frame data
    // std::ofstream file("first_frame.png", std::ios::binary);
    // file.write(reinterpret_cast<char*>(frameData), frameLen);
    // file.close();
}

TEST_F(LagrangeCodecTest, TestVideoGetSize) {
    ASSERT_TRUE(hasVideoData) << "Video test data not available";

    VideoInfo info = {};
    std::cout << "Getting video size info..." << std::endl;
    const int result = video_get_size(videoData.data(), static_cast<int>(videoData.size()), info);
    std::cout << "Video info retrieval completed with result: " << result << std::endl;
    EXPECT_EQ(result, 0) << "video_get_size function failed";
    EXPECT_EQ(info.width, 320) << "Video width is not expected";
    EXPECT_EQ(info.height, 240) << "Video height is not expected";
    EXPECT_EQ(info.duration, 124) << "Video duration is not expected";
    std::cout << "Video dimensions: " << info.width << "x" << info.height
            << ", duration: " << info.duration << std::endl;
}


TEST_F(LagrangeAudioCodecTest, TestAudioToPcmOnly) {
    ASSERT_TRUE(hasAudioData) << "Audio test data not available";

    std::vector<uint8_t> localPcmData;
    int result = audio_to_pcm(audioData.data(), static_cast<int>(audioData.size()), testCallback, &localPcmData);
    EXPECT_EQ(result, 0) << "audio_to_pcm function failed";
    EXPECT_FALSE(localPcmData.empty()) << "No PCM data was generated";
    std::cout << "PCM data size: " << localPcmData.size() << " bytes" << std::endl;
}

TEST_F(LagrangeAudioCodecTest, TestPcmToSilkOnly) {
    ASSERT_TRUE(hasAudioData) << "Audio test data not available";

    std::vector<uint8_t> localPcmData;
    int result = audio_to_pcm(audioData.data(), static_cast<int>(audioData.size()), testCallback, &localPcmData);
    ASSERT_EQ(result, 0) << "Failed to prepare PCM data";

    std::vector<uint8_t> localSilkData;
    result = silk_encode(localPcmData.data(), static_cast<int>(localPcmData.size()), testCallback, &localSilkData);
    EXPECT_EQ(result, 0) << "silk_encode function failed";
    EXPECT_FALSE(localSilkData.empty()) << "No SILK data was generated";
    std::cout << "SILK encoded data size: " << localSilkData.size() << " bytes" << std::endl;
}

TEST_F(LagrangeCodecTest, AudioToPcmRejectsInvalidInputSafely) {
    std::vector<uint8_t> invalidAudio = {0x00, 0x01, 0x02, 0x03, 0x04};
    std::vector<uint8_t> output;

    const int result = audio_to_pcm(invalidAudio.data(), static_cast<int>(invalidAudio.size()), testCallback, &output);
    EXPECT_NE(result, 0);
    EXPECT_TRUE(output.empty());
}

TEST_F(LagrangeCodecTest, AudioToPcmRejectsNullCallbackSafely) {
    ASSERT_TRUE(hasAudioData) << "Audio test data not available";
    const int result = audio_to_pcm(audioData.data(), static_cast<int>(audioData.size()), nullptr, nullptr);
    EXPECT_NE(result, 0);
}

TEST_F(LagrangeCodecTest, VideoGetSizeRejectsInvalidInputSafely) {
    std::vector<uint8_t> invalidVideo = {0x00, 0x01, 0x02, 0x03, 0x04};
    VideoInfo info = {};

    const int result = video_get_size(invalidVideo.data(), static_cast<int>(invalidVideo.size()), info);
    EXPECT_NE(result, 0);
    EXPECT_EQ(info.width, 0);
    EXPECT_EQ(info.height, 0);
    EXPECT_EQ(info.duration, 0);
}

TEST_F(LagrangeCodecTest, VideoFirstFrameRejectsInvalidInputSafely) {
    std::vector<uint8_t> invalidVideo = {0x00, 0x01, 0x02, 0x03, 0x04};
    uint8_t* frameData = nullptr;
    int frameLen = 0;

    const int result = video_first_frame(invalidVideo.data(), static_cast<int>(invalidVideo.size()), frameData, frameLen);
    EXPECT_NE(result, 0);
    EXPECT_EQ(frameData, nullptr);
    EXPECT_EQ(frameLen, 0);
}

int main(int argc, char** argv) {
    std::cout << "Starting LagrangeCodec tests..." << std::endl;
    testing::InitGoogleTest(&argc, argv);
    const int result = RUN_ALL_TESTS();
    std::cout << "Tests completed with result: " << result << std::endl;
    return result;
}
