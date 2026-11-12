#include "vision.hpp"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace okww::runtime {
cv::Mat readImage(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        fail("无法读取图像：" + path);
    auto bytes = file.readAll();
    cv::Mat data(1, int(bytes.size()), CV_8U, bytes.data());
    auto image = cv::imdecode(data, cv::IMREAD_COLOR);
    if (image.empty())
        fail("图像解码失败：" + path);
    return image;
}
cv::Mat decode(const Frame &f) {
    if (f.width <= 0 || f.height <= 0 || f.stride <= 0)
        fail("无效采集尺寸");
    cv::Mat out;
    auto *data = const_cast<char *>(f.bytes.constData());
    if (f.format == PixelFormat::MJPG) {
        out = cv::imdecode(cv::Mat(1, int(f.bytes.size()), CV_8U, data), cv::IMREAD_COLOR);
        if (out.empty() || out.cols != f.width || out.rows != f.height)
            fail("MJPEG 尺寸与采集设置不一致");
        return out;
    }
    const bool planar =
        f.format == PixelFormat::NV12 || f.format == PixelFormat::I420 || f.format == PixelFormat::P010;
    if (int64_t(f.stride) * f.height * (planar ? 3 : 2) / 2 > f.bytes.size())
        fail("采集帧长度不完整");
    if (f.format == PixelFormat::BGR24 || f.format == PixelFormat::BGRA32) {
        const int channels = f.format == PixelFormat::BGR24 ? 3 : 4;
        if (f.stride < f.width * channels)
            fail("RGB 行长度不完整");
        cv::Mat source(f.height, f.width, CV_MAKETYPE(CV_8U, channels), data, f.stride);
        if (channels == 4)
            cv::cvtColor(source, out, cv::COLOR_BGRA2BGR);
        else
            out = source.clone();
        if (f.bottomUp)
            cv::flip(out, out, 0);
        return out;
    }
    if (f.width % 2 || (planar && f.height % 2))
        fail("YUV 尺寸必须为偶数");
    if (f.format == PixelFormat::NV12) {
        if (f.stride < f.width)
            fail("NV12 行长度无效");
        cv::cvtColorTwoPlane(
            cv::Mat(f.height, f.width, CV_8U, data, f.stride),
            cv::Mat(f.height / 2, f.width / 2, CV_8UC2, data + f.stride * f.height, f.stride), out,
            cv::COLOR_YUV2BGR_NV12);
    } else if (f.format == PixelFormat::I420) {
        if (f.stride < f.width || f.stride % 2)
            fail("I420 行长度无效");
        cv::Mat flat(f.height * 3 / 2, f.width, CV_8U);
        for (int y = 0; y < f.height; ++y)
            memcpy(flat.ptr(y), data + y * f.stride, f.width);
        auto target = flat.data + f.width * f.height;
        for (int plane = 0; plane < 2; ++plane)
            for (int y = 0; y < f.height / 2; ++y)
                memcpy(target + plane * f.width * f.height / 4 + y * f.width / 2,
                       data + f.stride * f.height + plane * f.stride * f.height / 4 + y * f.stride / 2,
                       f.width / 2);
        cv::cvtColor(flat, out, cv::COLOR_YUV2BGR_I420);
    } else if (f.format == PixelFormat::YUY2 || f.format == PixelFormat::UYVY) {
        if (f.stride < f.width * 2)
            fail("YUV 行长度无效");
        cv::cvtColor(cv::Mat(f.height, f.width, CV_8UC2, data, f.stride), out,
                     f.format == PixelFormat::YUY2 ? cv::COLOR_YUV2BGR_YUY2 : cv::COLOR_YUV2BGR_UYVY);
    } else if (f.format == PixelFormat::P010) {
        if (f.stride < f.width * 2 || f.stride % 2)
            fail("P010 行长度无效");
        cv::Mat bytes(f.height * 3 / 2, f.width, CV_8U);
        for (int y = 0; y < bytes.rows; ++y) {
            auto row = reinterpret_cast<const uint16_t *>(data + y * f.stride);
            for (int x = 0; x < f.width; ++x)
                bytes.at<uint8_t>(y, x) = uint8_t(row[x] >> 8);
        }
        cv::cvtColor(bytes, out, cv::COLOR_YUV2BGR_NV12);
    } else
        fail("未实现的采集图像格式");
    return out;
}
cv::Mat inferenceImage(const Frame &frame, const Config &config) {
    cv::Mat image = decode(frame);
    auto crop = config.get("capture.crop").toArray();
    if (!crop.isEmpty()) {
        if (crop.size() != 4)
            fail("裁剪参数无效");
        cv::Rect rect(crop[0].toInt(), crop[1].toInt(), crop[2].toInt(), crop[3].toInt());
        if (rect.width <= 0 || rect.height <= 0 || (rect & cv::Rect(0, 0, image.cols, image.rows)) != rect)
            fail("裁剪区域越界");
        image = image(rect);
    }
    cv::Size size(config.n("capture.output_width"), config.n("capture.output_height"));
    if (size.width <= 0 || size.height <= 0)
        fail("识别尺寸无效");
    if (image.size() != size)
        cv::resize(image, image, size, 0, 0, cv::INTER_LINEAR);
    return image;
}
cv::Mat InferenceFrames::get(FramePtr frame) {
    if (!frame) {
        source_.reset();
        image_.release();
        return {};
    }
    if (!source_ || source_->sequence != frame->sequence || source_->width != frame->width ||
        source_->height != frame->height || source_->format != frame->format) {
        image_ = inferenceImage(*frame, config_);
        ++conversions_;
    }
    source_ = std::move(frame);
    return image_;
}
cv::Mat filtered(const cv::Mat &image, Filter filter) {
    if (filter == Filter::None)
        return image;
    cv::Mat out;
    if (filter == Filter::Negative80) {
        if (image.channels() == 3)
            cv::cvtColor(image, out, cv::COLOR_BGR2GRAY);
        else
            out = image;
        cv::threshold(out, out, 80, 255, cv::THRESH_BINARY_INV);
        if (image.channels() == 3)
            cv::cvtColor(out, out, cv::COLOR_GRAY2BGR);
        return out;
    }
    if (filter == Filter::Binary244 || filter == Filter::Binary220) {
        if (image.channels() == 3)
            cv::cvtColor(image, out, cv::COLOR_BGR2GRAY);
        else
            out = image;
        cv::threshold(out, out, filter == Filter::Binary244 ? 244 : 220, 255, cv::THRESH_BINARY);
        return out;
    }
    const int lower = filter == Filter::White ? 244 : filter == Filter::Dialog ? 210 : 0;
    const int upper = filter == Filter::InvertWhite ? 240 : 255;
    cv::inRange(image, cv::Scalar(lower, lower, lower), cv::Scalar(upper, upper, upper), out);
    cv::cvtColor(out, out, cv::COLOR_GRAY2BGR);
    return out;
}
Templates::Templates(const QString &coco) { add(coco); }
void Templates::add(const QString &coco) {
    const auto data = readJson(coco);
    QMap<int, QString> images, categories;
    for (auto v : data["images"].toArray()) {
        auto o = v.toObject();
        images[o["id"].toInt()] = o["file_name"].toString();
    }
    for (auto v : data["categories"].toArray()) {
        auto o = v.toObject();
        categories[o["id"].toInt()] = o["name"].toString();
    }
    const QSet<QString> hc{"monthly_card",
                           "suisui_forte3",
                           "message_dialog",
                           "claim_stamina_sign",
                           "skip_dialog_check",
                           "login_close",
                           "garden_confirm",
                           "garden_continue_game",
                           "garden_unpause",
                           "garden_get_gold",
                           "garden_get_purple",
                           "garden_get_skip",
                           "garden_not_interested_confirm",
                           "garden_not_interested",
                           "a_garden_back",
                           "garden_get_confirm_gray",
                           "the_garden_max",
                           "garden_shop_close",
                           "garden_new_stage",
                           "a_garden_restart",
                           "suisui_forte2",
                           "suisui_e1",
                           "e_forte",
                           "f_break_full"};
    const QSet<QString> vc{"monthly_card", "skip_dialog_check"};
    for (auto value : data["annotations"].toArray()) {
        auto a = value.toObject();
        auto name = categories.value(a["category_id"].toInt());
        auto b = a["bbox"].toArray();
        if (name.isEmpty() || b.size() != 4)
            fail("无效模板标注");
        auto path = images.value(a["image_id"].toInt());
        path.replace('\\', '/');
        if (QDir::isAbsolutePath(path) && path.contains("/images/"))
            path = "images/" + path.section("/images/", 1);
        path = QDir(QFileInfo(coco).absolutePath()).absoluteFilePath(path);
        const int x = pixelRound(b[0].toDouble()), y = pixelRound(b[1].toDouble());
        Source source{path,
                      QRect(x, y, pixelRound(b[0].toDouble() + b[2].toDouble()) - x,
                            pixelRound(b[1].toDouble() + b[3].toDouble()) - y),
                      name.contains("hcenter") || hc.contains(name),
                      name.contains("vcenter") || vc.contains(name)};
        if (name == "illusive_realm_exit" || name == "world_earth_icon")
            source.filter = Filter::White;
        else if (name == "purple_target_distance_icon" || name == "mouse_forte")
            source.filter = Filter::Binary244;
        else if (name == "e_forte")
            source.filter = Filter::Binary220;
        else if (name == "skip_dialog" || name == "skip_dialog_new")
            source.filter = Filter::Dialog;
        if (sources_.contains(name))
            continue;
        sources_[name] = source;
    }
    cache_.clear();
}
QRect Templates::anchored(QRect r, QSize source, QSize target, bool hc, bool vc) {
    if (source.isEmpty() || target.isEmpty())
        fail("模板缩放尺寸无效");
    const double scale =
        std::min(double(target.width()) / source.width(), double(target.height()) / source.height());
    auto axis = [scale](int value, int before, int after, bool center) {
        return center                ? pixelRound(after * .5 + (value - before * .5) * scale)
               : value > before / 2. ? after - pixelRound((before - value) * scale)
                                     : pixelRound(value * scale);
    };
    return {axis(r.x(), source.width(), target.width(), hc),
            axis(r.y(), source.height(), target.height(), vc), std::max(1, pixelRound(r.width() * scale)),
            std::max(1, pixelRound(r.height() * scale))};
}
const Templates::Scaled &Templates::scaled(const QString &name, QSize size) {
    if (size != size_) {
        cache_.clear();
        size_ = size;
    }
    if (cache_.contains(name))
        return cache_[name];
    if (!sources_.contains(name))
        fail("缺少已有模板：" + name);
    const auto &s = sources_[name];
    if (!images_.contains(s.path))
        images_[s.path] = readImage(s.path);
    const auto &image = images_[s.path];
    auto r = s.box;
    if (!QRect(0, 0, image.cols, image.rows).contains(r) || r.isEmpty())
        fail("模板标注越界：" + name);
    auto adjusted = anchored(r, {image.cols, image.rows}, size, s.hc, s.vc);
    cv::Mat resized;
    cv::resize(image(cv::Rect(r.x(), r.y(), r.width(), r.height())), resized,
               {adjusted.width(), adjusted.height()});
    return cache_[name] = {filtered(resized, s.filter), adjusted};
}
QVector<Match> Templates::find(const cv::Mat &frame, const QString &name, MatchOptions options) {
    if (checkpoint_) checkpoint_();
    if (frame.empty())
        return {};
    const auto &feature = scaled(name, {frame.cols, frame.rows});
    cv::Mat pattern = feature.mat;
    if (options.rotation != 0) {
        if (!std::isfinite(options.rotation))
            fail("模板旋转角度无效");
        const auto matrix =
            cv::getRotationMatrix2D({float(pattern.cols / 2), float(pattern.rows / 2)}, -options.rotation, 1);
        cv::Mat rotated;
        cv::warpAffine(pattern, rotated, matrix, pattern.size());
        pattern = std::move(rotated);
    }
    if (options.templateTop <= 0 || options.templateTop > 1 || options.searchHeight <= 0 ||
        options.searchHeight > 1)
        fail("局部模板比例无效");
    if (options.templateTop != 1)
        pattern = pattern.rowRange(0, std::max(1, int(pattern.rows * options.templateTop)));
    if (options.templateScale != 1) {
        if (!std::isfinite(options.templateScale) || options.templateScale <= 0 || options.templateScale > 8)
            fail("模板缩放倍率无效");
        cv::resize(pattern, pattern, {}, options.templateScale, options.templateScale, cv::INTER_AREA);
    }
    QRect area;
    if (options.search)
        area = *options.search;
    else {
        auto r = feature.box;
        if (options.searchHeight != 1)
            r.setHeight(std::max(1, int(feature.mat.rows * options.searchHeight)));
        int x = pixelRound(r.x() - frame.cols * options.horizontal),
            y = pixelRound(r.y() - frame.rows * options.vertical);
        area = {x, y, pixelRound(r.x() + r.width() + frame.cols * options.horizontal) - x,
                pixelRound(r.y() + r.height() + frame.rows * options.vertical) - y};
    }
    area = area.intersected(QRect(0, 0, frame.cols, frame.rows));
    if (area.width() < pattern.cols || area.height() < pattern.rows)
        return {};
    cv::Mat search =
        filtered(frame(cv::Rect(area.x(), area.y(), area.width(), area.height())), options.filter);
    if (options.grayscale) {
        if (search.channels() > 1)
            cv::cvtColor(search, search, cv::COLOR_BGR2GRAY);
        if (pattern.channels() > 1)
            cv::cvtColor(pattern, pattern, cv::COLOR_BGR2GRAY);
    }
    if (search.type() != pattern.type())
        fail("模板与图像预处理不一致：" + name);
    const cv::Size originalPattern = pattern.size();
    double scale = 1;
    if (options.targetHeight > 0 && frame.rows >= 1.5 * options.targetHeight) {
        scale = double(options.targetHeight) / frame.rows;
        cv::resize(
            search, search,
            {std::max(1, pixelRound(search.cols * scale)), std::max(1, pixelRound(search.rows * scale))}, 0,
            0, cv::INTER_AREA);
        cv::resize(pattern, pattern, {}, scale, scale, cv::INTER_AREA);
        if (search.cols < pattern.cols || search.rows < pattern.rows)
            return {};
    }
    cv::Mat scores;
    if (checkpoint_) checkpoint_();
    cv::matchTemplate(search, pattern, scores, cv::TM_CCOEFF_NORMED);
    if (checkpoint_) checkpoint_();
    QVector<Match> matches;
    const int limit = options.limit <= 0 ? 1000 : std::min(1000, options.limit);
    while (matches.size() < limit) {
        if (checkpoint_ && matches.size() % 16 == 0) checkpoint_();
        double score;
        cv::Point point;
        cv::minMaxLoc(scores, nullptr, &score, nullptr, &point);
        if (!std::isfinite(score) || score < options.threshold)
            break;
        matches.append({name,
                        {area.x() + pixelRound(point.x / scale), area.y() + pixelRound(point.y / scale),
                         originalPattern.width, originalPattern.height},
                        score});
        cv::Rect suppress(point.x - pattern.cols + 1, point.y - pattern.rows + 1, pattern.cols * 2 - 1,
                          pattern.rows * 2 - 1);
        scores(suppress & cv::Rect(0, 0, scores.cols, scores.rows)).setTo(-1);
    }
    return matches;
}
std::optional<Match> Templates::one(const cv::Mat &frame, const QString &name, MatchOptions options) {
    options.limit = 1;
    auto found = find(frame, name, options);
    return found.isEmpty() ? std::nullopt : std::optional(found.front());
}
double colorPercentage(const cv::Mat &frame, QRect box, cv::Scalar lower, cv::Scalar upper) {
    if (box.isEmpty() || !QRect(0, 0, frame.cols, frame.rows).contains(box))
        return 0;
    cv::Mat mask;
    cv::inRange(frame(cv::Rect(box.x(), box.y(), box.width(), box.height())), lower, upper, mask);
    return double(cv::countNonZero(mask)) / mask.total();
}
QRect relativeBox(const cv::Mat &f, double x, double y, double right, double bottom) {
    int px = pixelRound(f.cols * x), py = pixelRound(f.rows * y);
    return QRect(px, py, pixelRound(f.cols * right) - px, pixelRound(f.rows * bottom) - py)
        .intersected({0, 0, f.cols, f.rows});
}
namespace {
double gameAxis(double percent,double original,int actual,double scale,bool center) {
    const double value=percent*original;
    if(center) return pixelRound(actual*.5+(value-original*.5)*scale);
    return value>original*.5?actual-pixelRound((original-value)*scale):pixelRound(value*scale);
}
}
QPoint gamePoint(QSize size,double x,double y,bool hc,bool vc) {
    if(size.isEmpty()||!std::isfinite(x)||!std::isfinite(y)) fail("游戏界面坐标无效");
    if(std::abs(double(size.width())/size.height()-16./9.)<=.01)
        return {int(size.width()*x),int(size.height()*y)};
    const double base=size.height()*16./9.,scale=std::min(size.width()/base,1.);
    return {int(gameAxis(x,base,size.width(),scale,hc)),int(gameAxis(y,size.height(),size.height(),scale,vc))};
}
QRect gameBox(const cv::Mat &f,double x,double y,double right,double bottom,bool hc,bool vc) {
    if(f.empty()||right<x||bottom<y) fail("游戏界面识别范围无效");
    const bool resized=std::abs(double(f.cols)/f.rows-16./9.)>.01;
    const double base=resized?f.rows*16./9.:f.cols,scale=std::min(f.cols/base,1.);
    const auto origin=resized?gamePoint({f.cols,f.rows},x,y,hc,vc):QPoint(pixelRound(x*f.cols),pixelRound(y*f.rows));
    const double width=resized?(right*base-x*base)*scale:(right-x)*base;
    const double height=resized?(bottom*f.rows-y*f.rows)*scale:(bottom-y)*f.rows;
    return QRect(origin,QSize(pixelRound(width),pixelRound(height)))
        .intersected({0,0,f.cols,f.rows});
}
} // namespace okww::runtime
