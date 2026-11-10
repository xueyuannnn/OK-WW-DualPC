#pragma once
#include <QtWidgets>
namespace okww::classic {
class CodeEditor final : public QPlainTextEdit {
    QWidget *gutter_;
    int gutterWidth() const;

  protected:
    void resizeEvent(QResizeEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;

  public:
    explicit CodeEditor(QWidget *parent = nullptr);
};
} // namespace okww::classic
