#pragma once
#include "runtime/echo_detection_host.hpp"
namespace okww::runtime::detection_reference {
inline QVector<Match> prediction(const float *scores,size_t,size_t channels,size_t rows,double gain,int left,int top,double threshold,int label,const std::function<void()> &checkpoint) {
    const std::array<size_t,3> shape{1,channels,rows};
    QVector<Match> candidates;
    for (size_t row = 0; row < rows; ++row) {
        if (checkpoint && row % 256 == 0) checkpoint();
        float confidence = scores[4 * rows + row];
        size_t category = 0;
        for (size_t c = 5; c < shape[1]; ++c)
            if (scores[c * rows + row] > confidence) {
                confidence = scores[c * rows + row];
                category = c - 4;
            }
        // NMSBoxes excludes scores equal to the threshold.
        if (!std::isfinite(confidence) || confidence <= threshold || (label != -1 && int(category) != label))
            continue;
        // float32 arithmetic here matches NumPy's array postprocessing.
        const float x = scores[row] - float(left), y = scores[rows + row] - float(top);
        const float bw = scores[2 * rows + row], bh = scores[3 * rows + row], ratio = float(gain);
        const std::array<float, 4> values{(x - bw / 2) / ratio, (y - bh / 2) / ratio, bw / ratio, bh / ratio};
        if (std::any_of(values.begin(), values.end(),
                        [](float n) { return !std::isfinite(n) || std::abs(n) > 10000000; }))
            fail("声骸模型返回异常坐标");
        QRect box{int(values[0]), int(values[1]), int(values[2]), int(values[3])};
        if (box.width() > 0 && box.height() > 0)
            candidates.append({category == 0 ? "echo" : "unknown", box, confidence});
    }
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const auto &a, const auto &b) { return a.confidence > b.confidence; });
    QVector<Match> kept;
    for (const auto &candidate : candidates) {
        if (checkpoint) checkpoint();
        bool suppressed = false;
        for (const auto &prior : kept) {
            const auto intersection = candidate.box.intersected(prior.box);
            const double area = double(intersection.width()) * intersection.height();
            const double total = double(candidate.box.width()) * candidate.box.height() +
                                 double(prior.box.width()) * prior.box.height() - area;
            if (total > 0 && area / total > .45) {
                suppressed = true;
                break;
            }
        }
        if (!suppressed)
            kept.append(candidate);
    }
    const auto less = [](const Match &a, const Match &b) {
        const bool row = !(a.box.y() > b.box.y() + b.box.height() || a.box.y() + a.box.height() < b.box.y());
        int delta = row ? a.box.x() - b.box.x() : a.box.y() - b.box.y();
        if (!delta)
            delta = row ? a.box.y() - b.box.y() : a.box.x() - b.box.x();
        if (!delta)
            delta = int(a.confidence * 1000 - b.confidence * 1000);
        if (!delta)
            delta = QString::compare(a.name, b.name);
        return delta < 0;
    };
    // The source compares intersecting vertical spans as a row. It is not a
    // strict weak ordering, so do not give it to std::sort (undefined behavior).
    // Preserve Python's natural initial run and stable binary insertion.
    qsizetype run = std::min<qsizetype>(2, kept.size());
    if (kept.size() > 1) {
        const bool descending = less(kept[1], kept[0]);
        while (run < kept.size() && less(kept[run], kept[run - 1]) == descending)
            ++run;
        if (descending)
            std::reverse(kept.begin(), kept.begin() + run);
    }
    for (qsizetype i = run; i < kept.size(); ++i) {
        const auto item = kept[i];
        qsizetype low = 0, high = i;
        while (low < high) {
            const auto mid = (low + high) / 2;
            if (less(item, kept[mid]))
                high = mid;
            else
                low = mid + 1;
        }
        for (qsizetype j = i; j > low; --j)
            kept[j] = kept[j - 1];
        kept[low] = item;
    }
    return kept;
}
}
