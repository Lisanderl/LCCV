#include "lccv.hpp"
#include <libcamera/libcamera/stream.h>
#include <chrono>
#include <cstring>
#include <iostream>

using namespace lccv;
using namespace std::chrono;

PiCamera::PiCamera() {
    app = std::make_unique<LibcameraApp>(std::make_unique<Options>());
    options = static_cast<Options *>(app->GetOptions());
    options->camera = 0;
    options->photo_width = 4056;
    options->photo_height = 3040;
    options->video_width = 640;
    options->video_height = 480;
    options->framerate = 30;
    options->denoise = "auto";
    options->timeout = 1000;
    options->setMetering(Metering_Modes::METERING_MATRIX);
    options->setExposureMode(Exposure_Modes::EXPOSURE_NORMAL);
    options->setWhiteBalance(WhiteBalance_Modes::WB_AUTO);
    options->contrast = 1.0f;
    options->saturation = 1.0f;
    still_flags |= LibcameraApp::FLAG_STILL_RGB;
}

PiCamera::~PiCamera() {
    stopVideo();
    stopPhoto();
}

void PiCamera::getImage(cv::Mat &frame, CompletedRequestPtr &payload) {
    unsigned int w, h, stride;
    libcamera::Stream *stream = app->StillStream();
    app->StreamDimensions(stream, &w, &h, &stride);
    const std::vector<libcamera::Span<uint8_t>> mem = app->Mmap(payload->buffers[stream]);
    frame.create(h, w, CV_8UC3);
    for (unsigned int i = 0; i < h; i++) {
        std::memcpy(frame.ptr(i), mem[0].data() + i * stride, w * 3);
    }
}

bool PiCamera::startPhoto() {
    app->OpenCamera();
    app->ConfigureStill(still_flags);
    camerastarted = true;
    return true;
}

bool PiCamera::stopPhoto() {
    if (camerastarted) {
        camerastarted = false;
        app->Teardown();
        app->CloseCamera();
    }
    return true;
}

bool PiCamera::capturePhoto(cv::Mat &frame) {
    if (!camerastarted) startPhoto();
    app->StartCamera();
    LibcameraApp::Msg msg = app->Wait();
    if (msg.type == LibcameraApp::MsgType::RequestComplete) {
        getImage(frame, std::get<CompletedRequestPtr>(msg.payload));
    } else {
        return false;
    }
    app->StopCamera();
    stopPhoto();
    return true;
}

bool PiCamera::startVideo() {
    if (running.load()) return false;
    app->OpenCamera();
    app->ConfigureViewfinder();
    app->StartCamera();
    running.store(true);
    videoThread = std::thread(&PiCamera::videoLoop, this);
    return true;
}

void PiCamera::stopVideo() {
    if (!running.load()) return;
    running.store(false);
    if (videoThread.joinable()) videoThread.join();
    app->StopCamera();
    app->Teardown();
    app->CloseCamera();
}

bool PiCamera::getVideoFrame(cv::Mat &frame, unsigned int timeout) {
    std::unique_lock<std::mutex> lock(mtx);
    if (!frameCond.wait_for(lock, milliseconds(timeout), [this]() { return frameready.load(); })) {
        return false;
    }
    frame = currentFrame.clone();
    frameready.store(false);
    return true;
}

void PiCamera::videoLoop() {
    libcamera::Stream *stream = app->ViewfinderStream(&vw, &vh, &vstr);
    while (running.load()) {
        LibcameraApp::Msg msg = app->Wait();
        if (msg.type != LibcameraApp::MsgType::RequestComplete) continue;
        CompletedRequestPtr payload = std::get<CompletedRequestPtr>(msg.payload);
        auto mem = app->Mmap(payload->buffers[stream]);

        cv::Mat frame(vh, vw, CV_8UC3);
        for (unsigned int i = 0; i < vh; ++i) {
            std::memcpy(frame.ptr(i), mem[0].data() + i * vstr, vw * 3);
        }

        {
            std::lock_guard<std::mutex> lock(mtx);
            currentFrame = frame;
            frameready.store(true);
        }
        frameCond.notify_one();
    }
}
