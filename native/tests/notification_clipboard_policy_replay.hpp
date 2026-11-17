#pragma once
#include "runtime/notification.hpp"
#include "runtime/workflow_abi.hpp"
namespace okww::runtime::notice_clipboard_test {
struct Block {
    int id;
    QByteArray bytes;
    bool freed = false;
};
struct State {
    int scene, allocs = 0, opens = 0, sequences = 0;
    DWORD sequence = 10;
    QJsonArray trace;
    Block old{0, QByteArray::fromHex("4f004c00440000000000")};
    std::vector<std::unique_ptr<Block>> blocks;
    explicit State(int s) : scene(s) {
        if (s % 5 == 0)
            old.bytes = {};
    }
};
inline thread_local State *active = nullptr;
inline Block &block(HANDLE h) { return *reinterpret_cast<Block *>(h); }
inline HMODULE GetModuleHandleW(LPCWSTR) {
    active->trace.append("module");
    return reinterpret_cast<HMODULE>(uintptr_t(1));
}
template <class... Args> HWND CreateWindowExW(Args...) {
    active->trace.append("create-owner");
    return active->scene % 7 ? reinterpret_cast<HWND>(uintptr_t(1)) : nullptr;
}
inline BOOL DestroyWindow(HWND) {
    active->trace.append("destroy-owner");
    return true;
}
inline cv::Mat decode(const QByteArray &png) {
    active->trace.append(QJsonObject{{"decode", QString::fromUtf8(png)}});
    if (active->scene % 31 == 0)
        throw std::runtime_error("decode fixture");
    if (active->scene % 29 == 0)
        return {};
    return cv::Mat(1 + active->scene % 5, 1 + active->scene % 7, CV_8UC3,
                   cv::Scalar(11, 22, active->scene % 256));
}
inline HGLOBAL GlobalAlloc(UINT flags, SIZE_T size) {
    active->trace.append(QJsonObject{{"alloc", double(size)}, {"flags", int(flags)}});
    ++active->allocs;
    if ((active->allocs == 1 && active->scene % 11 == 0) || (active->allocs == 2 && active->scene % 17 == 0))
        return nullptr;
    auto object = std::make_unique<Block>();
    object->id = int(active->blocks.size()) + 1;
    object->bytes = QByteArray(qsizetype(size), char(0x6f));
    auto *pointer = object.get();
    active->blocks.push_back(std::move(object));
    return reinterpret_cast<HGLOBAL>(pointer);
}
inline LPVOID GlobalLock(HGLOBAL h) {
    auto &b = block(h);
    active->trace.append(QJsonObject{{"lock", b.id}});
    if ((b.id == 1 && active->scene % 13 == 0) || (b.id == 0 && active->scene % 19 == 0) ||
        (b.id == 2 && active->scene % 23 == 0))
        return nullptr;
    return b.bytes.data();
}
inline BOOL GlobalUnlock(HGLOBAL h) {
    active->trace.append(QJsonObject{{"unlock", block(h).id}});
    return true;
}
inline HGLOBAL GlobalFree(HGLOBAL h) {
    auto &b = block(h);
    active->trace.append(QJsonObject{{"free", b.id}});
    b.freed = true;
    return nullptr;
}
inline BOOL OpenClipboard(HWND) {
    active->trace.append("open");
    ++active->opens;
    return !((active->opens == 1 && active->scene % 37 == 0) ||
             (active->opens == 2 && active->scene % 41 == 0));
}
inline HANDLE GetClipboardData(UINT format) {
    active->trace.append(QJsonObject{{"get", int(format)}});
    return active->scene % 3 ? reinterpret_cast<HANDLE>(&active->old) : nullptr;
}
inline SIZE_T GlobalSize(HGLOBAL h) {
    auto &b = block(h);
    active->trace.append(QJsonObject{{"size", b.id}});
    return SIZE_T(b.bytes.size());
}
inline BOOL EmptyClipboard() {
    active->trace.append("empty");
    ++active->sequence;
    return true;
}
inline HANDLE SetClipboardData(UINT format, HANDLE h) {
    auto &b = block(h);
    active->trace.append(
        QJsonObject{{"set", int(format)}, {"block", b.id}, {"bytes", QString::fromLatin1(b.bytes.toHex())}});
    ++active->sequence;
    if ((format == CF_DIB && active->scene % 43 == 0) ||
        (format == CF_UNICODETEXT && active->scene % 47 == 0))
        return nullptr;
    return h;
}
inline DWORD GetClipboardSequenceNumber() {
    active->trace.append("sequence");
    ++active->sequences;
    return active->sequence + (active->sequences > 1 && active->scene % 53 == 0 ? 1 : 0);
}
inline BOOL CloseClipboard() {
    active->trace.append("close");
    return true;
}
class Original {
    HWND owner_ = nullptr;
    QByteArray savedText_;
    DWORD sequence_ = 0;

