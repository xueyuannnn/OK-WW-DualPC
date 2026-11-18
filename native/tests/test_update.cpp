#include "common.hpp"
#include "update.hpp"
#include "app_info.hpp"
#include <QtTest>
using namespace okww;
class MemoryReply final : public QNetworkReply {
    QByteArray body_;
    qint64 offset_=0;
  public:
    MemoryReply(const QNetworkRequest &request, QByteArray body, int status=200) : body_(std::move(body)) {
        setRequest(request); setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute,status);
        if (status!=200) setError(QNetworkReply::ContentAccessDenied,"fixture HTTP error");
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0,this,[this] { emit readyRead(); setFinished(true); emit finished(); });
    }
    void abort() override {}
    qint64 bytesAvailable() const override { return body_.size()-offset_+QNetworkReply::bytesAvailable(); }
    qint64 readData(char *data,qint64 maximum) override {
        const auto count=std::min(maximum,body_.size()-offset_);
        if (!count) return -1;
        memcpy(data,body_.constData()+offset_,size_t(count)); offset_+=count; return count;
    }
};
class UpdateTests final : public QObject {
    Q_OBJECT
    static QJsonObject release(QString tag, bool draft=false, bool preview=true, bool complete=true) {
        const auto base=product::github+"/releases/download/"+tag+'/';
        QJsonArray assets;
        if (complete) {
            assets.append(QJsonObject{{"name","OK-WW-DualPC-full.zip"},{"state","uploaded"},{"size",123},
                                      {"browser_download_url",base+"OK-WW-DualPC-full.zip"}});
            assets.append(QJsonObject{{"name","SHA256SUMS.txt"},{"state","uploaded"},
                                      {"browser_download_url",base+"SHA256SUMS.txt"}});
        }
        return {{"tag_name",tag},{"draft",draft},{"prerelease",preview},{"published_at","2026-11-14T00:00:00Z"},
                {"body","- 修复队伍切换\n- 改善截图\n\n下载完整包"},{"assets",assets}};
    }
  private slots:
    void semanticOrdering() {
        const QStringList ordered{"0.1.9","0.2.0-alpha.1","0.2.0-beta.7","v0.2.0-beta.8",
                                  "0.2.0-beta.10","0.2.0-rc.1","0.2.0","0.2.1","1.0.0"};
        for (int i=1;i<ordered.size();++i) {
            const auto a=update::Version::parse(ordered[i-1]),b=update::Version::parse(ordered[i]);
            QVERIFY(a && b); QVERIFY(a->compare(*b)<0); QVERIFY(b->compare(*a)>0);
        }
        QCOMPARE(update::Version::parse("v0.2.0+build.17")->compare(*update::Version::parse("0.2.0")),0);
        QVERIFY(!update::Version::parse("0.2.0-beta.01"));
        QVERIFY(!update::Version::parse("0.2.0.1"));
        QVERIFY(!update::Version::parse("01.2.0"));
        QVERIFY(!update::Version::parse("99999999999999999999999.2.0"));
    }
    void releaseSelectionDoesNotTrustApiOrderOrDowngrade() {
        QJsonArray json{release("v0.2.0-beta.7"),release("v0.3.0",true,false),release("v0.2.0-beta.10"),
                        release("v0.2.0",false,false),release("v0.2.0-beta.9",false,true,false),release("v0.2.0-beta.10")};
        const auto all=update::newerReleases(json,"0.2.0-beta.8",true);
        QCOMPARE(all.size(),3); QCOMPARE(all[0].tag,"v0.2.0"); QCOMPARE(all[1].tag,"v0.2.0-beta.10");
        QVERIFY(all[0].installable()); QVERIFY(!all[2].installable());
        const auto stable=update::newerReleases(json,"0.2.0-beta.8",false);
        QCOMPARE(stable.size(),1); QCOMPARE(stable[0].tag,"v0.2.0");
        QVERIFY(update::newerReleases(json,"0.2.0",true).isEmpty());
        QVERIFY_EXCEPTION_THROWN(update::newerReleases(json,"unknown",true),std::runtime_error);
    }
    void downloadBoundariesAndChecksums() {
        auto item=release("v0.2.1",false,false);
        auto assets=item["assets"].toArray();
        auto file=assets[0].toObject();
        file["browser_download_url"]="https://github.com/other/project/releases/download/v0.2.1/OK-WW-DualPC-full.zip";
        assets[0]=file; item["assets"]=assets;
        QVERIFY(!update::newerReleases({item},"0.2.0",true).first().installable());
        QVERIFY(update::trustedDownload(QUrl("https://release-assets.githubusercontent.com/file?sig=abc")));
        QVERIFY(!update::trustedDownload(QUrl("https://github.com.evil.invalid/file")));
        QVERIFY(!update::trustedDownload(QUrl("http://github.com/file")));
        QVERIFY(!update::trustedDownload(QUrl("https://user:pass@github.com/file")));
        QVERIFY(!update::trustedDownload(QUrl("https://github.com:444/file")));
        const QByteArray sha(64,'a');
        QCOMPARE(update::packageChecksum(sha+"  OK-WW-DualPC-source.zip\r\n"+sha+" *OK-WW-DualPC-full.zip\r\n"),sha);
        QVERIFY_EXCEPTION_THROWN(update::packageChecksum("bad  OK-WW-DualPC-full.zip"),std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(update::packageChecksum(sha+"  OK-WW-DualPC-full.zip\n"+sha+"  OK-WW-DualPC-full.zip\n"),std::runtime_error);
    }
    void summariesAndCancellation() {
        QCOMPARE(update::summary("# 更新\n- 改善采集\n\n- 修复连招\n下载完整包\n反馈邮箱"),"- 改善采集\n- 修复连招");
        QVERIFY(update::Version::parse(product::releaseVersion));
        update::Client client;
        client.check(product::releaseVersion);
        QVERIFY(client.busy());
        client.cancel();
        QVERIFY(!client.busy());
    }
    void renamedRepositoryAndLegacyAssetUrlsRemainInstallable() {
        for (const auto &project : {product::github, product::legacyGithub}) {
            auto item = release("v0.2.1",false,false);
            auto assets = item["assets"].toArray();
            for (int i=0;i<assets.size();++i) {
                auto file = assets[i].toObject();
                file["browser_download_url"] = project + "/releases/download/v0.2.1/" + file["name"].toString();
                assets[i] = file;
            }
            item["assets"] = assets;
            const auto selected = update::newerReleases({item},"0.2.0",true);
            QCOMPARE(selected.size(),1);
            QVERIFY(selected.first().installable());
            QCOMPARE(selected.first().page,QUrl(product::github + "/releases/tag/v0.2.1"));
        }
        for (const auto &project : {QString("https://github.com/xueyuannnn/other-project"),
                                    product::github + "-other", QString("http://github.com/xueyuannnn/okww-dualpc")}) {
            auto item = release("v0.2.1",false,false);
            auto assets = item["assets"].toArray();
            auto file = assets[0].toObject();
            file["browser_download_url"] = project + "/releases/download/v0.2.1/" + file["name"].toString();
            assets[0] = file;
            item["assets"] = assets;
            QVERIFY(!update::newerReleases({item},"0.2.0",true).first().installable());
        }
    }
    void checkAndDownloadThroughTheNetworkStateMachine() {
        QTemporaryDir job;
        const QByteArray package="fixture package bytes";
        const auto sha=QCryptographicHash::hash(package,QCryptographicHash::Sha256).toHex();
        QStringList requested;
        update::Client client(nullptr,[&](const QNetworkRequest &request) {
            requested.append(request.url().toString());
            QByteArray body;
            if (request.url().host()=="api.github.com") body=QJsonDocument(QJsonArray{release("v0.2.0-beta.10")}).toJson();
            else if (request.url().path().endsWith("SHA256SUMS.txt")) body=sha+"  OK-WW-DualPC-full.zip\n";
            else body=package;
            return new MemoryReply(request,body);
        });
        QSignalSpy checked(&client,&update::Client::checked),failed(&client,&update::Client::failed);
        client.check("0.2.0-beta.8");
        QTRY_COMPARE(checked.size(),1); QCOMPARE(failed.size(),0);
        QVERIFY(requested.first().startsWith("https://api.github.com/repositories/" +
                QString::number(product::githubRepositoryId) + "/releases?"));
        auto selected=update::newerReleases({release("v0.2.0-beta.10")},"0.2.0-beta.8",true).first();
        selected.size=package.size(); selected.digest=sha;
        QSignalSpy downloaded(&client,&update::Client::downloaded);
        client.download(selected,job.path());
        QTRY_COMPARE(downloaded.size(),1); QCOMPARE(failed.size(),0);
        QFile output(job.path()+"/package.zip"); QVERIFY(output.open(QIODevice::ReadOnly)); QCOMPARE(output.readAll(),package);
        QCOMPARE(requested.size(),3);
    }
    void corruptDownloadsAndHttpErrorsCannotProduceAnInstallableFile() {
        for (int status:{200,429}) {
            QTemporaryDir job;
            update::Client client(nullptr,[status](const QNetworkRequest &request) {
                const QByteArray body=request.url().path().endsWith("SHA256SUMS.txt") ?
                    QByteArray(64,'a')+"  OK-WW-DualPC-full.zip\n" : QByteArray("corrupt");
                return new MemoryReply(request,body,status);
            });
            auto selected=update::newerReleases({release("v0.2.0-beta.10")},"0.2.0-beta.8",true).first();
            selected.size=7;
            QSignalSpy failed(&client,&update::Client::failed),downloaded(&client,&update::Client::downloaded);
            client.download(selected,job.path());
            QTRY_COMPARE(failed.size(),1); QCOMPARE(downloaded.size(),0);
            QVERIFY(!QFile::exists(job.path()+"/package.zip")); QVERIFY(!client.busy());
        }
    }
    void githubPublicReleaseProbe() {
        if (!qEnvironmentVariableIsSet("OKWW_TEST_GITHUB_UPDATES")) QSKIP("Live GitHub probe is opt-in");
        update::Client client;
        QSignalSpy checked(&client,&update::Client::checked),failed(&client,&update::Client::failed);
        client.check("0.2.0-beta.1");
        QTRY_VERIFY_WITH_TIMEOUT(!checked.isEmpty() || !failed.isEmpty(),35000);
        QVERIFY2(failed.isEmpty(),failed.isEmpty()?"":qPrintable(failed.first().first().toString()));
        const auto releases=qvariant_cast<QList<update::Release>>(checked.first().first());
        QVERIFY(!releases.isEmpty()); QVERIFY(releases.first().installable());
        const auto evidence=qEnvironmentVariable("OKWW_UI_ARTIFACTS");
        if (!evidence.isEmpty()) {
            QJsonArray list;
            for (const auto &release:releases) list.append(QJsonObject{{"tag",release.tag},{"summary",update::summary(release.body)},
                                                                     {"package",release.package.toString()},{"installable",release.installable()}});
            QFile output(evidence+"/github-releases.json"); QVERIFY(output.open(QIODevice::WriteOnly)); output.write(QJsonDocument(list).toJson());
        }
    }
    void installationLockPreventsLaunchingIntoPartiallyUpdatedFiles() {
        QTemporaryDir root;
        QVERIFY(QDir().mkpath(root.path()+"/.updates"));
        QVERIFY(!update::installationBusy(root.path()));
        const auto path=QDir::toNativeSeparators(root.path()+"/.updates/install.lock");
        const auto file=CreateFileW(reinterpret_cast<const wchar_t *>(path.utf16()),GENERIC_READ|GENERIC_WRITE,
                                    0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        QVERIFY(file!=INVALID_HANDLE_VALUE);
        QVERIFY(update::installationBusy(root.path()));
        CloseHandle(file);
        QVERIFY(!update::installationBusy(root.path()));
    }
};
QTEST_GUILESS_MAIN(UpdateTests)
#include "test_update.moc"
