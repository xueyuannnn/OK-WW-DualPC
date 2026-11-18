#include "update.hpp"
#include "app_info.hpp"
#include "common.hpp"
#include "platform_workflow.hpp"
#include "update_version_abi.hpp"
#include <algorithm>

namespace okww::update {
bool installationBusy(const QString &root) {
    static_assert(ERROR_ACCESS_DENIED == 5 && ERROR_SHARING_VIOLATION == 32 && ERROR_LOCK_VIOLATION == 33);
    HANDLE file = INVALID_HANDLE_VALUE;
    DWORD error = 0;
    return rust::platformWorkflowRun(
               rust::WorkflowMethod::UpdateInstallationBusy, {},
               [&](rust::WorkflowOp op, const double *, double *out) {
                   switch (op) {
                   case rust::WorkflowOp::UpdateOpenInstallLock: {
                       const auto path = QDir::toNativeSeparators(root + "/.updates/install.lock");
                       file = CreateFileW(reinterpret_cast<const wchar_t *>(path.utf16()), GENERIC_READ,
                                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                       error = GetLastError();
                       break;
                   }
                   case rust::WorkflowOp::UpdateInstallLockOpened:
                       out[0] = file != INVALID_HANDLE_VALUE;
                       break;
                   case rust::WorkflowOp::UpdateCloseInstallLock:
                       CloseHandle(file);
                       break;
                   case rust::WorkflowOp::UpdateInstallLockError:
                       out[0] = error;
                       break;
                   default:
                       throw std::runtime_error("Rust 更新锁宿主无效");
                   }
               })[0] != 0;
}
std::optional<Version> Version::parse(QString text) {
    rust::ParsedVersion parsed{};
    if (rust::okww_rs_version_parse(reinterpret_cast<const uint16_t *>(text.utf16()), size_t(text.size()),
                                    &parsed) != 1)
        return {};
    return Version{
        parsed.numbers[0], parsed.numbers[1], parsed.numbers[2],
        text.mid(qsizetype(parsed.preStart), qsizetype(parsed.preLength)).split('.', Qt::SkipEmptyParts)};
}
int Version::compare(const Version &other) const {
    QVarLengthArray<rust::WideView, 8> left(pre.size()), right(other.pre.size());
    const auto span = [](const QString &part) {
        return rust::WideView{reinterpret_cast<const uint16_t *>(part.utf16()), size_t(part.size())};
    };
    std::transform(pre.cbegin(), pre.cend(), left.begin(), span);
    std::transform(other.pre.cbegin(), other.pre.cend(), right.begin(), span);
    const rust::VersionView a{{major, minor, patch}, left.data(), size_t(left.size())},
        b{{other.major, other.minor, other.patch}, right.data(), size_t(right.size())};
    int32_t result = 0;
    if (rust::okww_rs_version_compare(&a, &b, &result) != 1)
        throw std::runtime_error("Rust 版本比较接口异常");
    return result;
}
bool trustedDownload(const QUrl &url) {
    return rust::platformWorkflowRun(rust::WorkflowMethod::UpdateTrustedDownload, {},
                                     [&](rust::WorkflowOp op, const double *, double *out) {
                                         switch (op) {
                                         case rust::WorkflowOp::UpdateUrlHttps:
                                             out[0] = url.scheme() == "https";
                                             break;
                                         case rust::WorkflowOp::UpdateUrlUserInfoEmpty:
                                             out[0] = url.userInfo().isEmpty();
                                             break;
                                         case rust::WorkflowOp::UpdateUrlPort:
                                             out[0] = url.port(-1);
                                             break;
                                         case rust::WorkflowOp::UpdateUrlTrustedHost:
                                             out[0] = QStringList{"github.com",
                                                                  "release-assets.githubusercontent.com",
                                                                  "objects.githubusercontent.com",
                                                                  "github-releases.githubusercontent.com"}
                                                          .contains(url.host());
                                             break;
                                         default:
                                             throw std::runtime_error("Rust 更新地址宿主无效");
                                         }
                                     })[0] != 0;
}
QList<Release> newerReleases(const QJsonArray &json, const QString &current, bool includePreview) {
    std::optional<Version> installed, version;
    QList<Release> result;
    QSet<QString> seen;
    QJsonObject object, item;
    QString tag, base, name, digest;
    QUrl url;
    Release release;
    const std::array<double, 1> args{double(includePreview)};
    rust::platformWorkflowRun(
        rust::WorkflowMethod::UpdateNewerReleases, args,
        [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::UpdateParseInstalledVersion:
                installed = Version::parse(current);
                break;
            case rust::WorkflowOp::UpdateInstalledValid:
                out[0] = installed.has_value();
                break;
            case rust::WorkflowOp::UpdateReleaseCount:
                out[0] = double(json.size());
                break;
            case rust::WorkflowOp::UpdateSelectRelease:
                object = json.at(int(a[0])).toObject();
                break;
            case rust::WorkflowOp::UpdateReadTag:
                tag = std::as_const(object)["tag_name"].toString();
                break;
            case rust::WorkflowOp::UpdateParseTagVersion:
                version = Version::parse(tag);
                break;
            case rust::WorkflowOp::UpdateTagValid:
                out[0] = version.has_value();
                break;
            case rust::WorkflowOp::UpdateReleaseDraft:
                out[0] = std::as_const(object)["draft"].toBool();
                break;
            case rust::WorkflowOp::UpdatePublicationEmpty:
                out[0] = std::as_const(object)["published_at"].toString().isEmpty();
                break;
            case rust::WorkflowOp::UpdateReleasePreview:
                out[0] = std::as_const(object)["prerelease"].toBool();
                break;
            case rust::WorkflowOp::UpdateVersionHasPreview:
                out[0] = !version->pre.isEmpty();
                break;
            case rust::WorkflowOp::UpdateCompareInstalled:
                out[0] = version->compare(*installed);
                break;
            case rust::WorkflowOp::UpdateTagSeen:
                out[0] = seen.contains(tag);
                break;
            case rust::WorkflowOp::UpdateCreateRelease:
                release = {};
                release.tag = tag;
                release.name = std::as_const(object)["name"].toString(tag);
                release.body = std::as_const(object)["body"].toString();
                release.page = QUrl(product::github + "/releases/tag/" + tag);
                break;
            case rust::WorkflowOp::UpdateAssignPreview:
                release.prerelease = a[0] != 0;
                break;
            case rust::WorkflowOp::UpdateAssetBaseUrl:
                base = product::github + "/releases/download/" + tag + '/';
                break;
            case rust::WorkflowOp::UpdateAssetCount:
                out[0] = double(std::as_const(object)["assets"].toArray().size());
                break;
            case rust::WorkflowOp::UpdateSelectAsset:
                item = std::as_const(object)["assets"].toArray().at(int(a[0])).toObject();
                break;
            case rust::WorkflowOp::UpdateAssetUploaded:
                out[0] = std::as_const(item)["state"] == "uploaded";
                break;
            case rust::WorkflowOp::UpdateReadAssetName:
                name = std::as_const(item)["name"].toString();
                break;
            case rust::WorkflowOp::UpdateReadAssetUrl:
                url = QUrl(std::as_const(item)["browser_download_url"].toString());
                break;
            case rust::WorkflowOp::UpdateAssetUrlMatches:
                out[0] = url == QUrl(base + name) ||
                         url == QUrl(product::legacyGithub + "/releases/download/" + tag + '/' + name);
                break;
            case rust::WorkflowOp::UpdateAssetPackageName:
                out[0] = name == "OK-WW-DualPC-full.zip";
                break;
            case rust::WorkflowOp::UpdateAssignPackageUrl:
                release.package = url;
                break;
            case rust::WorkflowOp::UpdateAssignPackageSize:
                release.size = std::as_const(item)["size"].toInteger();
                break;
            case rust::WorkflowOp::UpdateReadAssetDigest:
                digest = std::as_const(item)["digest"].toString();
                break;
            case rust::WorkflowOp::UpdateDigestSha256Prefix:
                out[0] = digest.startsWith("sha256:");
                break;
            case rust::WorkflowOp::UpdateAssignPackageDigest:
                release.digest = digest.mid(7).toLatin1().toLower();
                break;
            case rust::WorkflowOp::UpdateAssetChecksumsName:
                out[0] = name == "SHA256SUMS.txt";
                break;
            case rust::WorkflowOp::UpdateAssignChecksumsUrl:
                release.checksums = url;
                break;
            case rust::WorkflowOp::UpdateRememberTag:
                seen.insert(tag);
                break;
            case rust::WorkflowOp::UpdateAppendRelease:
                result.append(release);
                break;
            case rust::WorkflowOp::UpdateSortReleases:
                std::sort(result.begin(), result.end(), [](const Release &a, const Release &b) {
                    return Version::parse(a.tag)->compare(*Version::parse(b.tag)) > 0;
                });
                break;
            case rust::WorkflowOp::UpdatePolicyFailure:
                throw std::runtime_error("当前软件版本标识无效");
            default:
                throw std::runtime_error("Rust 更新版本筛选宿主无效");
            }
        });
    return result;
}
QString summary(const QString &body) {
    QStringList input, lines;
    QString line, result;
    rust::platformWorkflowRun(
        rust::WorkflowMethod::UpdateSummary, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::UpdateSplitSummary:
                input = body.split('\n');
                break;
            case rust::WorkflowOp::UpdateSummaryLineCount:
                out[0] = double(input.size());
                break;
            case rust::WorkflowOp::UpdateSelectSummaryLine:
                line = input.at(int(a[0]));
                break;
            case rust::WorkflowOp::UpdateTrimSummaryLine:
                line = line.trimmed();
                break;
            case rust::WorkflowOp::UpdateSummaryLineEmpty:
                out[0] = line.isEmpty();
                break;
            case rust::WorkflowOp::UpdateSummaryIgnoredPrefix:
                out[0] =
                    line.startsWith(QStringList{"#", "下载", "源码", "反馈", "更新时", "验证"}.at(int(a[0])));
                break;
            case rust::WorkflowOp::UpdateAppendSummaryLine:
                lines.append(line.left(int(a[0])));
                break;
            case rust::WorkflowOp::UpdateReturnEmptySummary:
                result = "此版本未填写更新摘要，可在 GitHub 发布页查看。";
                break;
            case rust::WorkflowOp::UpdateJoinSummary:
                result = lines.join('\n');
                break;
            default:
                throw std::runtime_error("Rust 更新摘要宿主无效");
            }
        });
    return result;
}
QByteArray packageChecksum(const QByteArray &text) {
    QRegularExpression pattern;
    QRegularExpressionMatch match;
    QStringList lines;
    QByteArray found;
    rust::platformWorkflowRun(
        rust::WorkflowMethod::UpdatePackageChecksum, {},
        [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::UpdateCreateChecksumPattern:
                pattern = QRegularExpression(R"(^([0-9A-Fa-f]{64})\s+\*?OK-WW-DualPC-full\.zip$)");
                break;
            case rust::WorkflowOp::UpdateSplitChecksumText:
                lines = QString::fromUtf8(text).split('\n');
                break;
            case rust::WorkflowOp::UpdateChecksumLineCount:
                out[0] = double(lines.size());
                break;
            case rust::WorkflowOp::UpdateMatchChecksumLine:
                match = pattern.match(lines.at(int(a[0])).trimmed());
                break;
            case rust::WorkflowOp::UpdateChecksumLineMatches:
                out[0] = match.hasMatch();
                break;
            case rust::WorkflowOp::UpdateAssignChecksum:
                found = match.captured(1).toLatin1().toLower();
                break;
            case rust::WorkflowOp::UpdatePolicyFailure:
                throw std::runtime_error(int(a[0]) == 1 ? "校验文件包含重复的完整包记录"
                                                        : "发布版本缺少完整包 SHA256 校验值");
            default:
                throw std::runtime_error("Rust 更新校验值宿主无效");
            }
        });
    return found;
}
bool Release::installable() const {
    const std::array<double, 1> args{double(size)};
    return rust::platformWorkflowRun(rust::WorkflowMethod::UpdateReleaseInstallable, args,
                                     [&](rust::WorkflowOp op, const double *, double *out) {
                                         switch (op) {
                                         case rust::WorkflowOp::UpdatePackageUrlEmpty:
                                             out[0] = package.isEmpty();
                                             break;
                                         case rust::WorkflowOp::UpdateChecksumsUrlEmpty:
                                             out[0] = checksums.isEmpty();
                                             break;
                                         default:
                                             throw std::runtime_error("Rust 更新附件宿主无效");
                                         }
                                     })[0] != 0;
}