  public:
    explicit Original(const QByteArray &png) {
        owner_ =
            notice_clipboard_test::CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                                   notice_clipboard_test::GetModuleHandleW(nullptr), nullptr);
        if (!owner_)
            fail("无法创建通知剪贴板窗口");
        auto image = decode(png);
        if (image.empty()) {
            notice_clipboard_test::DestroyWindow(owner_);
            fail("通知截图无法解码");
        }
        const int stride = (image.cols * 3 + 3) & ~3;
        const auto bytes = sizeof(BITMAPINFOHEADER) + size_t(stride) * image.rows;
        HGLOBAL memory = notice_clipboard_test::GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (!memory) {
            notice_clipboard_test::DestroyWindow(owner_);
            fail("无法分配通知截图");
        }
        auto *buffer = static_cast<unsigned char *>(notice_clipboard_test::GlobalLock(memory));
        if (!buffer) {
            notice_clipboard_test::GlobalFree(memory);
            notice_clipboard_test::DestroyWindow(owner_);
            fail("无法锁定通知截图内存");
        }
        memset(buffer, 0, bytes);
        auto *header = reinterpret_cast<BITMAPINFOHEADER *>(buffer);
        header->biSize = sizeof(*header);
        header->biWidth = image.cols;
        header->biHeight = image.rows;
        header->biPlanes = 1;
        header->biBitCount = 24;
        header->biCompression = BI_RGB;
        for (int y = 0; y < image.rows; ++y)
            memcpy(buffer + sizeof(*header) + size_t(stride) * y, image.ptr(image.rows - 1 - y),
                   size_t(image.cols) * 3);
        notice_clipboard_test::GlobalUnlock(memory);
        if (!notice_clipboard_test::OpenClipboard(owner_)) {
            notice_clipboard_test::GlobalFree(memory);
            notice_clipboard_test::DestroyWindow(owner_);
            fail("剪贴板正在使用，通知截图未发送");
        }
        auto old = notice_clipboard_test::GetClipboardData(CF_UNICODETEXT);
        if (old) {
            auto *value = static_cast<const char *>(notice_clipboard_test::GlobalLock(old));
            if (value) {
                savedText_ = QByteArray(value, int(notice_clipboard_test::GlobalSize(old)));
                notice_clipboard_test::GlobalUnlock(old);
            }
        }
        notice_clipboard_test::EmptyClipboard();
        const bool set = notice_clipboard_test::SetClipboardData(CF_DIB, memory) != nullptr;
        sequence_ = notice_clipboard_test::GetClipboardSequenceNumber();
        notice_clipboard_test::CloseClipboard();
        if (!set) {
            notice_clipboard_test::GlobalFree(memory);
            notice_clipboard_test::DestroyWindow(owner_);
            fail("无法写入通知截图");
        }
    }
    ~Original() {
        // Preserve a new user copy rather than overwriting it on cleanup.
        if (notice_clipboard_test::GetClipboardSequenceNumber() == sequence_ &&
            notice_clipboard_test::OpenClipboard(owner_)) {
            notice_clipboard_test::EmptyClipboard();
            if (!savedText_.isEmpty()) {
                auto memory = notice_clipboard_test::GlobalAlloc(GMEM_MOVEABLE, size_t(savedText_.size()));
                if (memory) {
                    auto *data = notice_clipboard_test::GlobalLock(memory);
                    if (data) {
                        memcpy(data, savedText_.data(), size_t(savedText_.size()));
                        notice_clipboard_test::GlobalUnlock(memory);
                        if (!notice_clipboard_test::SetClipboardData(CF_UNICODETEXT, memory))
                            notice_clipboard_test::GlobalFree(memory);
                    } else
                        notice_clipboard_test::GlobalFree(memory);
                }
            }
            notice_clipboard_test::CloseClipboard();
        }
        notice_clipboard_test::DestroyWindow(owner_);
    }
};
class Current {
    HWND owner_ = nullptr;
    QByteArray savedText_;
    DWORD sequence_ = 0;
    void workflow(rust::WorkflowMethod method, const QByteArray *png = nullptr) {
        cv::Mat image;
        int stride = 0;
        size_t bytes = 0;
        HGLOBAL memory = nullptr, old = nullptr;
        unsigned char *buffer = nullptr;
        const char *oldText = nullptr;
        void *saved = nullptr;
        rust::workflowRun(method, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::NoticeClipboardCreateOwner:
                owner_ = notice_clipboard_test::CreateWindowExW(
                    0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                    notice_clipboard_test::GetModuleHandleW(nullptr), nullptr);
                out[0] = owner_ != nullptr;
                break;
            case rust::WorkflowOp::NoticeClipboardDecode:
                image = decode(*png);
                break;
            case rust::WorkflowOp::NoticeClipboardImageEmpty:
                out[0] = image.empty();
                break;
            case rust::WorkflowOp::NoticeClipboardDestroyOwner:
                notice_clipboard_test::DestroyWindow(owner_);
                break;
            case rust::WorkflowOp::NoticeClipboardImageCols:
                out[0] = image.cols;
                break;
            case rust::WorkflowOp::NoticeClipboardImageRows:
                out[0] = image.rows;
                break;
            case rust::WorkflowOp::NoticeClipboardStride:
                stride = int(a[0]);
                bytes = sizeof(BITMAPINFOHEADER) + size_t(stride) * image.rows;
                break;
            case rust::WorkflowOp::NoticeClipboardAllocateImage:
                memory = notice_clipboard_test::GlobalAlloc(GMEM_MOVEABLE, bytes);
                out[0] = memory != nullptr;
                break;
            case rust::WorkflowOp::NoticeClipboardLockImage:
                buffer = static_cast<unsigned char *>(notice_clipboard_test::GlobalLock(memory));
                out[0] = buffer != nullptr;
                break;
            case rust::WorkflowOp::NoticeClipboardFreeMemory:
                notice_clipboard_test::GlobalFree(memory);
                break;
            case rust::WorkflowOp::NoticeClipboardImageHeader: {
                memset(buffer, 0, bytes);
                auto *header = reinterpret_cast<BITMAPINFOHEADER *>(buffer);
                header->biSize = sizeof(*header);
                header->biWidth = image.cols;
                header->biHeight = image.rows;
                header->biPlanes = 1;
                header->biBitCount = 24;
                header->biCompression = BI_RGB;
                break;
            }
            case rust::WorkflowOp::NoticeClipboardCopyRow: {
                const int y = int(a[0]);
                memcpy(buffer + sizeof(BITMAPINFOHEADER) + size_t(stride) * y, image.ptr(image.rows - 1 - y),
                       size_t(image.cols) * 3);
                break;
            }
            case rust::WorkflowOp::NoticeClipboardUnlockMemory:
                notice_clipboard_test::GlobalUnlock(memory);
                break;
            case rust::WorkflowOp::NoticeClipboardOpen:
                out[0] = notice_clipboard_test::OpenClipboard(owner_);
                break;
            case rust::WorkflowOp::NoticeClipboardOldText:
                old = notice_clipboard_test::GetClipboardData(CF_UNICODETEXT);
                out[0] = old != nullptr;
                break;
            case rust::WorkflowOp::NoticeClipboardLockOld:
                oldText = static_cast<const char *>(notice_clipboard_test::GlobalLock(old));
                out[0] = oldText != nullptr;
                break;
            case rust::WorkflowOp::NoticeClipboardSaveOld:
                savedText_ = QByteArray(oldText, int(notice_clipboard_test::GlobalSize(old)));
                break;
            case rust::WorkflowOp::NoticeClipboardUnlockOld:
                notice_clipboard_test::GlobalUnlock(old);
                break;
            case rust::WorkflowOp::NoticeClipboardEmpty:
                notice_clipboard_test::EmptyClipboard();
                break;
            case rust::WorkflowOp::NoticeClipboardSetImage:
                out[0] = notice_clipboard_test::SetClipboardData(CF_DIB, memory) != nullptr;
                break;
            case rust::WorkflowOp::NoticeClipboardSaveSequence:
                sequence_ = notice_clipboard_test::GetClipboardSequenceNumber();
                break;
            case rust::WorkflowOp::NoticeClipboardClose:
                notice_clipboard_test::CloseClipboard();
                break;
            case rust::WorkflowOp::NoticeClipboardSequenceMatches:
                out[0] = notice_clipboard_test::GetClipboardSequenceNumber() == sequence_;
                break;
            case rust::WorkflowOp::NoticeClipboardSavedEmpty:
                out[0] = savedText_.isEmpty();
                break;
            case rust::WorkflowOp::NoticeClipboardAllocateSaved:
                memory = notice_clipboard_test::GlobalAlloc(GMEM_MOVEABLE, size_t(savedText_.size()));
                out[0] = memory != nullptr;
                break;
            case rust::WorkflowOp::NoticeClipboardLockSaved:
                saved = notice_clipboard_test::GlobalLock(memory);
                out[0] = saved != nullptr;
                break;
            case rust::WorkflowOp::NoticeClipboardCopySaved:
                memcpy(saved, savedText_.data(), size_t(savedText_.size()));
                break;
            case rust::WorkflowOp::NoticeClipboardSetText:
                out[0] = notice_clipboard_test::SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
                break;
            case rust::WorkflowOp::NoticeClipboardFailure: {
                static const QStringList errors{"无法创建通知剪贴板窗口",
                                                "通知截图无法解码",
                                                "无法分配通知截图",
                                                "无法锁定通知截图内存",
                                                "剪贴板正在使用，通知截图未发送",
                                                "无法写入通知截图"};
                fail(errors.at(int(a[0])));
            }
            default:
                fail("Rust 通知剪贴板宿主无效");
            }
        });
    }

  public:
    explicit Current(const QByteArray &png) { workflow(rust::WorkflowMethod::NoticeClipboardCreate, &png); }
    ~Current() { workflow(rust::WorkflowMethod::NoticeClipboardRestore); }
};
template <class T> QJsonObject outcome(int scene) {
    State state(scene);
    active = &state;
    QJsonObject result;
    try {
        {
            T clipboard("PNG");
            state.trace.append("image-ready");
        }
        result["ok"] = true;
    } catch (const std::exception &e) {
        result["error"] = errorText(e);
    } catch (...) {
        result["error"] = "unknown";
    }
    QJsonArray blocks;
    for (const auto &b : state.blocks)
        blocks.append(QJsonObject{
            {"id", b->id}, {"freed", b->freed}, {"bytes", QString::fromLatin1(b->bytes.toHex())}});
    result["trace"] = state.trace;
    result["blocks"] = blocks;
    return result;
}
} // namespace okww::runtime::notice_clipboard_test
