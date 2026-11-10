#include "classic_editor.hpp"
namespace okww::classic {
namespace {
class Highlighter final : public QSyntaxHighlighter {
  public:
    using QSyntaxHighlighter::QSyntaxHighlighter;
    void highlightBlock(const QString &text) override {
        const bool python = document()->property("codeLanguage").toString() == "python";
        const bool dark = qApp->palette().color(QPalette::Window).lightness() < 128;
        const auto apply = [&](const QRegularExpression &pattern, QColor color, bool bold = false) {
            QTextCharFormat format;
            format.setForeground(color);
            if (bold)
                format.setFontWeight(QFont::DemiBold);
            auto matches = pattern.globalMatch(text);
            while (matches.hasNext()) {
                auto match = matches.next();
                setFormat(match.capturedStart(), match.capturedLength(), format);
            }
        };
        apply(QRegularExpression(
                  "\\b(class|struct|namespace|using|public|private|protected|if|else|for|"
                  "while|return|void|bool|int|double|float|auto|const|constexpr|override|true|"
                  "false|nullptr|throw|try|catch|include|def|self|from|import|as|elif|in|is|not|and|or|None|"
                  "True|False|except|finally|raise|with|lambda|yield|pass|break|continue|super)\\b"),
              dark ? QColor("#75bfff") : QColor("#2789d8"), true);
        apply(QRegularExpression(R"("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')"),
              dark ? QColor("#e5b591") : QColor("#a45c22"));
        apply(QRegularExpression(python ? "#.*$" : "//.*$"), dark ? QColor("#8ac686") : QColor("#38933c"));
        setCurrentBlockState(0);
        int start = previousBlockState() == 1 ? 0 : text.indexOf("/*");
        while (start >= 0) {
            int end = text.indexOf("*/", start + 2);
            int length = end < 0 ? text.size() - start : end - start + 2;
            QTextCharFormat format;
            format.setForeground(dark ? QColor("#8ac686") : QColor("#38933c"));
            setFormat(start, length, format);
            if (end < 0) {
                setCurrentBlockState(1);
                break;
            }
            start = text.indexOf("/*", end + 2);
        }
    }
};
} // namespace
CodeEditor::CodeEditor(QWidget *parent) : QPlainTextEdit(parent), gutter_(new QWidget(this)) {
    setFont(QFont("Consolas", 10));
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setTabStopDistance(fontMetrics().horizontalAdvance(' ') * 4);
    gutter_->installEventFilter(this);
    const auto updateWidth = [this] {
        setViewportMargins(gutterWidth(), 0, 0, 0);
        gutter_->update();
    };
    connect(this, &QPlainTextEdit::blockCountChanged, this, updateWidth);
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect &rect, int dy) {
        if (dy)
            gutter_->scroll(0, dy);
        else
            gutter_->update(0, rect.y(), gutter_->width(), rect.height());
    });
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        QTextEdit::ExtraSelection selection;
        const bool dark = palette().color(QPalette::Base).lightness() < 128;
        selection.format.setBackground(dark ? QColor("#393627") : QColor("#fcf5d8"));
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.clearSelection();
        setExtraSelections({selection});
    });
    new Highlighter(document());
    updateWidth();
}
int CodeEditor::gutterWidth() const {
    return 12 + fontMetrics().horizontalAdvance('9') * QString::number(blockCount()).size();
}
void CodeEditor::resizeEvent(QResizeEvent *event) {
    QPlainTextEdit::resizeEvent(event);
    const auto r = contentsRect();
    gutter_->setGeometry(r.left() + 3, r.top() + 5, gutterWidth() - 3, r.height() - 10);
}
bool CodeEditor::eventFilter(QObject *object, QEvent *event) {
    if (object != gutter_ || event->type() != QEvent::Paint)
        return QPlainTextEdit::eventFilter(object, event);
    QPainter painter(gutter_);
    painter.fillRect(gutter_->rect(), palette().color(QPalette::Base));
    painter.setPen(QColor("#999999"));
    auto block = firstVisibleBlock();
    int number = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    while (block.isValid() && top < gutter_->height()) {
        if (block.isVisible())
            painter.drawText(0, top, gutter_->width() - 5, fontMetrics().height(), Qt::AlignRight,
                             QString::number(number + 1));
        top += qRound(blockBoundingRect(block).height());
        block = block.next();
        ++number;
    }
    return true;
}
} // namespace okww::classic