void Client::runUpdate(int method, UpdateCall call) {
    QNetworkReply *saved = nullptr;
    QNetworkReply::NetworkError networkError = QNetworkReply::NoError;
    int status = 0;
    QString networkMessage;
    QUrl finalUrl;
    QByteArray chunk;
    std::optional<QNetworkRequest> request;
    QJsonParseError parseError;
    QJsonDocument document;
    QJsonArray entries;
    QList<Release> result;
    rust::platformWorkflowRun(
        rust::WorkflowMethod(method), call.args,
        [&](rust::WorkflowOp op, const double *a, double *out, const std::exception_ptr &error) {
            switch (op) {
            case rust::WorkflowOp::UpdateClientSingleShot:
                deadline_.setSingleShot(true);
                break;
            case rust::WorkflowOp::UpdateClientBindTimeout:
                connect(&deadline_, &QTimer::timeout, this,
                        [this] { runUpdate(int(rust::WorkflowMethod::UpdateClientTimeout), {}); });
                break;
            case rust::WorkflowOp::UpdateClientCancelNested:
                cancel();
                break;
            case rust::WorkflowOp::UpdateClientEmitFailure:
                emit failed(*call.text);
                break;
            case rust::WorkflowOp::UpdateClientSetBusy:
                busy_ = a[0] != 0;
                break;
            case rust::WorkflowOp::UpdateClientStopDeadline:
                deadline_.stop();
                break;
            case rust::WorkflowOp::UpdateClientReplyPresent:
                out[0] = bool(reply_);
                break;
            case rust::WorkflowOp::UpdateClientSaveReply:
                saved = reply_.data();
                break;
            case rust::WorkflowOp::UpdateClientClearReply:
                reply_ = nullptr;
                break;
            case rust::WorkflowOp::UpdateClientDisconnectReply:
                saved->disconnect(this);
                break;
            case rust::WorkflowOp::UpdateClientAbortReply:
                saved->abort();
                break;
            case rust::WorkflowOp::UpdateClientDeleteReplyLater:
                saved->deleteLater();
                break;
            case rust::WorkflowOp::UpdateClientOutputPresent:
                out[0] = output_ != nullptr;
                break;
            case rust::WorkflowOp::UpdateClientCancelOutput:
                output_->cancelWriting();
                break;
            case rust::WorkflowOp::UpdateClientDeleteOutput:
                delete output_;
                break;
            case rust::WorkflowOp::UpdateClientResetOutput:
                output_ = nullptr;
                break;
            case rust::WorkflowOp::UpdateClientClearBuffer:
                buffer_.clear();
                break;
            case rust::WorkflowOp::UpdateClientResetReceived:
                received_ = 0;
                break;
            case rust::WorkflowOp::UpdateClientCreateRequest:
                request.emplace(*call.url);
                break;
            case rust::WorkflowOp::UpdateClientUserAgent:
                request->setRawHeader("User-Agent", (product::name + '/' + product::releaseVersion).toUtf8());
                break;
            case rust::WorkflowOp::UpdateClientAcceptHeader:
                request->setRawHeader("Accept", "application/vnd.github+json");
                break;
            case rust::WorkflowOp::UpdateClientRedirectPolicy:
                request->setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                      QNetworkRequest::NoLessSafeRedirectPolicy);
                break;
            case rust::WorkflowOp::UpdateClientTransferTimeout:
                request->setTransferTimeout(int(a[0]));
                break;
            case rust::WorkflowOp::UpdateClientHasTransport:
                out[0] = bool(transport_);
                break;
            case rust::WorkflowOp::UpdateClientStartTransport:
                reply_ = transport_(*request);
                break;
            case rust::WorkflowOp::UpdateClientStartNetwork:
                reply_ = network_.get(*request);
                break;
            case rust::WorkflowOp::UpdateClientReadBufferLimit:
                reply_->setReadBufferSize(qint64(a[0]));
                break;
            case rust::WorkflowOp::UpdateClientStartDeadline:
                deadline_.start(int(a[0]));
                break;
            case rust::WorkflowOp::UpdateClientBindReadyRead:
                connect(reply_, &QNetworkReply::readyRead, this,
                        [this, limit = qint64(a[0]), file = a[1] != 0] {
                            const std::array<double, 2> args{double(limit), double(file)};
                            runUpdate(int(rust::WorkflowMethod::UpdateClientReadyRead), {.args = args});
                        });
                break;
            case rust::WorkflowOp::UpdateClientBindFinished:
                connect(reply_, &QNetworkReply::finished, this, [this, done = std::move(*call.ownedDone)] {
                    runUpdate(int(rust::WorkflowMethod::UpdateClientFinished), {.callback = &done});
                });
                break;
            case rust::WorkflowOp::UpdateClientReadChunk:
                chunk = reply_->readAll();
                break;
            case rust::WorkflowOp::UpdateClientAccumulateReceived:
                received_ += chunk.size();
                break;
            case rust::WorkflowOp::UpdateClientReceived:
                out[0] = double(received_);
                break;
            case rust::WorkflowOp::UpdateClientFailLargeFile:
                fail("GitHub 返回的文件超过预期大小。");
                break;
            case rust::WorkflowOp::UpdateClientWriteChunk:
                out[0] = output_->write(chunk) == chunk.size();
                break;
            case rust::WorkflowOp::UpdateClientFailWrite:
                fail("无法写入更新包，请检查剩余磁盘空间。");
                break;
            case rust::WorkflowOp::UpdateClientHashChunk:
                hash_.addData(chunk);
                break;
            case rust::WorkflowOp::UpdateClientEmitProgress:
                emit progress(received_, selected_.size);
                break;
            case rust::WorkflowOp::UpdateClientAppendChunk:
                buffer_.append(chunk);
                break;
            case rust::WorkflowOp::UpdateClientSavedReplyPresent:
                out[0] = saved != nullptr;
                break;
            case rust::WorkflowOp::UpdateClientReadHttpStatus:
                status = saved->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                break;
            case rust::WorkflowOp::UpdateClientReadNetworkError:
                networkError = saved->error();
                break;
            case rust::WorkflowOp::UpdateClientReadNetworkMessage:
                networkMessage = saved->errorString();
                break;
            case rust::WorkflowOp::UpdateClientReadFinalUrl:
                finalUrl = saved->url();
                break;
            case rust::WorkflowOp::UpdateClientHttpStatus:
                out[0] = status;
                break;
            case rust::WorkflowOp::UpdateClientNetworkError:
                out[0] = int(networkError);
                break;
            case rust::WorkflowOp::UpdateClientFailRateLimit:
                fail("GitHub 请求次数已达上限，请稍后重试。");
                break;
            case rust::WorkflowOp::UpdateClientFailNetwork:
                fail(QString("无法获取更新（HTTP %1）：%2").arg(status).arg(networkMessage));
                break;
            case rust::WorkflowOp::UpdateClientFinalApiHost:
                out[0] = finalUrl.host() == "api.github.com";
                break;
            case rust::WorkflowOp::UpdateClientFinalTrusted:
                out[0] = trustedDownload(finalUrl);
                break;
            case rust::WorkflowOp::UpdateClientFailRedirect:
                fail("更新下载被重定向到非 GitHub 地址。");
                break;
            case rust::WorkflowOp::UpdateClientInvokeDone:
                (*call.callback)();
                break;
            case rust::WorkflowOp::UpdateClientFailDoneException:
                try {
                    std::rethrow_exception(error);
                } catch (const std::exception &e) {
                    fail(QString::fromUtf8(e.what()));
                }
                break;
            case rust::WorkflowOp::UpdateClientBusy:
                out[0] = busy_;
                break;
            case rust::WorkflowOp::UpdateClientAssignCurrent:
                current_ = *call.text;
                break;
            case rust::WorkflowOp::UpdateClientAssignPreviews:
                previews_ = a[0] != 0;
                break;
            case rust::WorkflowOp::UpdateClientClearReleases:
                releases_ = {};
                break;
            case rust::WorkflowOp::UpdateClientFirstPage:
                page_ = 1;
                break;
            case rust::WorkflowOp::UpdateClientGetPageNested:
                getPage();
                break;
            case rust::WorkflowOp::UpdateClientRequestPage:
                get(QUrl("https://api.github.com/repositories/" + QString::number(product::githubRepositoryId) +
                         "/releases?per_page=100&page=" + QString::number(page_)),
                    8 * 1024 * 1024,
                    [this] { runUpdate(int(rust::WorkflowMethod::UpdateClientPageReply), {}); });
                break;
            case rust::WorkflowOp::UpdateClientParseReleasePage:
                document = QJsonDocument::fromJson(buffer_, &parseError);
                break;
            case rust::WorkflowOp::UpdateClientPageParseOk:
                out[0] = parseError.error == QJsonParseError::NoError;
                break;
            case rust::WorkflowOp::UpdateClientPageArray:
                out[0] = document.isArray();
                break;
            case rust::WorkflowOp::UpdateClientReadPageEntries:
                entries = document.array();
                break;
            case rust::WorkflowOp::UpdateClientPageEntryCount:
                out[0] = double(entries.size());
                break;
            case rust::WorkflowOp::UpdateClientAppendPageEntry:
                releases_.append(entries.at(int(a[0])));
                break;
            case rust::WorkflowOp::UpdateClientPageNumber:
                out[0] = page_;
                break;
            case rust::WorkflowOp::UpdateClientNextPage:
                ++page_;
                break;
            case rust::WorkflowOp::UpdateClientFilterReleases:
                result = newerReleases(releases_, current_, previews_);
                break;
            case rust::WorkflowOp::UpdateClientEmitChecked:
                emit checked(result);
                break;
            case rust::WorkflowOp::UpdateClientReleaseInstallable:
                out[0] = call.release->installable();
                break;
            case rust::WorkflowOp::UpdateClientReleasePackageTrusted:
                out[0] = trustedDownload(call.release->package);
                break;
            case rust::WorkflowOp::UpdateClientReleaseChecksumsTrusted:
                out[0] = trustedDownload(call.release->checksums);
                break;
            case rust::WorkflowOp::UpdateClientReleaseSize:
                out[0] = double(call.release->size);
                break;
            case rust::WorkflowOp::UpdateClientEmitUnavailable:
                emit failed("此版本未提供可用的完整包和校验文件，请前往 GitHub 下载。");
                break;
            case rust::WorkflowOp::UpdateClientAssignRelease:
                selected_ = *call.release;
                break;
            case rust::WorkflowOp::UpdateClientAssignJob:
                job_ = *call.text;
                break;
            case rust::WorkflowOp::UpdateClientRequestChecksums:
                get(selected_.checksums, 128 * 1024,
                    [this] { runUpdate(int(rust::WorkflowMethod::UpdateClientChecksumReply), {}); });
                break;
            case rust::WorkflowOp::UpdateClientReadExpectedChecksum:
                expected_ = packageChecksum(buffer_);
                break;
            case rust::WorkflowOp::UpdateClientSelectedDigestEmpty:
                out[0] = selected_.digest.isEmpty();
                break;
            case rust::WorkflowOp::UpdateClientDigestMatches:
                out[0] = selected_.digest == expected_;
                break;
            case rust::WorkflowOp::UpdateClientCreateOutput:
                output_ = new QSaveFile(job_ + "/package.zip");
                break;
            case rust::WorkflowOp::UpdateClientOpenOutput:
                out[0] = output_->open(QIODevice::WriteOnly);
                break;
            case rust::WorkflowOp::UpdateClientResetHash:
                hash_.reset();
                break;
            case rust::WorkflowOp::UpdateClientRequestPackage:
                get(
                    selected_.package, selected_.size,
                    [this] { runUpdate(int(rust::WorkflowMethod::UpdateClientPackageReply), {}); }, true);
                break;
            case rust::WorkflowOp::UpdateClientReceivedSizeMatches:
                out[0] = received_ == selected_.size;
                break;
            case rust::WorkflowOp::UpdateClientPackageHashMatches:
                out[0] = hash_.result().toHex() == expected_;
                break;
            case rust::WorkflowOp::UpdateClientCommitOutput:
                out[0] = output_->commit();
                break;
            case rust::WorkflowOp::UpdateClientEmitDownloaded:
                emit downloaded(job_ + "/package.zip", QString::fromLatin1(expected_));
                break;
            case rust::WorkflowOp::UpdateClientFailTimeout:
                fail("连接 GitHub 超时，请检查网络后重试。");
                break;
            case rust::WorkflowOp::UpdateClientThrow:
                throw std::runtime_error(QStringList{
                    "GitHub 版本列表格式无效", "发布附件与校验文件不一致，已取消更新", "无法创建更新包文件",
                    "更新包大小或 SHA256 校验失败，请重新下载", "保存更新包失败，请检查剩余空间"}
                                             .at(int(a[0]))
                                             .toUtf8()
                                             .constData());
            default:
                throw std::runtime_error("Rust 更新客户端宿主无效");
            }
        });
}
Client::Client(QObject *parent, std::function<QNetworkReply *(const QNetworkRequest &)> transport)
    : QObject(parent), network_(this), transport_(std::move(transport)) {
    runUpdate(int(rust::WorkflowMethod::UpdateClientInitialize), {});
}
void Client::fail(const QString &message) {
    runUpdate(int(rust::WorkflowMethod::UpdateClientFail), {.text = &message});
}
void Client::cancel() { runUpdate(int(rust::WorkflowMethod::UpdateClientCancel), {}); }
void Client::get(const QUrl &url, qint64 limit, std::function<void()> done, bool file) {
    const std::array<double, 2> args{double(limit), double(file)};
    runUpdate(int(rust::WorkflowMethod::UpdateClientGet), {.args = args, .url = &url, .ownedDone = &done});
}
void Client::check(const QString &current, bool includePreview) {
    const std::array<double, 1> args{double(includePreview)};
    runUpdate(int(rust::WorkflowMethod::UpdateClientCheck), {.args = args, .text = &current});
}
void Client::getPage() { runUpdate(int(rust::WorkflowMethod::UpdateClientGetPage), {}); }
void Client::download(const Release &release, const QString &job) {
    runUpdate(int(rust::WorkflowMethod::UpdateClientDownload), {.text = &job, .release = &release});
}
} // namespace okww::update
