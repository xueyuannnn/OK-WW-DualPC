#pragma once
#include <QtWidgets>
#include <functional>

namespace okww::classic {
QIcon icon(char16_t glyph, QColor color = QColor("#333333"));
QIcon fluentIcon(const QString &name, bool dark = false);
void setFluentIcon(QAbstractButton *button, const QString &name, bool primary = false);
void refreshFluentIcons(QWidget *root, bool dark);
void setActionIcon(QAbstractButton *button);
void setDisclosureButton(QPushButton *button);
QString stylesheet(bool dark, const QColor &accent = QColor("#009faa"));
void installChineseUi();
QVBoxLayout *dialogLayout(QDialog &dialog, const QString &title, const QString &description,
                         QSize preferred = {680, 540});
void polishForm(QFormLayout *form);
QPixmap characterPortrait(const QString &root, const QString &id);
QUrl workshopFeedbackUrl(const QJsonObject &entry);
void paintPortrait(QPainter &painter, const QRectF &rect, const QPixmap &image, const QString &fallback,
                   const QPalette &palette);
class ComboBox final : public QComboBox {
  public:
    explicit ComboBox(QWidget *parent = nullptr);
    void showPopup() override;
};
class DateTimeEditor final : public QWidget {
    ComboBox *year_, *month_, *day_;
    QTimeEdit *time_;
    void setDate(QDate value);
    void updateDays();

  public:
    explicit DateTimeEditor(const QDateTime &value, QWidget *parent = nullptr);
    QDateTime dateTime() const;
    QDate date() const;
    std::function<void()> onChanged;
};
// The spin box stays the source of truth, including its range and save signals.
QWidget *sliderEditor(QAbstractSpinBox *number, bool logarithmic = false);
class AspectImageLabel final : public QLabel {
  public:
    using QLabel::QLabel;

  protected:
    void paintEvent(QPaintEvent *) override;
};
class Switch final : public QAbstractButton {
  public:
    explicit Switch(QWidget *parent = nullptr);
    QSize sizeHint() const override { return {80, 30}; }

  protected:
    void paintEvent(QPaintEvent *) override;
};
class ExpandCard final : public QFrame {
    QWidget *header_, *body_;
    QLabel *title_, *description_;
    QLabel *icon_ = nullptr;
    QToolButton *chevron_;
    QVBoxLayout *textLayout_;
    bool expanded_ = false;

  protected:
    bool eventFilter(QObject *, QEvent *) override;

  public:
    QHBoxLayout *actions;
    std::function<void(bool)> onExpanded;
    ExpandCard(QString title, QString description, QWidget *parent = nullptr);
    QWidget *body() const { return body_; }
    void setExpanded(bool expanded);
    bool expanded() const { return expanded_; }
    void setText(QString title, QString description);
    void setCompactHeader();
    void setHeaderIcon(const QString &name);
};
class TitleBar final : public QWidget {
    QMainWindow *window_;
    QToolButton *maximize_;

  public:
    explicit TitleBar(QMainWindow *window, const QIcon &icon);

  protected:
    void mousePressEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
};
} // namespace okww::classic
