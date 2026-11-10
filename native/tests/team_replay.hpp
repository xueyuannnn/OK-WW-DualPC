#pragma once
#include "runtime/team_macro.hpp"
#include <opencv2/imgproc.hpp>
namespace okww::runtime::test {
class TeamReplay : public CombatHost {
    QJsonObject config_;
    cv::Mat frame_;
    cv::Mat readyFrame_;
    bool heavyTriggered_ = false;
    double time_ = 100, animation_ = -1e12, heldAt_ = -1, qingHeldAt_ = -1;
    std::array<std::array<double, 3>, 3> readyAt_{};
    std::array<std::unique_ptr<Character>, 3> party_;
    int index_ = 0;

  public:
    QJsonArray events;
    QSet<QString> held;
    explicit TeamReplay(QJsonObject config) : config_(config) {
        if (config.contains("image"))
            frame_ = readImage(config["image"].toString());
        if (config.contains("ready_image"))
            readyFrame_ = readImage(config["ready_image"].toString());
        const QStringList names = config["names"].toArray().size() == 3
                                      ? QStringList{config["names"].toArray()[0].toString(),
                                                    config["names"].toArray()[1].toString(),
                                                    config["names"].toArray()[2].toString()}
                                  : config["team"] == "hsin"    ? QStringList{"Hsin", "Rover", "Suisui"}
                                  : config["team"] == "aemeath" ? QStringList{"Aemeath", "Linnai", "Mornye"}
                                  : config["team"] == "cart"  ? QStringList{"Cartethyia", "Ciaccona", "Rover"}
                                  : config["team"] == "denia" ? QStringList{"Denia", "Mornye", "Qingxiao"}
                                                              : QStringList{"Lucy", "Rebecca", "ShoreKeeper"};
        index_ = config["entry"].toInt();
        useLiberation = config["use_liberation"].toBool(true);
        for (int i = 0; i < 3; ++i) {
            CharacterState s;
            s.index = i;
            s.name = s.className = names[i];
            s.current = index_ == i;
            s.role = i == 2 ? Role::Healer : Role::MainDps;
            s.hasIntro = config["intro"].toBool() && index_ == i;
            party_[i] = std::make_unique<Character>(*this, s);
            party_[i]->introMotion = i == 2 ? 1.6 : .9;
            for (int j = 0; j < 3; ++j)
                readyAt_[i][j] = config["ready"].toBool(true) ? 0 : 1e9;
        }
    }
    Character &entry() { return *party_[index_]; }
    void event(QString kind, QString value) {
        events.append(QJsonObject{{"kind", kind}, {"value", value}, {"time", time_}});
    }
    double now() const override { return time_; }
    void sleep(double seconds) override {
        time_ += std::max(0., seconds);
        if (config_.contains("cancel_at") && time_ >= config_["cancel_at"].toDouble())
            throw Cancelled();
        if (time_ > 1200)
            throw CombatEnded("team replay exceeded time budget");
    }
    void preciseWait(double seconds) override { sleep(seconds); }
    void nextFrame() override { sleep(.02); }
    void refreshObservation() override {}
    uint64_t observationId() const override { return uint64_t(time_ * 100000); }
    cv::Mat frameImage() override {
        if (!readyFrame_.empty() && readyAt_[0][0] > 100 && time_ >= readyAt_[0][0] + .5)
            return readyFrame_;
        return frame_.empty() ? cv::Mat::zeros(1080, 1920, CV_8UC3) : frame_;
    }
    void checkCombat() override {}
    bool inTeam() override {
        if (config_["heavy_animation"].toBool() && index_ == 0 && held.contains("left") && !heavyTriggered_ &&
            time_ - heldAt_ >= .25) {
            heavyTriggered_ = true;
            animation_ = time_ - .03;
        }
        return !config_["animation"].toBool(true) || time_ - animation_ < .03 || time_ - animation_ >= .55;
    }
    int partyIndex() override { return index_; }
    int partyCount() override { return config_["partial_hud"].toBool() ? 2 : 3; }
    bool openWorldAutoCombat() const override { return false; }
    bool available(Skill skill, int index = -1, bool = true, bool = true) override {
        return skill != Skill::ExtraAction && time_ >= readyAt_[index < 0 ? index_ : index][int(skill)];
    }
    bool hasCd(Skill s, int i = -1) override { return !available(s, i); }
    double cooldown(Skill s, int i = -1) override { return available(s, i) ? 0 : 2; }
    double highlighted(Skill s) override { return available(s) ? .1 : 0; }
    double white(const QString &) override { return 0; }
    double levitatorWhite() override { return .5; }
    bool forte(Forte) override { return config_["forte"].toBool() && (heldAt_ < 0 || time_ - heldAt_ < .12); }
    double concerto(Character &) override { return config_["con"].toDouble(1); }
    void tap(QString k, double after = 0, double = -1, double down = .01) override {
        rawTap(k, down);
        sleep(after);
    }
    void rawTap(const QString &k, double hold) override {
        event("key", k);
        if (k.size() == 1 && k >= "1" && k <= "3") {
            if (!config_["switch_fail"].toBool() && inTeam())
                index_ = k.toInt() - 1;
        } else
            for (int i = 0; i < 3; ++i)
                if (k == hotkey(Skill(i))) {
                    if (available(Skill(i))) {
                        if (i == 2)
                            animation_ = time_;
                        readyAt_[index_][i] = time_ + config_["cooldown"].toDouble(2);
                    }
                }
        sleep(hold);
    }
    QString hotkey(Skill s) const override { return QStringList{"e", "r", "q", "4"}[int(s)]; }
    QString toolKey() const override { return "t"; }
    QString jumpKey() const override { return "space"; }
    void click(QString button = "left", double = -1) override { rawClick(button, .01); }
    void rawClick(const QString &button, double hold) override {
        event("click", button);
        sleep(std::max(.02, hold));
    }
    void clickRelative(double x, double y) override {
        event("coordinate", QString("%1,%2").arg(x).arg(y));
        sleep(.26);
    }
    void scroll(int steps) override {
        event("scroll", QString::number(steps));
        sleep(.06);
    }
    void mouseDown(QString button = "left") override {
        event("down", button);
        held.insert(button);
        if (button == "left")
            heldAt_ = time_;
        if (button == "left")
            heavyTriggered_ = false;
        if (button == "left" && index_ == 2 && config_["team"] == "denia")
            qingHeldAt_ = time_;
    }
    void mouseUp(QString button = "left") override {
        event("up", button);
        held.remove(button);
    }
    void keyDown(QString k) override {
        event("key_down", k);
        held.insert(k);
    }
    void keyUp(QString k) override {
        event("key_up", k);
        held.remove(k);
    }
    void switchNext(Character &previous, bool = false, bool = false, const SwitchAction & = {}) override {
        event("switch", "");
        const int target = config_.contains("switch_target") ? config_["switch_target"].toInt()
                           : config_["team"] == "denia"      ? (events.size() < 30 && useLiberation ? 2 : 1)
                                                             : (useLiberation && events.size() < 30 ? 0 : 2);
        previous.switchOut(false);
        index_ = target;
        party_[target]->state.current = true;
    }
    QVector<Character *> characters() override { return {party_[0].get(), party_[1].get(), party_[2].get()}; }
    bool breakF(bool = false) override { return false; }
    bool checkBreak() override { return false; }
    bool feature(const QString &name, double = .8, const QString & = {}) override {
        if (name == "suisui_e1")
            return config_["forte"].toBool() && available(Skill::Resonance);
        if (name == "suisui_forte3" || name == "hsin_h1" || name == "hsin_lib1")
            return config_["forte"].toBool();
        if (name == "hsin_lib2")
            return config_["heavy_animation"].toBool();
        if (name == "has_target")
            return config_["airborne"].toBool();
        if (name == "qingxiao_e")
            return config_["qing_e"].toBool() && available(Skill::Resonance);
        if (name == "aemeath_e1")
            return config_["aem_e"].toBool() && available(Skill::Resonance);
        return name == config_["qing_heavy"].toString() &&
               (config_["heavy_interrupted"].toBool() || qingHeldAt_ < 0 || time_ - qingHeldAt_ < .12);
    }
    QString bestInBox(const QString &, const QStringList &, double = .6) override { return {}; }
    double whiteRatio(QSize, QRect, cv::Scalar = {250, 246, 244}, bool = true) override {
        return forte(Forte::Bar) ? .2 : 0;
    }
    QJsonValue characterSetting(const QString &, QJsonValue fallback = {}) override { return fallback; }
    int segmentedGauge(QSize, QRect, ColorRange, int, int, int, double) override { return 0; }
    QString targetName() const override { return "has_target"; }
    QString taskId() const override { return "AutoCombatTask"; }
    cv::Mat imageRegion(QSize reference, QRect area, bool hc = true) override {
        if (!frame_.empty()) {
            const auto box = Templates::anchored(area, reference, {frame_.cols, frame_.rows}, hc)
                                 .intersected({0, 0, frame_.cols, frame_.rows});
            if (box.isEmpty())
                return {};
            return frame_(cv::Rect(box.x(), box.y(), box.width(), box.height()));
        }
        return cv::Mat::zeros(area.height(), area.width(), CV_8UC3);
    }
    cv::Mat namedImage(const QString &) override {
        cv::Mat hsv(10, 10, CV_8UC3,
                    config_["gold"].toBool() ? cv::Scalar(20, 180, 180) : cv::Scalar(0, 0, 0)),
            bgr;
        cv::cvtColor(hsv, bgr, cv::COLOR_HSV2BGR);
        return bgr;
    }
    bool partialFeature(const QString &, double, double, double) override { return false; }
    bool featureInRegion(const QString &, QSize, QRect, double) override { return false; }
    bool targetLocked() override { return true; }
    void ensureElement(Character &) override {}
    bool isAutoCombatTask() const override { return true; }
    void log(const QString &) override {}
};
} // namespace okww::runtime::test
