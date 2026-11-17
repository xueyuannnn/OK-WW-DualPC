#include "window.hpp"
#include <QDesktopServices>

namespace okww {
namespace {
QString powershell() {
    return qEnvironmentVariable("SystemRoot", "C:/Windows") + "/System32/WindowsPowerShell/v1.0/powershell.exe";
}
void hiddenProcess(QProcess &process) {
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
        args->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
        args->startupInfo->wShowWindow = SW_HIDE;
    });
}
QStringList installerArguments(const QString &plan, const QString &mode) {
    return {"-NoLogo", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File",
            QFileInfo(plan).absolutePath()+"/install.ps1", "-Plan", plan, "-Mode", mode};
}
void discardUpdateJob(const QString &root, const QString &job) {
    if (job.isEmpty()) return;
    (void)QtConcurrent::run([root,job] {
        const auto base=QDir(root).canonicalPath()+"/.updates";
        const QFileInfo directory(job);
        if (!directory.fileName().startsWith("job-") || directory.isSymLink() ||
            directory.canonicalPath().compare(base,Qt::CaseInsensitive)!=0 ||
            QDir(base).canonicalPath().compare(base,Qt::CaseInsensitive)!=0) return;
        QFile owner(job+"/.owner");
        if (!owner.open(QIODevice::ReadOnly) || owner.readAll()!=product::name.toUtf8()) return;
        owner.close();
        QDir(job).removeRecursively();
    });
}
} // namespace
QWidget *Window::buildAppUpdates() {
    auto card = new QFrame;
    card->setObjectName("classicCard");
    card->setProperty("aboutSection", "updates");
    auto layout = new QVBoxLayout(card);
    layout->setContentsMargins(16,14,16,14);
    layout->setSpacing(10);
    auto row = new QHBoxLayout;
    auto titles = new QVBoxLayout;
    auto heading = new QLabel("版本更新");
    heading->setObjectName("sectionTitle");
    titles->addWidget(heading);
    updateStatus_ = new QLabel("当前版本 " + product::releaseVersion + " · 更新来自本项目 GitHub Releases");
    updateStatus_->setObjectName("appUpdateStatus");
    updateStatus_->setWordWrap(true);
    titles->addWidget(updateStatus_);
    row->addLayout(titles,1);
    checkUpdates_ = new QPushButton("检查更新");
    checkUpdates_->setObjectName("checkAppUpdates");
    classic::setFluentIcon(checkUpdates_,"Sync");
    row->addWidget(checkUpdates_);
    layout->addLayout(row);
    auto project = new QLabel("<a href=\"" + product::github + "\">" + product::github + "</a>");
    project->setObjectName("projectGithubUrl");
    project->setOpenExternalLinks(true);
    project->setTextInteractionFlags(Qt::TextBrowserInteraction);
    layout->addWidget(project);
    auto options = new QHBoxLayout;
    auto automatic = new QCheckBox("启动时自动检查更新");
    automatic->setObjectName("autoCheckAppUpdates");
    automatic->setChecked(uiPreferences_["check_app_updates"].toBool(true));
    auto preview = new QCheckBox("包含预览版");
    preview->setObjectName("includePreviewUpdates");
    preview->setChecked(uiPreferences_["preview_app_updates"].toBool(true));
    options->addWidget(automatic);
    options->addWidget(preview);
    options->addStretch();
    layout->addLayout(options);
    auto hint = new QLabel("检查更新不会上传配置。确认后下载完整包，校验通过再替换；保留配置和工坊，重新打开后保持停止。");
    hint->setObjectName("caption");
    hint->setWordWrap(true);
    layout->addWidget(hint);
    const auto save = [this] {
        try { writeJson(data_+"/native-ui.json",uiPreferences_); }
        catch (const std::exception &e) { log(errorText(e)); }
    };
    connect(automatic,&QCheckBox::toggled,this,[this,save](bool enabled) { uiPreferences_["check_app_updates"]=enabled; save(); });
    connect(preview,&QCheckBox::toggled,this,[this,save](bool enabled) {
        uiPreferences_["preview_app_updates"]=enabled; save();
        appReleases_.clear();
        checkUpdates_->setText("检查更新");
    });
    connect(checkUpdates_,&QPushButton::clicked,this,[this] {
        if (!appReleases_.isEmpty()) showAppUpdates(); else checkAppUpdates(true);
    });
    connect(updateClient_,&update::Client::checked,this,[this](QList<update::Release> releases) {
        checkUpdates_->setEnabled(true);
        appReleases_=std::move(releases);
        if (appReleases_.isEmpty()) {
            updateStatus_->setText("未发现更新版本（当前 "+product::releaseVersion+"）。");
            checkUpdates_->setText("检查更新");
            return;
        }
        updateStatus_->setText(QString("发现 %1 个新版本 · 最新 %2").arg(appReleases_.size()).arg(appReleases_.first().tag));
        checkUpdates_->setText("查看新版本");
        if (manualUpdateCheck_) showAppUpdates();
        else if (tray_ && tray_->isVisible()) tray_->showMessage(product::name,"有新版本可用，在“关于”中查看并更新。",QSystemTrayIcon::Information,5000);
    });
    connect(updateClient_,&update::Client::failed,this,[this](const QString &message) {
        checkUpdates_->setEnabled(true);
        updateStatus_->setText(message+" 可使用上方 GitHub 入口手动下载。");
        log("更新检查或下载失败："+message);
    });
    return card;
}
void Window::checkAppUpdates(bool manual) {
    if (closing_ || updateClient_->busy() || updatePrepare_ || updateDialog_) return;
    if (!manual && !uiPreferences_["check_app_updates"].toBool(true)) return;
    manualUpdateCheck_=manual;
    checkUpdates_->setEnabled(false);
    updateStatus_->setText("正在连接 GitHub 检查更新…");
    updateClient_->check(product::releaseVersion,uiPreferences_["preview_app_updates"].toBool(true));
}
void Window::showAppUpdates() {
    if (appReleases_.isEmpty()) return;
    if (updateDialog_) { updateDialog_->show(); updateDialog_->raise(); return; }
    auto dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setObjectName("appUpdateDialog");
    dialog->setWindowTitle("软件更新 · "+product::name);
    updateDialog_=dialog;
    const auto latest=appReleases_.first();
    auto layout=classic::dialogLayout(*dialog,"发现新版本 "+latest.tag,
        "当前 "+product::releaseVersion+"。以下为各版本摘要，确认后更新至最新版本。",{760,580});
    auto notes=new QTextBrowser;
    notes->setObjectName("appReleaseNotes");
    notes->setOpenExternalLinks(true);
    QString html;
    for (const auto &release:appReleases_) {
        html += "<h3>"+release.tag.toHtmlEscaped()+(release.prerelease?" · 预览版":"")+"</h3><p>"+
                update::summary(release.body).toHtmlEscaped().replace('\n',"<br>")+"</p><p><a href=\""+
                release.page.toString().toHtmlEscaped()+"\">查看完整发布说明</a></p>";
    }
    notes->setHtml(html);
    layout->addWidget(notes,1);
    auto status=new QLabel("更新完成后会重新打开软件，任务和连接保持停止。更新前请结束当前游戏操作。");
    status->setObjectName("appUpdateProgressText");
    status->setWordWrap(true);
    layout->addWidget(status);
    auto progress=new QProgressBar;
    progress->setObjectName("appUpdateProgress");
    progress->setRange(0,1000);
    progress->hide();
    layout->addWidget(progress);
    auto buttons=new QHBoxLayout;
    auto github=new QPushButton("前往 GitHub");
    classic::setFluentIcon(github,"GitHub");
    connect(github,&QPushButton::clicked,dialog,[latest] { QDesktopServices::openUrl(latest.page); });
    buttons->addWidget(github);
    buttons->addStretch();
    auto cancel=new QPushButton("暂不更新");
    connect(cancel,&QPushButton::clicked,dialog,&QDialog::reject);
    buttons->addWidget(cancel);
    auto install=new QPushButton("下载并更新");
    install->setObjectName("installAppUpdate");
    install->setProperty("primary",true);
    install->setEnabled(latest.installable());
    if (!latest.installable()) status->setText("最新版本尚未提供完整包和校验文件，请前往 GitHub 查看。");
    buttons->addWidget(install);
    layout->addLayout(buttons);
    connect(install,&QPushButton::clicked,dialog,[this,dialog,latest,install,cancel,progress,status] {
        install->setEnabled(false);
        cancel->setText("取消更新");
        progress->show();
        status->setText("正在下载并校验更新包…");
        prepareAppUpdate(latest,dialog);
    });
    connect(updateClient_,&update::Client::progress,dialog,[status,progress](qint64 received,qint64 total) {
        progress->setValue(total>0?int(received*1000/total):0);
        status->setText(QString("正在下载 %1 / %2 MB").arg(received/1048576.0,0,'f',1).arg(total/1048576.0,0,'f',1));
    });
    connect(updateClient_,&update::Client::failed,dialog,[install,status,progress,latest](const QString &message) {
        status->setText(message); progress->hide(); install->setEnabled(latest.installable());
    });
    connect(dialog,&QDialog::rejected,this,[this,dialog] {
        pendingUpdatePlan_.clear();
        updateClient_->cancel();
        const auto job=dialog->property("updateJob").toString();
        if (updatePrepare_) {
            auto process=updatePrepare_.data();
            disconnect(process,nullptr,dialog,nullptr);
            connect(process,&QProcess::finished,this,[this,job,process] { discardUpdateJob(root_,job); process->deleteLater(); });
            process->kill(); updatePrepare_=nullptr;
        } else discardUpdateJob(root_,job);
        checkUpdates_->setEnabled(true);
    });
    dialog->show();
}
void Window::prepareAppUpdate(const update::Release &release, QDialog *dialog) {
    const auto status=dialog->findChild<QLabel *>("appUpdateProgressText");
    const auto install=dialog->findChild<QPushButton *>("installAppUpdate");
    const auto failure=[status,install](const QString &message) { status->setText(message); install->setEnabled(true); };
    try {
        const auto executable=QCoreApplication::applicationFilePath();
        if (QFileInfo(root_).canonicalFilePath()!=QFileInfo(QCoreApplication::applicationDirPath()).canonicalFilePath() ||
            QFileInfo(executable).fileName()!="OKWW-DualPC-Native-Dev.exe" || !QFile::exists(root_+"/PACKAGE.json"))
            fail("当前是源码开发目录，请从 GitHub 下载完整包后使用自动更新。");
        const auto root=QDir(root_).absolutePath();
        if (QDir(data_).absolutePath().compare(root,Qt::CaseInsensitive)==0) fail("配置需放在独立目录中，请手动更新。");
        discardUpdateJob(root,dialog->property("updateJob").toString());
        const auto job=root+"/.updates/job-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (QFileInfo(root+"/.updates").isSymLink()) fail("更新目录不能是链接，请手动更新。");
        if (!QDir().mkpath(job)) fail("无法创建更新目录，请检查目录写入权限。");
        dialog->setProperty("updateJob",job);
        QFile owner(job+"/.owner");
        if (!owner.open(QIODevice::WriteOnly) || owner.write(product::name.toUtf8())!=product::name.toUtf8().size()) fail("无法写入更新目录。");
        owner.close();
        const QStorageInfo storage(root);
        if (storage.bytesAvailable()<release.size*3+512LL*1024*1024) fail("剩余空间不足，请至少预留完整包大小三倍加 512 MB。");
        QFile script(":/updater/install.ps1");
        if (!script.open(QIODevice::ReadOnly)) fail("缺少更新组件，请前往 GitHub 下载。");
        QFile copy(job+"/install.ps1");
        // Windows PowerShell 5 reads UTF-8 scripts reliably with a BOM.
        if (!copy.open(QIODevice::WriteOnly) || copy.write(QByteArray::fromHex("efbbbf")+script.readAll())<0) fail("无法准备更新组件。");
        copy.close();
        const auto plan=job+"/plan.json";
        const auto connection=std::make_shared<QMetaObject::Connection>();
        *connection=connect(updateClient_,&update::Client::downloaded,dialog,
            [this,dialog,status,install,job,plan,release,connection](const QString &,const QString &sha) {
            disconnect(*connection);
            try {
                FILETIME created{},exit{},kernel{},user{};
                if (!GetProcessTimes(GetCurrentProcess(),&created,&exit,&kernel,&user)) fail("无法读取更新进程状态。");
                const quint64 ticks=(quint64(created.dwHighDateTime)<<32|created.dwLowDateTime)+504911232000000000ULL;
                writeJson(plan,{{"schema",1},{"root",QDir(root_).absolutePath()},{"data",QDir(data_).absolutePath()},
                               {"job",job},{"pid",qint64(QCoreApplication::applicationPid())},
                               {"parent_start_ticks",QString::number(ticks)},{"sha256",sha},{"release",release.tag}});
                auto process=new QProcess(this);
                updatePrepare_=process;
                hiddenProcess(*process);
                process->setWorkingDirectory(job);
                process->setProgram(powershell());
                process->setArguments(installerArguments(plan,"Prepare"));
                process->setProcessChannelMode(QProcess::MergedChannels);
                status->setText("下载校验通过，正在解压并验证程序文件…");
                auto progress=dialog->findChild<QProgressBar *>("appUpdateProgress");
                progress->setRange(0,0);
                connect(process,&QProcess::readyReadStandardOutput,dialog,[process,status] {
                    const auto output=QString::fromUtf8(process->readAllStandardOutput());
                    if (output.contains("Extracting")) status->setText("正在解压完整包…");
                    if (output.contains("Verifying package")) status->setText("正在校验程序文件，完成后将重启软件…");
                });
                connect(process,&QProcess::errorOccurred,dialog,[this,process,status,install](auto error) {
                    if (error!=QProcess::FailedToStart) return;
                    status->setText("无法启动系统更新组件："+process->errorString());
                    install->setEnabled(true); updatePrepare_=nullptr; process->deleteLater();
                });
                connect(process,&QProcess::finished,dialog,[this,process,status,install,plan](int code,QProcess::ExitStatus exit) {
                    updatePrepare_=nullptr; process->deleteLater();
                    if (code || exit!=QProcess::NormalExit) {
                        status->setText("更新包准备失败，原程序未修改。请查看 "+QFileInfo(plan).absolutePath()+"/update.log");
                        install->setEnabled(true); return;
                    }
                    pendingUpdatePlan_=plan;
                    status->setText("校验完成，正在保存配置并退出，稍后将自动重新打开…");
                    requestExit();
                });
                process->start();
            } catch (const std::exception &e) { status->setText(errorText(e)); install->setEnabled(true); }
        });
        connect(updateClient_,&update::Client::failed,dialog,[this,connection](const QString &) { disconnect(*connection); });
        updateClient_->download(release,job);
    } catch (const std::exception &e) { failure(errorText(e)); }
}
bool Window::launchUpdateInstaller() {
    QProcess process;
    hiddenProcess(process);
    process.setProgram(powershell());
    process.setArguments(installerArguments(pendingUpdatePlan_,"Install"));
    process.setWorkingDirectory(QFileInfo(pendingUpdatePlan_).absolutePath());
    process.setStandardOutputFile(QFileInfo(pendingUpdatePlan_).absolutePath()+"/installer-output.log",QIODevice::Append);
    process.setStandardErrorFile(QFileInfo(pendingUpdatePlan_).absolutePath()+"/installer-output.log",QIODevice::Append);
    if (!process.startDetached()) {
        QMessageBox::warning(this,"更新未启动","无法启动更新组件，原程序未修改。请重试或从 GitHub 下载完整包。");
        pendingUpdatePlan_.clear();
        return false;
    }
    pendingUpdatePlan_.clear();
    return true;
}
} // namespace okww
