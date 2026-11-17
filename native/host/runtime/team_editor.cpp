#include "team_store.hpp"
#include "workflow_abi.hpp"

// Referencing the resource initializer also retains its object when linking
// okww_runtime as a static library. These are the actual compiled source units.
static void initBuiltinCodeResources() {
    static const bool initialized = [] {
        Q_INIT_RESOURCE(builtin_code);
        return true;
    }();
    Q_UNUSED(initialized);
}

namespace okww::runtime {
namespace {
[[noreturn]] void editorFailure(int code, const QString &detail = {}) {
    switch (code) {
    case 0:
        fail("内置角色源码资源缺失：" + detail);
    case 3:
        fail("未找到当前角色对应的内置源码：" + detail);
    default:
        break;
    }
    static const QStringList messages{"",
                                      "内置源码中缺少对应角色定义",
                                      "内置源码定义不完整",
                                      "",
                                      "该文件不属于当前角色的内置代码",
                                      "导入模块缺少可查看的 C++ 实现文件",
                                      "所选角色不属于当前队伍",
                                      "队伍不存在",
                                      "代码查看模式无效",
                                      "所选文件不属于当前角色",
                                      "无法读取工坊角色代码",
                                      "此队伍尚未导入原生模块"};
    fail(messages.at(code));
}
QString builtinText(const QString &file) {
    initBuiltinCodeResources();
    std::unique_ptr<QFile> input;
    QString result;
    rust::workflowRun(rust::WorkflowMethod::TeamEditorBuiltinText, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::TeamEditorCreateResourceFile:
                              input = std::make_unique<QFile>(":/builtin-code/" + file);
                              break;
                          case rust::WorkflowOp::TeamEditorOpenResource:
                              out[0] = input->open(QIODevice::ReadOnly);
                              break;
                          case rust::WorkflowOp::TeamEditorReadResource:
                              result = QString::fromUtf8(input->readAll());
                              break;
                          case rust::WorkflowOp::TeamEditorFailure:
                              editorFailure(int(a[0]), file);
                              break;
                          default:
                              fail("Rust 内置源码读取宿主无效");
                          }
                      });
    return result;
}
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
                editorFailure(int(a[0]));
                break;
            default:
                fail("Rust 内置源码摘录宿主无效");
            }
        });
    return result;
}
QJsonObject builtinEditor(const QString &member, const QString &requested) {
    initBuiltinCodeResources();
    QRegularExpression declaration;
    QStringList available, files, rustFiles;
    QString primary, selected, source, file, text, label;
    QJsonArray choices;
    int offset = -1;
    QJsonObject result;
    rust::workflowRun(
        rust::WorkflowMethod::TeamEditorBuiltin, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::TeamEditorCreateDeclaration:
                declaration = QRegularExpression("\\bclass\\s+" + QRegularExpression::escape(member) + "\\b");
                break;
            case rust::WorkflowOp::TeamEditorListBuiltinFiles:
                available = QDir(":/builtin-code").entryList({"characters_*.cpp"}, QDir::Files);
                break;
            case rust::WorkflowOp::TeamEditorBuiltinFileCount:
                out[0] = double(available.size());
                break;
            case rust::WorkflowOp::TeamEditorSelectBuiltinFile:
                selected = available.at(int(a[0]));
                break;
            case rust::WorkflowOp::TeamEditorDeclarationMatches:
                out[0] = declaration.match(builtinText(selected)).hasMatch();
                break;
            case rust::WorkflowOp::TeamEditorChoosePrimary:
                primary = selected;
                break;
            case rust::WorkflowOp::TeamEditorCommonCharacter:
                out[0] = QStringList{"Chixia", "Youhu"}.contains(member);
                break;
            case rust::WorkflowOp::TeamEditorChooseCommonPrimary:
                primary = "character.cpp";
                break;
            case rust::WorkflowOp::TeamEditorPrimaryEmpty:
                out[0] = primary.isEmpty();
                break;
            case rust::WorkflowOp::TeamEditorInitialChoices:
                files = {primary, "full:" + primary};
                break;
            case rust::WorkflowOp::TeamEditorCommonChoicePresent:
                out[0] = files.contains(QStringList{"character.cpp", "character.hpp"}.at(int(a[0])));
                break;
            case rust::WorkflowOp::TeamEditorAppendCommonChoice:
                files.append(QStringList{"character.cpp", "character.hpp"}.at(int(a[0])));
                break;
            case rust::WorkflowOp::TeamEditorLoadRustChoices: {
                static const QHash<QString, QString> modules{
                    {"characters_basic.cpp", "rust/rotation/basic.rs"},
                    {"characters_extended.cpp", "rust/rotation/extended.rs"},
                    {"characters_stateful.cpp", "rust/rotation/stateful.rs"},
                    {"characters_advanced.cpp", "rust/rotation/advanced.rs"},
                    {"characters_gauges.cpp", "rust/rotation/gauges.rs"},
                    {"characters_forms.cpp", "rust/rotation/forms.rs"},
                    {"characters_spectro.cpp", "rust/rotation/spectro.rs"},
                    {"characters_additional.cpp", "rust/rotation/additional.rs"},
                    {"character.cpp", "rust/rotation.rs"}};
                rustFiles = {modules.value(primary), "rust/combat.rs"};
                break;
            }
            case rust::WorkflowOp::TeamEditorRustChoiceCount:
                out[0] = double(rustFiles.size());
                break;
            case rust::WorkflowOp::TeamEditorAppendRustChoice:
                files.append(rustFiles.at(int(a[0])));
                break;
            case rust::WorkflowOp::TeamEditorRequestedEmpty:
                out[0] = requested.isEmpty();
                break;
            case rust::WorkflowOp::TeamEditorUsePrimary:
                source = primary;
                break;
            case rust::WorkflowOp::TeamEditorUseRequested:
                source = requested;
                break;
            case rust::WorkflowOp::TeamEditorSourceInChoices:
                out[0] = files.contains(source);
                break;
            case rust::WorkflowOp::TeamEditorSourceFull:
                out[0] = source.startsWith("full:");
                break;
            case rust::WorkflowOp::TeamEditorStripFullPrefix:
                file = source.mid(5);
                break;
            case rust::WorkflowOp::TeamEditorUseSourceFile:
                file = source;
                break;
            case rust::WorkflowOp::TeamEditorLoadSelectedText:
                text = builtinText(file);
                break;
            case rust::WorkflowOp::TeamEditorSourcePrimary:
                out[0] = source == primary;
                break;
            case rust::WorkflowOp::TeamEditorDeclarationOffset:
                offset = declaration.match(text).capturedStart();
                break;
            case rust::WorkflowOp::TeamEditorCommonOffset:
                offset = text.indexOf("void Character::doPerform(");
                break;
            case rust::WorkflowOp::TeamEditorWrapExcerpt:
                text = "// " + primary + " · " + member + '\n' + definitionExcerpt(text, offset) + '\n';
                break;
            case rust::WorkflowOp::TeamEditorResetOffset:
                offset = 0;
                break;
            case rust::WorkflowOp::TeamEditorChoiceCount:
                out[0] = double(files.size());
                break;
            case rust::WorkflowOp::TeamEditorSelectChoice:
                file = files.at(int(a[0]));
                break;
            case rust::WorkflowOp::TeamEditorChoiceFull:
                out[0] = file.startsWith("full:");
                break;
            case rust::WorkflowOp::TeamEditorChoicePrimary:
                out[0] = file == primary;
                break;
            case rust::WorkflowOp::TeamEditorLabelFullChoice:
                label = "完整文件 · " + file.mid(5);
                break;
            case rust::WorkflowOp::TeamEditorLabelPrimaryChoice:
                label = "当前角色实现 · " + file;
                break;
            case rust::WorkflowOp::TeamEditorLabelPlainChoice:
                label = file;
                break;
            case rust::WorkflowOp::TeamEditorAppendChoice:
                choices.append(QJsonObject{{"id", file}, {"label", label}});
                break;
            case rust::WorkflowOp::TeamEditorOffsetNegative:
                out[0] = offset < 0;
                break;
            case rust::WorkflowOp::TeamEditorFocusLine:
                out[0] = double(text.left(offset).count('\n'));
                break;
            case rust::WorkflowOp::TeamEditorReturnBuiltin:
                result = {
                    {"code", text},
                    {"read_only", true},
                    {"source_file", source},
                    {"file_choices", choices},
                    {"code_kind", "builtin"},
                    {"focus_line", int(a[1])},
                    {"message",
                     QStringList{"下面显示所选角色的内置 C++ 定义；完整源码和通用逻辑可在代码文件中查看。",
                                 "该角色沿用通用内置连招，下面显示其实际 C++ 实现。"}
                         .at(int(a[0]))}};
                break;
            case rust::WorkflowOp::TeamEditorFailure:
                editorFailure(int(a[0]), member);
                break;
            default:
                fail("Rust 内置角色源码宿主无效");
            }
        });
    return result;
}
QString primaryImportedSource(const QJsonObject &sources) {
    QStringList keys;
    QString result;
    rust::workflowRun(rust::WorkflowMethod::TeamEditorPrimaryImported, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::TeamEditorHsinCorePresent:
                              out[0] = sources.contains("hsin_core.cpp");
                              break;
                          case rust::WorkflowOp::TeamEditorReturnHsinCore:
                              result = "hsin_core.cpp";
                              break;
                          case rust::WorkflowOp::TeamEditorLoadSourceKeys:
                              keys = sources.keys();
                              break;
                          case rust::WorkflowOp::TeamEditorSourceKeyCount:
                              out[0] = double(keys.size());
                              break;
                          case rust::WorkflowOp::TeamEditorSourceKeyCpp:
                              out[0] = keys.at(int(a[0])).endsWith(".cpp");
                              break;
                          case rust::WorkflowOp::TeamEditorReturnSourceKey:
                              result = keys.at(int(a[0]));
                              break;
                          case rust::WorkflowOp::TeamEditorFailure:
                              editorFailure(int(a[0]));
                              break;
                          default:
                              fail("Rust 导入源码入口宿主无效");
                          }
                      });
    return result;
}
} // namespace
QJsonObject TeamStore::runEditor(const QJsonArray &value, const QString &member, const QString &requested,
                                 const QString &view) const {
    QStringList team, keys;
    QString mode, id, source, primary;
    QJsonObject config, sources, result;
    QJsonArray choices;
    std::unique_ptr<QFile> input;
    rust::workflowRun(
        rust::WorkflowMethod::TeamStoreEditor, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::TeamEditorReadMembers:
                team = members(value);
                break;
            case rust::WorkflowOp::TeamEditorMemberInTeam:
                out[0] = team.contains(member);
                break;
            case rust::WorkflowOp::TeamEditorLoadTeamConfig:
                config = teamConfig(team);
                break;
            case rust::WorkflowOp::TeamEditorTeamConfigEmpty:
                out[0] = config.isEmpty();
                break;
            case rust::WorkflowOp::TeamEditorViewActive:
                out[0] = view == "active";
                break;
            case rust::WorkflowOp::TeamEditorViewImport:
                out[0] = view == "import";
                break;
            case rust::WorkflowOp::TeamEditorReadMode:
                mode = std::as_const(config)["code_mode"].toString("builtin");
                break;
            case rust::WorkflowOp::TeamEditorPythonTeam:
                out[0] = pythonTeam(team);
                break;
            case rust::WorkflowOp::TeamEditorModeImport:
                out[0] = mode == "import";
                break;
            case rust::WorkflowOp::TeamEditorPythonSourceName:
                source = member + ".py";
                break;
            case rust::WorkflowOp::TeamEditorRequestedEmpty:
                out[0] = requested.isEmpty();
                break;
            case rust::WorkflowOp::TeamEditorRequestedMatchesPython:
                out[0] = requested == source;
                break;
            case rust::WorkflowOp::TeamEditorCreatePythonFile:
                input = std::make_unique<QFile>(folder(team) + '/' + member + ".import.py");
                break;
            case rust::WorkflowOp::TeamEditorOpenPythonFile:
                out[0] = input->open(QIODevice::ReadOnly);
                break;
            case rust::WorkflowOp::TeamEditorPythonFileSize:
                out[0] = double(input->size());
                break;
            case rust::WorkflowOp::TeamEditorReadPythonCode:
                result = {
                    {"code", QString::fromUtf8(input->readAll())},
                    {"read_only", false},
                    {"source_file", source},
                    {"file_choices",
                     QJsonArray{QJsonObject{{"id", source}, {"label", source + " · Python 工坊代码"}}}},
                    {"code_kind", "python"},
                    {"language", "python"},
                    {"focus_line", 1},
                    {"message", "运行原始 Python 工坊代码；保存后通过语法与加载校验，下次战斗段生效。"}};
                break;
            case rust::WorkflowOp::TeamEditorDropPythonFile:
                input.reset();
                break;
            case rust::WorkflowOp::TeamEditorTeamKey:
                id = key(team);
                break;
            case rust::WorkflowOp::TeamEditorModulePresent:
                out[0] = modules_.contains(id);
                break;
            case rust::WorkflowOp::TeamEditorLoadModuleSources:
                sources = modules_[id].manifest["sources"].toObject();
                break;
            case rust::WorkflowOp::TeamEditorUsePrimaryImported:
                source = primaryImportedSource(sources);
                break;
            case rust::WorkflowOp::TeamEditorUseRequested:
                source = requested;
                break;
            case rust::WorkflowOp::TeamEditorReadModuleCode:
                result = code(value, source);
                break;
            case rust::WorkflowOp::TeamEditorComputePrimaryImported:
                primary = primaryImportedSource(sources);
                break;
            case rust::WorkflowOp::TeamEditorAppendPrimaryImportedChoice:
                choices.append(QJsonObject{{"id", primary}, {"label", primary + " · 队伍连招"}});
                break;
            case rust::WorkflowOp::TeamEditorLoadSourceKeys:
                keys = sources.keys();
                break;
            case rust::WorkflowOp::TeamEditorSourceKeyCount:
                out[0] = double(keys.size());
                break;
            case rust::WorkflowOp::TeamEditorSourceKeyPrimary:
                out[0] = keys.at(int(a[0])) == primary;
                break;
            case rust::WorkflowOp::TeamEditorAppendImportedChoice: {
                const auto file = keys.at(int(a[0]));
                choices.append(QJsonObject{{"id", file}, {"label", file}});
                break;
            }
            case rust::WorkflowOp::TeamEditorAssignImportedMetadata:
                result["source_file"] = source;
                result["file_choices"] = choices;
                result["code_kind"] = "import";
                result["shared_team_code"] = true;
                result["focus_line"] = 1;
                result["message"] = "三名角色由同一队伍模块统一编排；代码文件单独选择，不代表角色。";
                break;
            case rust::WorkflowOp::TeamEditorReadBuiltinCode:
                result = builtinEditor(member, requested);
                break;
            case rust::WorkflowOp::TeamEditorAssignCharacter:
                result["character"] = member;
                break;
            case rust::WorkflowOp::TeamEditorAssignActiveMode:
                result["active_mode"] = mode;
                break;
            case rust::WorkflowOp::TeamEditorAssignViewOnly:
                result["view_only"] = a[0] != 0;
                break;
            case rust::WorkflowOp::TeamEditorSetReadOnly:
                result["read_only"] = true;
                break;
            case rust::WorkflowOp::TeamEditorFailure:
                editorFailure(int(a[0]));
                break;
            default:
                fail("Rust 角色代码查看宿主无效");
            }
        });
    return result;
}
QJsonObject TeamStore::editor(const QJsonArray &value, const QString &member, const QString &source,
                              const QString &view) const {
    std::lock_guard lock(mutex_);
    return runEditor(value, member, source, view);
}
} // namespace okww::runtime
