#pragma once
#include "runtime/workflow_abi.hpp"
namespace okww::runtime::editor_test {
QString definitionExcerpt(const QString &text, int start) {
    QRegularExpression tokens;
    QRegularExpressionMatchIterator matches;
    QRegularExpressionMatch match;
    int end = 0;
    QString result;
    const std::array<double, 1> args{double(start)};
    rust::workflowRun(
        rust::WorkflowMethod::TeamEditorDefinition, args,
        [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::TeamEditorCreateTokenIterator:
                tokens = QRegularExpression(
                    R"code(//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|[{}])code");
                matches = tokens.globalMatch(text, start);
                break;
            case rust::WorkflowOp::TeamEditorTokenHasNext:
                out[0] = matches.hasNext();
                break;
            case rust::WorkflowOp::TeamEditorSelectToken:
                match = matches.next();
                break;
            case rust::WorkflowOp::TeamEditorTokenOpening:
                out[0] = match.captured() == "{";
                break;
            case rust::WorkflowOp::TeamEditorTokenClosing:
                out[0] = match.captured() == "}";
                break;
            case rust::WorkflowOp::TeamEditorDefinitionEnd:
                end = match.capturedEnd();
                break;
            case rust::WorkflowOp::TeamEditorEndBeforeText:
                out[0] = end < text.size();
                break;
            case rust::WorkflowOp::TeamEditorEndSemicolon:
                out[0] = text[end] == ';';
                break;
            case rust::WorkflowOp::TeamEditorIncludeSemicolon:
                ++end;
                break;
            case rust::WorkflowOp::TeamEditorReturnDefinition:
                result = text.mid(start, end - start);
                break;
            case rust::WorkflowOp::TeamEditorFailure:
                fail(int(a[0]) == 1 ? "内置源码中缺少对应角色定义" : "内置源码定义不完整");
                break;
            default:
                fail("Rust 内置源码摘录宿主无效");
            }
        });
    return result;
}

} // namespace okww::runtime::editor_test
