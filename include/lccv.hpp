#ifndef LCCV_HPP
#define LCCV_HPP
// lccv.hpp

#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <opencv2/opencv.hpp>
#include <thread>

#include <libcamera/libcamera.h>
#include "core/libcamera_app.hpp"

namespace lccv {

class PiCamera {
public:
    PiCamera();
    ~PiCamera();

    Options *options;

    // Photo mode
    bool startPhoto();
    bool capturePhoto(cv::Mat &frame);
    bool stopPhoto();

    // Video mode
    bool startVideo();
    bool getVideoFrame(cv::Mat &frame, unsigned int timeout);
    void stopVideo();

    // Applies new zoom options
    void ApplyZoomOptions();

private:
    std::unique_ptr<LibcameraApp> app;
    std::thread videoThread;
    std::atomic<bool> running{false}, frameready{false};

    unsigned int still_flags = 0;
    unsigned int vw = 0, vh = 0, vstr = 0;
    uint8_t *framebuffer = nullptr;
    bool camerastarted = false;

    std::mutex mtx;
    std::condition_variable frameCond;
    cv::Mat currentFrame;

    void getImage(cv::Mat &frame, CompletedRequestPtr &payload);
    void videoLoop();
};

} // namespace lccv
#endif
// lccv_threaded.hpp