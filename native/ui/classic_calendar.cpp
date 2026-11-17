#include "classic_ui.hpp"
#include <algorithm>
namespace okww::classic {
DateTimeEditor::DateTimeEditor(const QDateTime &value, QWidget *parent) : QWidget(parent) {
    const auto local = value.isValid() ? value.toLocalTime() : QDateTime::currentDateTime();
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    auto dateRow = new QHBoxLayout;
    dateRow->setSpacing(8);
    year_ = new ComboBox;
    month_ = new ComboBox;
    day_ = new ComboBox;
    year_->setObjectName("scheduleYear");
    month_->setObjectName("scheduleMonth");
    day_->setObjectName("scheduleDay");
    year_->setAccessibleName("执行年份");
    month_->setAccessibleName("执行月份");
    day_->setAccessibleName("执行日期");
    for (int year = std::min(1970, local.date().year()); year <= std::max(2100, local.date().year()); ++year)
        year_->addItem(QString::number(year) + " 年", year);
    for (int month = 1; month <= 12; ++month)
        month_->addItem(QString::number(month) + " 月", month);
    year_->setMinimumWidth(116);
    month_->setMinimumWidth(90);
    day_->setMinimumWidth(90);
    dateRow->addWidget(year_, 4);
    dateRow->addWidget(month_, 3);
    dateRow->addWidget(day_, 3);
    layout->addLayout(dateRow);
    auto timeRow = new QHBoxLayout;
    timeRow->setSpacing(10);
    time_ = new QTimeEdit(QTime(local.time().hour(), local.time().minute()));
    time_->setObjectName("scheduleTime");
    time_->setAccessibleName("执行时间，24小时制，可直接输入小时和分钟");
    time_->setDisplayFormat("HH:mm");
    time_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    time_->setKeyboardTracking(false);
    time_->setAlignment(Qt::AlignCenter);
    time_->setFixedWidth(120);
    timeRow->addWidget(time_);
    auto hint = new QLabel("24 小时制");
    hint->setObjectName("caption");
    timeRow->addWidget(hint);
    timeRow->addStretch();
    auto today = new QPushButton("今天"), tomorrow = new QPushButton("明天");
    today->setObjectName("scheduleToday");
    tomorrow->setObjectName("scheduleTomorrow");
    for (auto b : {today, tomorrow}) {
        b->setCheckable(true);
        b->setProperty("chip", true);
        b->setAutoDefault(false);
        timeRow->addWidget(b);
    }
    layout->addLayout(timeRow);
    setDate(local.date());
    const auto refresh = [this, today, tomorrow] {
        today->setChecked(date() == QDate::currentDate());
        tomorrow->setChecked(date() == QDate::currentDate().addDays(1));
        if (onChanged) onChanged();
    };
    for (auto selector : {year_, month_})
        QObject::connect(selector, &QComboBox::currentIndexChanged, this, [this, refresh] {
            updateDays();
            refresh();
        });
    QObject::connect(day_, &QComboBox::currentIndexChanged, this, refresh);
    QObject::connect(time_, &QTimeEdit::timeChanged, this, refresh);
    QObject::connect(today, &QPushButton::clicked, this, [this, refresh] { setDate(QDate::currentDate()); refresh(); });
    QObject::connect(tomorrow, &QPushButton::clicked, this, [this, refresh] { setDate(QDate::currentDate().addDays(1)); refresh(); });
    refresh();
}
void DateTimeEditor::updateDays() {
    const int wanted = std::max(1, day_->currentData().toInt());
    const int count = QDate(year_->currentData().toInt(), month_->currentData().toInt(), 1).daysInMonth();
    const QSignalBlocker blocker(day_);
    day_->clear();
    for (int day = 1; day <= count; ++day)
        day_->addItem(QString::number(day) + " 日", day);
    day_->setCurrentIndex(std::min(wanted, count) - 1);
}
void DateTimeEditor::setDate(QDate value) {
    const QSignalBlocker y(year_), m(month_), d(day_);
    year_->setCurrentIndex(year_->findData(value.year()));
    month_->setCurrentIndex(month_->findData(value.month()));
    updateDays();
    day_->setCurrentIndex(day_->findData(value.day()));
}
QDateTime DateTimeEditor::dateTime() const { return QDateTime(date(), time_->time()); }
QDate DateTimeEditor::date() const {
    return QDate(year_->currentData().toInt(), month_->currentData().toInt(), day_->currentData().toInt());
}
} // namespace okww::classic