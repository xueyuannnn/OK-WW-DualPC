#pragma once
#include "common.hpp"
#ifdef OKWW_FAST_PREVIEW
#include <opencv2/imgproc.hpp>
#endif
namespace okww::common_preview_reference {
static void pixel(const Frame &f, int x, int y, uint8_t *bgr) {
    auto *p = reinterpret_cast<const uint8_t *>(f.bytes.constData());
    int row = f.bottomUp ? f.height - 1 - y : y;
    if (f.format == PixelFormat::BGR24 || f.format == PixelFormat::BGRA32) {
        p += row * f.stride + x * (f.format == PixelFormat::BGR24 ? 3 : 4);
        memcpy(bgr, p, 3);
        return;
    }
    if (f.format == PixelFormat::P010) {
        auto row = reinterpret_cast<const uint16_t *>(p + y * f.stride);
        auto uv = reinterpret_cast<const uint16_t *>(p + f.stride * f.height + (y / 2) * f.stride);
        int yy = std::max(0, int(row[x] >> 6) - 64) * 1220542;
        int u = int(uv[x & ~1] >> 6) - 512, v = int(uv[(x & ~1) + 1] >> 6) - 512;
        auto clip = [](int64_t value) {
            return uint8_t(std::clamp<int64_t>((value + (1 << 21)) >> 22, 0, 255));
        };
        bgr[0] = clip(int64_t(yy) + int64_t(2116026) * u);
        bgr[1] = clip(int64_t(yy) - int64_t(409993) * u - int64_t(852492) * v);
        bgr[2] = clip(int64_t(yy) + int64_t(1673527) * v);
        return;
    }
    int Y = 0, U = 128, V = 128;
    if (f.format == PixelFormat::NV12) {
        Y = p[y * f.stride + x];
        auto uv = p + f.stride * f.height + (y / 2) * f.stride + (x & ~1);
        U = uv[0];
        V = uv[1];
    } else if (f.format == PixelFormat::I420) {
        Y = p[y * f.stride + x];
        auto uv = p + f.stride * f.height;
        U = uv[(y / 2) * (f.stride / 2) + x / 2];
        V = uv[f.stride * f.height / 4 + (y / 2) * (f.stride / 2) + x / 2];
    } else {
        p += y * f.stride + (x & ~1) * 2;
        bool u = f.format == PixelFormat::UYVY;
        Y = p[(x & 1) * 2 + (u ? 1 : 0)];
        U = p[u ? 0 : 1];
        V = p[u ? 2 : 3];
    }
    int yy = std::max(0, Y - 16) * 1220542;
    U -= 128;
    V -= 128;
    auto clip = [](int v) { return uint8_t(std::clamp((v + (1 << 19)) >> 20, 0, 255)); };
    bgr[0] = clip(yy + 2116026 * U);
    bgr[1] = clip(yy - 409993 * U - 852492 * V);
    bgr[2] = clip(yy + 1673527 * V);
}
QImage frameImage(const Frame &f) {
    if (f.format == PixelFormat::MJPG) {
        auto im = QImage::fromData(f.bytes, "JPG");
        if (im.isNull())
            fail("MJPEG 解码失败");
        if (im.size() != QSize(f.width, f.height))
            fail("MJPEG 图像尺寸与采集设置不符");
        return im.convertToFormat(QImage::Format_RGB32);
    }
    QImage im(f.width, f.height, QImage::Format_RGB32);
    for (int y = 0; y < f.height; ++y) {
        auto p = im.scanLine(y);
        for (int x = 0; x < f.width; ++x) {
            pixel(f, x, y, p + 4 * x);
            p[4 * x + 3] = 255;
        }
    }
    return im;
}
QImage previewImage(const Frame &f, QSize target) {
    if (f.width <= 0 || f.height <= 0 || target.isEmpty())
        return {};
    QSize size(f.width, f.height);
    if (size.width() > target.width() || size.height() > target.height())
        size.scale(target, Qt::KeepAspectRatio);
    if (f.format == PixelFormat::MJPG)
        return common_preview_reference::frameImage(f).scaled(size, Qt::KeepAspectRatio,
                                                              Qt::SmoothTransformation);
#ifdef OKWW_FAST_PREVIEW
    if (f.width > 0 && f.height > 0 && f.stride > 0 &&
        (f.format == PixelFormat::NV12 || f.format == PixelFormat::YUY2 || f.format == PixelFormat::UYVY ||
         f.format == PixelFormat::BGR24 || f.format == PixelFormat::BGRA32)) {
        const bool planar = f.format == PixelFormat::NV12;
        const int channels = f.format == PixelFormat::BGRA32  ? 4
                             : f.format == PixelFormat::BGR24 ? 3
                             : planar                         ? 1
                                                              : 2;
        if (int64_t(f.stride) * f.height * (planar ? 3 : 2) / 2 > f.bytes.size() ||
            f.stride < f.width * channels || ((planar || channels == 2) && f.width % 2) ||
            (planar && f.height % 2))
            fail("预览图像长度或尺寸无效");
        auto bytes = const_cast<char *>(f.bytes.constData());
        cv::Mat bgr;
        if (planar)
            cv::cvtColorTwoPlane(
                cv::Mat(f.height, f.width, CV_8U, bytes, f.stride),
                cv::Mat(f.height / 2, f.width / 2, CV_8UC2, bytes + f.stride * f.height, f.stride), bgr,
                cv::COLOR_YUV2BGR_NV12);
        else if (channels == 2)
            cv::cvtColor(cv::Mat(f.height, f.width, CV_8UC2, bytes, f.stride), bgr,
                         f.format == PixelFormat::YUY2 ? cv::COLOR_YUV2BGR_YUY2 : cv::COLOR_YUV2BGR_UYVY);
        else if (channels == 4)
            cv::cvtColor(cv::Mat(f.height, f.width, CV_8UC4, bytes, f.stride), bgr, cv::COLOR_BGRA2BGR);
        else
            bgr = cv::Mat(f.height, f.width, CV_8UC3, bytes, f.stride);
        if (f.bottomUp && channels >= 3) {
            cv::Mat flipped;
            cv::flip(bgr, flipped, 0);
            bgr = std::move(flipped);
        }
        cv::Mat resized;
        if (size == QSize(bgr.cols, bgr.rows))
            resized = bgr;
        else
            // Integrate source pixels so thin text and lines survive reduction.
            cv::resize(bgr, resized, {size.width(), size.height()}, 0, 0, cv::INTER_AREA);
        // Converted/resized matrices own their pixels. Keep that allocation
        // alive with the QImage instead of copying the entire RGB image again.
        // Native BGR at full size still borrows the reusable capture buffer.
        if (channels == 3 && !f.bottomUp && size == QSize(f.width, f.height))
            return QImage(resized.data, resized.cols, resized.rows, int(resized.step), QImage::Format_BGR888)
                .copy();
        auto owner = new cv::Mat(std::move(resized));
        return QImage(
            owner->data, owner->cols, owner->rows, int(owner->step), QImage::Format_BGR888,
            [](void *pixels) { delete static_cast<cv::Mat *>(pixels); }, owner);
    }
#endif
    return common_preview_reference::frameImage(f).scaled(size, Qt::KeepAspectRatio,
                                                          Qt::SmoothTransformation);
}

} // namespace okww::common_preview_reference
