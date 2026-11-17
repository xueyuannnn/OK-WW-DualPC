#include "team_store.hpp"
#include "native_team_abi.hpp"
#include "workflow_abi.hpp"
#include "workshop.hpp"
#include <QCryptographicHash>
namespace okww::runtime {
namespace {
QByteArray bytes(const QString &path, qint64 limit = 64 * 1024 * 1024) {
    std::unique_ptr<QFile> file;
    QByteArray result;
    const std::array<double, 1> args{double(limit)};
    rust::workflowRun(rust::WorkflowMethod::TeamStoreBytes, args,
                      [&](rust::WorkflowOp op, const double *, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::TeamStoreCreateReadFile:
                              file = std::make_unique<QFile>(path);
                              break;
                          case rust::WorkflowOp::TeamStoreOpenReadFile:
                              out[0] = file->open(QIODevice::ReadOnly);
                              break;
                          case rust::WorkflowOp::TeamStoreReadFileSize:
                              out[0] = double(file->size());
                              break;
                          case rust::WorkflowOp::TeamStoreReadAllBytes:
                              result = file->readAll();
                              break;
                          case rust::WorkflowOp::TeamStoreFailure:
                              fail("无法读取队伍文件：" + QFileInfo(path).fileName());
                              break;
                          default:
                              fail("Rust 队伍文件读取宿主无效");
                          }
                      });
    return result;
}
void saveBytes(const QString &path, const QByteArray &value) {
    std::unique_ptr<QSaveFile> file;
    rust::workflowRun(rust::WorkflowMethod::TeamStoreSaveBytes, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::TeamStoreMakeFileDirectory:
                              out[0] = QDir().mkpath(QFileInfo(path).absolutePath());
                              break;
                          case rust::WorkflowOp::TeamStoreCreateSaveFile:
                              file = std::make_unique<QSaveFile>(path);
                              break;
                          case rust::WorkflowOp::TeamStoreOpenSaveFile:
                              out[0] = file->open(QIODevice::WriteOnly);
                              break;
                          case rust::WorkflowOp::TeamStoreWriteAllBytes:
                              out[0] = file->write(value) == value.size();
                              break;
                          case rust::WorkflowOp::TeamStoreCommitBytes:
                              out[0] = file->commit();
                              break;
                          case rust::WorkflowOp::TeamStoreFailure:
                              fail(int(a[0]) == 1 ? "无法创建队伍目录" : "队伍文件保存失败");
                              break;
                          default:
                              fail("Rust 队伍文件保存宿主无效");
                          }
                      });
}
QString digest(const QByteArray &data) {
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}
QString canonical(QString name) {
    rust::workflowRun(rust::WorkflowMethod::TeamStoreCanonical, {},
                      [&](rust::WorkflowOp op, const double *, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::TeamStoreTrimName:
                              name = name.trimmed();
                              break;
                          case rust::WorkflowOp::TeamStoreShorekeeperAlias:
                              out[0] = name.compare("Shorekeeper", Qt::CaseInsensitive) == 0;
                              break;
                          case rust::WorkflowOp::TeamStoreCanonicalShorekeeper:
                              name = "ShoreKeeper";
                              break;
                          case rust::WorkflowOp::TeamStoreLynaeAlias:
                              out[0] = name == "Lynae";
                              break;
                          case rust::WorkflowOp::TeamStoreCanonicalLinnai:
                              name = "Linnai";
                              break;
                          default:
                              fail("Rust 队伍角色名宿主无效");
                          }
                      });
    return name;
}
bool safeSource(const QString &name) {
    QStringList parts;
    return rust::workflowRun(rust::WorkflowMethod::TeamStoreSafeSource, {},
                             [&](rust::WorkflowOp op, const double *, double *out) {
                                 switch (op) {
                                 case rust::WorkflowOp::TeamStoreSplitPath:
                                     parts = name.split('/');
                                     break;
                                 case rust::WorkflowOp::TeamStoreSourceEmpty:
                                     out[0] = name.isEmpty();
                                     break;
                                 case rust::WorkflowOp::TeamStoreSourceAbsolute:
                                     out[0] = QDir::isAbsolutePath(name);
                                     break;
                                 case rust::WorkflowOp::TeamStoreSourceBackslash:
                                     out[0] = name.contains('\\');
                                     break;
                                 case rust::WorkflowOp::TeamStoreSourceColon:
                                     out[0] = name.contains(':');
                                     break;
                                 case rust::WorkflowOp::TeamStoreSourceParent:
                                     out[0] = parts.contains("..");
                                     break;
                                 case rust::WorkflowOp::TeamStoreSourceCurrent:
                                     out[0] = parts.contains(".");
                                     break;
                                 case rust::WorkflowOp::TeamStoreSourceEmptyPart:
                                     out[0] = parts.contains("");
                                     break;
                                 case rust::WorkflowOp::TeamStoreSourceExtension:
                                     out[0] =
                                         QStringList{"cpp", "hpp", "h"}.contains(QFileInfo(name).suffix());
                                     break;
                                 default:
                                     fail("Rust 队伍源码名宿主无效");
                                 }
                             })[0] != 0;
}
} // namespace
QStringList TeamStore::members(const QJsonValue &value) {
    QStringList result, split, classes;
    QString name;
    QSet<QString> unique;
    rust::workflowRun(rust::WorkflowMethod::TeamStoreMembers, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::TeamStoreMembersValueKind:
                              out[0] = value.isArray() ? 0 : value.isString() ? 1 : 2;
                              break;
                          case rust::WorkflowOp::TeamStoreMembersArrayCount:
                              out[0] = double(value.toArray().size());
                              break;
                          case rust::WorkflowOp::TeamStoreAppendArrayMember:
                              result.append(canonical(value.toArray().at(int(a[0])).toString()));
                              break;
                          case rust::WorkflowOp::TeamStoreSplitMembers:
                              split = value.toString().split(',');
                              break;
                          case rust::WorkflowOp::TeamStoreSplitMemberCount:
                              out[0] = double(split.size());
                              break;
                          case rust::WorkflowOp::TeamStoreAppendStringMember:
                              result.append(canonical(split.at(int(a[0]))));
                              break;
                          case rust::WorkflowOp::TeamStoreLoadClasses:
                              classes = nativeCharacterClasses();
                              break;
                          case rust::WorkflowOp::TeamStoreMemberCount:
                              out[0] = double(result.size());
                              break;
                          case rust::WorkflowOp::TeamStoreSelectMember:
                              name = result.at(int(a[0]));
                              break;
                          case rust::WorkflowOp::TeamStoreClassSupported:
                              out[0] = classes.contains(name);
                              break;
                          case rust::WorkflowOp::TeamStoreMemberBase:
                              out[0] = name == "BaseChar";
                              break;
                          case rust::WorkflowOp::TeamStoreMemberDuplicate:
                              out[0] = unique.contains(name);
                              break;
                          case rust::WorkflowOp::TeamStoreRememberMember:
                              unique.insert(name);
                              break;
                          case rust::WorkflowOp::TeamStoreSortMembers:
                              result.sort();
                              break;
                          case rust::WorkflowOp::TeamStoreFailure:
                              fail(int(a[0]) == 3 ? "队伍需要三名不同的已支持角色" : "队伍需要三名不同角色");
                              break;
                          default:
                              fail("Rust 队伍成员宿主无效");
                          }
                      });
    return result;
}
QString TeamStore::key(const QStringList &team) {
    auto sorted = team;
    QString result;
    rust::workflowRun(rust::WorkflowMethod::TeamStoreKey, {},
                      [&](rust::WorkflowOp op, const double *, double *) {
                          switch (op) {
                          case rust::WorkflowOp::TeamStoreSortMembers:
                              sorted.sort();
                              break;
                          case rust::WorkflowOp::TeamStoreJoinKey:
                              result = sorted.join("__");
                              break;
                          default:
                              fail("Rust 队伍标识宿主无效");
                          }
                      });
    return result;
}
QString TeamStore::folder(const QStringList &team) const { return directory_ + "/" + key(team); }
QJsonObject TeamStore::teamConfig(const QStringList &team) const {
    QString path;
    QJsonObject result;
    rust::workflowRun(rust::WorkflowMethod::TeamStoreConfig, {},
                      [&](rust::WorkflowOp op, const double *, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::TeamStoreComputeConfigPath:
                              path = folder(team) + "/team.json";
                              break;
                          case rust::WorkflowOp::TeamStoreConfigExists:
                              out[0] = QFile::exists(path);
                              break;
                          case rust::WorkflowOp::TeamStoreReadConfig:
                              result = readJson(path);
                              break;
                          default:
                              fail("Rust 队伍配置读取宿主无效");
                          }
                      });
    return result;
}
void TeamStore::saveConfig(const QStringList &team, QJsonObject config) {
    rust::workflowRun(rust::WorkflowMethod::TeamStoreSaveConfig, {},
                      [&](rust::WorkflowOp op, const double *, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::TeamStoreAssignConfigTeam:
                              config["team"] = team.join(", ");
                              break;
                          case rust::WorkflowOp::TeamStoreCreateConfigDirectory:
                              out[0] = QDir().mkpath(folder(team));
                              break;
                          case rust::WorkflowOp::TeamStoreWriteConfig:
                              writeJson(folder(team) + "/team.json", config);
                              break;
                          case rust::WorkflowOp::TeamStoreFailure:
                              fail("无法创建队伍配置目录");
                              break;
                          default:
                              fail("Rust 队伍配置保存宿主无效");
                          }
                      });
}
TeamStore::Module TeamStore::loadModule(const QString &path) const {
    std::shared_ptr<QLibrary> library;
    using RuntimeAbi = uint32_t (*)();
    using Read = const char *(*)();
    RuntimeAbi runtimeAbi = nullptr;
    Read read = nullptr;
    Module::Create create = nullptr;
    const char *text = nullptr;
    QJsonDocument document;
    QJsonObject manifest;
    Module result;
    rust::workflowRun(
        rust::WorkflowMethod::TeamStoreLoadModule, {},
        [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::TeamModuleCreateLibrary:
                library = std::shared_ptr<QLibrary>(new QLibrary(QFileInfo(path).absoluteFilePath()),
                                                    [](QLibrary *value) {
                                                        value->unload();
                                                        delete value;
                                                    });
                break;
            case rust::WorkflowOp::TeamModuleSetLoadHints:
                library->setLoadHints(QLibrary::ResolveAllSymbolsHint);
                break;
            case rust::WorkflowOp::TeamModuleLoadLibrary:
                out[0] = library->load();
                break;
            case rust::WorkflowOp::TeamModuleResolveRuntimeAbi:
                runtimeAbi = reinterpret_cast<RuntimeAbi>(library->resolve("okww_team_runtime_abi_v1"));
                break;
            case rust::WorkflowOp::TeamModuleRuntimeAbiPresent:
                out[0] = runtimeAbi != nullptr;
                break;
            case rust::WorkflowOp::TeamModuleReadRuntimeAbi:
                out[0] = runtimeAbi();
                break;
            case rust::WorkflowOp::TeamModuleResolveManifest:
                read = reinterpret_cast<Read>(library->resolve("okww_team_manifest_v1"));
                break;
            case rust::WorkflowOp::TeamModuleResolveFactory:
                create = reinterpret_cast<Module::Create>(library->resolve("okww_team_create_v1"));
                break;
            case rust::WorkflowOp::TeamModuleManifestReaderPresent:
                out[0] = read != nullptr;
                break;
            case rust::WorkflowOp::TeamModuleFactoryPresent:
                out[0] = create != nullptr;
                break;
            case rust::WorkflowOp::TeamModuleReadManifestText:
                text = read();
                break;
            case rust::WorkflowOp::TeamModuleManifestTextPresent:
                out[0] = text != nullptr;
                break;
            case rust::WorkflowOp::TeamModuleParseManifest:
                document = QJsonDocument::fromJson(QByteArray(text));
                break;
            case rust::WorkflowOp::TeamModuleManifestObject:
                out[0] = document.isObject();
                break;
            case rust::WorkflowOp::TeamModuleAssignManifest:
                manifest = document.object();
                break;
            case rust::WorkflowOp::TeamModuleFormatMatches:
                out[0] = manifest["format"] == "okww-native-team";
                break;
            case rust::WorkflowOp::TeamModuleManifestAbi:
                out[0] = manifest["abi"].toInt();
                break;
            case rust::WorkflowOp::TeamModuleOriginalSourceCount:
                out[0] = manifest["original_sources"].toObject().size();
                break;
            case rust::WorkflowOp::TeamModuleValidateMembers:
                members(manifest["team"]);
                break;
            case rust::WorkflowOp::TeamModuleIdentifierEmpty:
                out[0] = manifest["id"].toString().isEmpty();
                break;
            case rust::WorkflowOp::TeamModuleReturnLoaded:
                result = {library, manifest, QFileInfo(path).absoluteFilePath(), create};
                break;
            case rust::WorkflowOp::TeamModuleFailure: {
                switch (int(a[0])) {
                case 0:
                    fail("原生队伍模块加载失败：" + library->errorString());
                case 1:
                    fail("原生队伍模块需要使用当前版本重新编译；Python 工坊代码不受影响");
                case 2:
                    fail("队伍模块与当前 C++ 版本不兼容");
                case 3:
                    fail("队伍模块缺少清单");
                case 4:
                    fail("队伍模块清单损坏");
                case 5:
                    fail("队伍模块格式不受支持");
                case 6:
                    fail("队伍模块缺少标识");
                }
                break;
            }
            default:
                fail("Rust 队伍模块加载宿主无效");
            }
        });
    return result;
}
TeamStore::TeamStore(QString root, QString data)
    : root_(std::move(root)), data_(data), directory_(data + "/configs-dualpc/custom_teams"),
      moduleDirectory_(data + "/native-teams"), deletedDirectory_(data + "/deleted-teams") {
    QStringList directories, activeKeys;
    QString directory, path, registry, file, activeKey;
    QJsonObject active;
    Module module;
    rust::workflowRun(
        rust::WorkflowMethod::TeamStoreInitialize, {},
        [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::TeamModuleInitialDirectories:
                directories = {root_ + "/native-teams"};
                break;
            case rust::WorkflowOp::TeamModuleSourceProjectExists:
                out[0] = QFile::exists(root_ + "/native/CMakeLists.txt");
                break;
            case rust::WorkflowOp::TeamModuleAppendBuildDirectory:
                directories.append(root_ + "/build-native/Release");
                break;
            case rust::WorkflowOp::TeamModuleUniqueDirectories:
                directories.removeDuplicates();
                break;
            case rust::WorkflowOp::TeamModuleDirectoryCount:
                out[0] = double(directories.size());
                break;
            case rust::WorkflowOp::TeamModuleSelectDirectory:
                directory = directories.at(int(a[0]));
                break;
            case rust::WorkflowOp::TeamModuleBundledPath:
                path = directory + "/okww_team_" +
                       QStringList{"lucy", "cart", "denia", "aemeath", "hsin"}.at(int(a[0])) + ".dll";
                break;
            case rust::WorkflowOp::TeamModuleSelectedPathExists:
                out[0] = QFile::exists(path);
                break;
            case rust::WorkflowOp::TeamModuleLoadSelected:
                module = loadModule(path);
                break;
            case rust::WorkflowOp::TeamModuleActivateBundled:
                modules_[key(members(module.manifest["team"]))] = std::move(module);
                break;
            case rust::WorkflowOp::TeamModuleRegistryPath:
                registry = moduleDirectory_ + "/active.json";
                break;
            case rust::WorkflowOp::TeamModuleRegistryExists:
                out[0] = QFile::exists(registry);
                break;
            case rust::WorkflowOp::TeamModuleReadRegistry:
                active = readJson(registry);
                activeKeys = active.keys();
                break;
            case rust::WorkflowOp::TeamModuleRegistryCount:
                out[0] = double(activeKeys.size());
                break;
            case rust::WorkflowOp::TeamModuleSelectRegistryEntry:
                activeKey = activeKeys.at(int(a[0]));
                file = std::as_const(active)[activeKey].toString();
                break;
            case rust::WorkflowOp::TeamModuleRegistryFilenameValid:
                out[0] = QRegularExpression("^okww_team_[0-9a-f]{64}\\.dll$").match(file).hasMatch();
                break;
            case rust::WorkflowOp::TeamModuleRegisteredPath:
                path = moduleDirectory_ + '/' + file;
                break;
            case rust::WorkflowOp::TeamModuleRegisteredDigestMatches:
                out[0] = file == "okww_team_" + digest(bytes(path)) + ".dll";
                break;
            case rust::WorkflowOp::TeamModuleRegistryTeamMatches:
                out[0] = key(members(module.manifest["team"])) == activeKey;
                break;
            case rust::WorkflowOp::TeamModuleActivateRegistered:
                modules_[activeKey] = std::move(module);
                break;
            case rust::WorkflowOp::TeamModuleFailure:
                fail(QStringList{"原生队伍注册文件损坏", "原生队伍模块校验失败", "队伍注册记录与模块不一致"}
                         .at(int(a[0]) - 7));
                break;
            default:
                fail("Rust 队伍模块发现宿主无效");
            }
        });
}
bool TeamStore::pythonTeam(const QStringList &team) const {
    return rust::workflowRun(rust::WorkflowMethod::TeamStorePythonTeam, {},
                             [&](rust::WorkflowOp op, const double *a, double *out) {
                                 switch (op) {
                                 case rust::WorkflowOp::TeamModuleMemberCount:
                                     out[0] = double(team.size());
                                     break;
                                 case rust::WorkflowOp::TeamModulePythonMemberExists:
                                     out[0] = QFile::exists(folder(team) + '/' + team.at(int(a[0])) +
                                                            ".import.py");
                                     break;
                                 default:
                                     fail("Rust Python 队伍检测宿主无效");
                                 }
                             })[0] != 0;
}
QJsonObject TeamStore::pythonCommand(QJsonObject request) const {
    std::lock_guard lock(mutex_);
    QJsonObject result;
    rust::workflowRun(rust::WorkflowMethod::TeamStorePythonCommand, {},
                      [&](rust::WorkflowOp op, const double *, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::TeamModuleFileSessionPresent:
                              out[0] = bool(fileSession_);
                              break;
                          case rust::WorkflowOp::TeamModuleCreateFileSession:
                              fileSession_ = std::make_shared<WorkshopFileSession>(root_, data_);
                              break;
                          case rust::WorkflowOp::TeamModuleRunPythonCommand:
                              result = fileSession_->command(std::move(request));
                              break;
                          default:
                              fail("Rust Python 队伍文件通道宿主无效");
                          }
                      });
    return result;
}
void TeamStore::verifySources(const QStringList &team, const Module &module) const {
    QJsonObject originals, saved;
    QStringList files;
    QString file, path, receipt;
    QJsonValue expected;
    rust::workflowRun(rust::WorkflowMethod::TeamStoreVerifySources, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::TeamModuleLoadOriginalSources:
                              originals = module.manifest["original_sources"].toObject();
                              files = originals.keys();
                              break;
                          case rust::WorkflowOp::TeamModuleSourceCount:
                              out[0] = double(files.size());
                              break;
                          case rust::WorkflowOp::TeamModuleSelectOriginalSource:
                              file = files.at(int(a[0]));
                              expected = std::as_const(originals)[file];
                              break;
                          case rust::WorkflowOp::TeamModuleOriginalSourceFilename:
                              out[0] = QFileInfo(file).fileName() == file;
                              break;
                          case rust::WorkflowOp::TeamModuleOriginalSourcePath:
                              path = folder(team) + '/' + file;
                              break;
                          case rust::WorkflowOp::TeamModuleSelectedPathExists:
                              out[0] = QFile::exists(path);
                              break;
                          case rust::WorkflowOp::TeamModuleOriginalDigestMatches:
                              out[0] = digest(bytes(path, 8 * 1024 * 1024)) == expected.toString();
                              break;
                          case rust::WorkflowOp::TeamModuleReceiptPath:
                              receipt = folder(team) + "/native-source.json";
                              break;
                          case rust::WorkflowOp::TeamModuleReceiptExists:
                              out[0] = QFile::exists(receipt);
                              break;
                          case rust::WorkflowOp::TeamModuleReadReceipt:
                              saved = readJson(receipt);
                              break;
                          case rust::WorkflowOp::TeamModuleReceiptIdMatches:
                              out[0] = std::as_const(saved)["id"] == module.manifest["id"];
                              break;
                          case rust::WorkflowOp::TeamModuleReceiptSourcesMatch:
                              out[0] = std::as_const(saved)["original_sources"] ==
                                       module.manifest["original_sources"];
                              break;
                          case rust::WorkflowOp::TeamModuleFailure:
                              switch (int(a[0])) {
                              case 10:
                                  fail("队伍源码清单路径无效");
                              case 11:
                                  fail("个人队伍源文件已修改，需要重新迁移后使用：" + key(team) + '/' + file);
                              case 12:
                                  fail("队伍原始源码不完整，不能静默使用旧连招");
                              case 13:
                                  fail("此队伍还没有匹配的原生迁移记录，请导入原生队伍包");
                              case 14:
                                  fail("原生队伍与迁移记录不匹配");
                              }
                              break;
                          default:
                              fail("Rust 队伍源码校验宿主无效");
                          }
                      });
}

TeamStore::CatalogResult TeamStore::runCatalog(int method, QString id, CombatHost *host) const {
    Module module;
    CatalogResult result;
    QFileInfoList entries;
    QJsonObject config, item;
    QString path, directory, python;
    QStringList team, paths;
    QJsonArray labels, sources, items, pythonTeams;
    std::unique_ptr<QCryptographicHash> hash;
    std::unique_ptr<QDirIterator> files;
    QByteArray fingerprint;
    QVector<Character *> party;
    std::optional<std::unique_lock<std::recursive_mutex>> lock;
    rust::workflowRun(
        rust::WorkflowMethod(method), {},
        [&](rust::WorkflowOp op, const double *a, double *out, const std::exception_ptr &error) {
            switch (op) {
            case rust::WorkflowOp::TeamCatalogScanDirectories:
                entries = QDir(directory_).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
                break;
            case rust::WorkflowOp::TeamCatalogDirectoryCount:
                out[0] = double(entries.size());
                break;
            case rust::WorkflowOp::TeamCatalogSelectDirectory:
                path = entries.at(int(a[0])).absoluteFilePath() + "/team.json";
                break;
            case rust::WorkflowOp::TeamCatalogConfigExists:
                out[0] = QFile::exists(path);
                break;
            case rust::WorkflowOp::TeamCatalogReadConfig:
                config = readJson(path);
                break;
            case rust::WorkflowOp::TeamCatalogReadMembers:
                team = members(config["team"]);
                break;
            case rust::WorkflowOp::TeamCatalogComputeKey:
                id = key(team);
                break;
            case rust::WorkflowOp::TeamCatalogClearLabels:
                labels = {};
                sources = {};
                break;
            case rust::WorkflowOp::TeamCatalogMemberCount:
                out[0] = double(team.size());
                break;
            case rust::WorkflowOp::TeamCatalogAppendLabel:
                labels.append(team.at(int(a[0])));
                break;
            case rust::WorkflowOp::TeamCatalogCreateItem:
                item = {{"team", QJsonArray::fromStringList(team)},
                        {"name", config["name"].toString(team.join(" / "))},
                        {"labels", labels},
                        {"mode", config["code_mode"].toString("builtin")},
                        {"native", true}};
                break;
            case rust::WorkflowOp::TeamCatalogIsPython:
                out[0] = pythonTeam(team);
                break;
            case rust::WorkflowOp::TeamCatalogMarkPython:
                item["python"] = true;
                break;
            case rust::WorkflowOp::TeamCatalogAppendPythonSource:
                sources.append(team.at(int(a[0])) + ".py");
                break;
            case rust::WorkflowOp::TeamCatalogAssignPythonSources:
                item["source_files"] = sources;
                break;
            case rust::WorkflowOp::TeamCatalogFindPython:
                python = workshopPython(root_);
                break;
            case rust::WorkflowOp::TeamCatalogPythonMissing:
                out[0] = python.isEmpty();
                break;
            case rust::WorkflowOp::TeamCatalogSetAvailable:
                item["available"] = a[0] != 0;
                break;
            case rust::WorkflowOp::TeamCatalogPythonMissingError:
                item["error"] = "缺少工坊 Python 兼容引擎，请使用完整包";
                break;
            case rust::WorkflowOp::TeamCatalogHasModule:
                out[0] = modules_.contains(id);
                break;
            case rust::WorkflowOp::TeamCatalogAssignModuleSources:
                item["source_files"] =
                    QJsonArray::fromStringList(modules_[id].manifest["sources"].toObject().keys());
                break;
            case rust::WorkflowOp::TeamCatalogVerifyModule:
                verifySources(team, modules_[id]);
                break;
            case rust::WorkflowOp::TeamCatalogSetVerificationError:
                try {
                    std::rethrow_exception(error);
                } catch (const std::exception &e) {
                    item["error"] = errorText(e);
                }
                break;
            case rust::WorkflowOp::TeamCatalogModuleMissingError:
                item["error"] = "尚未导入此队伍的原生模块";
                break;
            case rust::WorkflowOp::TeamCatalogAppendItem:
                result.array.append(item);
                break;
            case rust::WorkflowOp::TeamCatalogLoadItems:
                items = catalog();
                break;
            case rust::WorkflowOp::TeamCatalogCreateHash:
                hash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
                break;
            case rust::WorkflowOp::TeamCatalogItemCount:
                out[0] = double(items.size());
                break;
            case rust::WorkflowOp::TeamCatalogSelectItem:
                item = items.at(int(a[0])).toObject();
                break;
            case rust::WorkflowOp::TeamCatalogItemImportMode:
                out[0] = std::as_const(item)["mode"] == "import";
                break;
            case rust::WorkflowOp::TeamCatalogItemAvailable:
                out[0] = std::as_const(item)["available"].toBool();
                break;
            case rust::WorkflowOp::TeamCatalogItemPython:
                out[0] = std::as_const(item)["python"].toBool();
                break;
            case rust::WorkflowOp::TeamCatalogAppendValidationTeam:
                pythonTeams.append(std::as_const(item)["team"]);
                break;
            case rust::WorkflowOp::TeamCatalogComputeValidationDirectory:
                directory = folder(members(std::as_const(item)["team"]));
                break;
            case rust::WorkflowOp::TeamCatalogHashMemberKey:
                hash->addData(key(members(std::as_const(item)["team"])).toUtf8());
                break;
            case rust::WorkflowOp::TeamCatalogHashConfig:
                hash->addData(bytes(directory + "/team.json"));
                break;
            case rust::WorkflowOp::TeamCatalogCreatePythonIterator:
                files = std::make_unique<QDirIterator>(directory, QStringList{"*.py"}, QDir::Files,
                                                       QDirIterator::Subdirectories);
                paths.clear();
                break;
            case rust::WorkflowOp::TeamCatalogPythonIteratorHasNext:
                out[0] = files->hasNext();
                break;
            case rust::WorkflowOp::TeamCatalogAppendPythonPath:
                paths.append(files->next());
                break;
            case rust::WorkflowOp::TeamCatalogSortPythonPaths:
                paths.sort();
                break;
            case rust::WorkflowOp::TeamCatalogPythonPathCount:
                out[0] = double(paths.size());
                break;
            case rust::WorkflowOp::TeamCatalogSelectPythonPath:
                path = paths.at(int(a[0]));
                break;
            case rust::WorkflowOp::TeamCatalogHashRelativePath:
                hash->addData(QDir(directory).relativeFilePath(path).toUtf8());
                break;
            case rust::WorkflowOp::TeamCatalogHashPythonBytes:
                hash->addData(bytes(path, 8 * 1024 * 1024));
                break;
            case rust::WorkflowOp::TeamCatalogDropPythonIterator:
                paths.clear();
                files.reset();
                break;
            case rust::WorkflowOp::TeamCatalogFinishFingerprint:
                fingerprint = hash->result();
                break;
            case rust::WorkflowOp::TeamCatalogLock:
                lock.emplace(mutex_);
                break;
            case rust::WorkflowOp::TeamCatalogUnlock:
                lock.reset();
                break;
            case rust::WorkflowOp::TeamCatalogFingerprintMatches:
                out[0] = validatedPythonSources_ == fingerprint;
                break;
            case rust::WorkflowOp::TeamCatalogValidationTeamCount:
                out[0] = double(pythonTeams.size());
                break;
            case rust::WorkflowOp::TeamCatalogValidatePython:
                pythonCommand({{"op", "validate"}, {"teams", pythonTeams}});
                break;
            case rust::WorkflowOp::TeamCatalogCommitFingerprint:
                validatedPythonSources_ = fingerprint;
                break;
            case rust::WorkflowOp::TeamCatalogLoadParty:
                party = host->characters();
                break;
            case rust::WorkflowOp::TeamCatalogPartyCount:
                out[0] = double(party.size());
                break;
            case rust::WorkflowOp::TeamCatalogPartyMemberPresent:
                out[0] = party.at(int(a[0])) != nullptr;
                break;
            case rust::WorkflowOp::TeamCatalogAppendPartyMember:
                team.append(canonical(party.at(int(a[0]))->state.className));
                break;
            case rust::WorkflowOp::TeamCatalogContainsBaseChar:
                out[0] = team.contains("BaseChar");
                break;
            case rust::WorkflowOp::TeamCatalogLoadSelectedConfig:
                config = teamConfig(team);
                break;
            case rust::WorkflowOp::TeamCatalogConfigImportMode:
                out[0] = std::as_const(config)["code_mode"] == "import";
                break;
            case rust::WorkflowOp::TeamCatalogReturnPythonKey:
                result.string = "python:" + id;
                break;
            case rust::WorkflowOp::TeamCatalogReturnNativeKey:
                result.string = id;
                break;
            case rust::WorkflowOp::TeamCatalogSelectionEmpty:
                out[0] = id.isEmpty();
                break;
            case rust::WorkflowOp::TeamCatalogSelectionPython:
                out[0] = id.startsWith("python:");
                break;
            case rust::WorkflowOp::TeamCatalogCreatePythonMacro:
                result.macro = std::make_unique<PythonTeamMacro>(*host, root_, data_);
                break;
            case rust::WorkflowOp::TeamCatalogConfigureMacro:
                result.macro->configure();
                break;
            case rust::WorkflowOp::TeamCatalogCopyModule:
                module = modules_[id];
                break;
            case rust::WorkflowOp::TeamCatalogCreateNativeMacro:
                result.macro.reset(module.create(host));
                break;
            case rust::WorkflowOp::TeamCatalogMacroPresent:
                out[0] = bool(result.macro);
                break;
            case rust::WorkflowOp::TeamCatalogFailure:
                switch (int(a[0])) {
                case 0:
                    fail(std::as_const(item)["name"].toString() + "：" +
                         std::as_const(item)["error"].toString());
                case 1:
                    fail("当前队伍缺少原生模块：" + id);
                case 2:
                    fail("原生队伍模块不存在");
                case 3:
                    fail("原生队伍创建失败");
                }
                break;
            default:
                fail("Rust 队伍目录与选择宿主无效");
            }
        });
    return result;
}
QJsonArray TeamStore::catalog() const {
    std::lock_guard lock(mutex_);
    return runCatalog(int(rust::WorkflowMethod::TeamStoreCatalog)).array;
}
void TeamStore::validateEnabled() const { runCatalog(int(rust::WorkflowMethod::TeamStoreValidateEnabled)); }
QString TeamStore::selection(CombatHost &host) const {
    return runCatalog(int(rust::WorkflowMethod::TeamStoreSelection), {}, &host).string;
}
std::unique_ptr<TeamMacro> TeamStore::create(const QString &id, CombatHost &host) const {
    return runCatalog(int(rust::WorkflowMethod::TeamStoreCreate), id, &host).macro;
}

namespace {
[[noreturn]] void teamMutationFailure(int code) {
    static const QStringList messages{
        "无效队伍模式",
        "队伍不存在",
        "请先导入此队伍的原生模块",
        "队伍路径越界",
        "无法将队伍移入恢复目录，原配置保留",
        "队伍源文件不存在",
        "角色不属于当前队伍",
        "队伍源码过大",
        "内置队伍没有可导出的原生模块",
        "请选择工坊队伍 ZIP（.zip）或原生队伍包（.okww-team）。Python 工坊代码可直接导入，无需转为 C++。",
        "原生队伍包校验失败",
        "原生队伍模块文件冲突",
        "原生队伍包清单与模块不一致"};
    fail(messages.at(code));
}
} // namespace
TeamStore::MutationResult TeamStore::runMutation(int method, const QJsonArray &value, QString parameter,
                                                 const QString &text) {
    MutationResult result;
    QStringList team, originals;
    QString id, member, removal, destination, target, receipt, registry;
    QJsonObject config, item, package, manifest, active;
    QJsonArray items;
    QVector<QPair<QStringList, QJsonObject>> before;
    QByteArray raw, binary;
    Module module;
    rust::workflowRun(
        rust::WorkflowMethod(method), {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::TeamMutationFailure:
                teamMutationFailure(int(a[0]));
                break;
            case rust::WorkflowOp::TeamMutationModeImport:
                out[0] = parameter == "import";
                break;
            case rust::WorkflowOp::TeamMutationModeBuiltin:
                out[0] = parameter == "builtin";
                break;
            case rust::WorkflowOp::TeamMutationReadMembers:
                team = members(value);
                break;
            case rust::WorkflowOp::TeamMutationLoadConfig:
                config = teamConfig(team);
                break;
            case rust::WorkflowOp::TeamMutationConfigEmpty:
                out[0] = config.isEmpty();
                break;
            case rust::WorkflowOp::TeamMutationPythonTeam:
                out[0] = pythonTeam(team);
                break;
            case rust::WorkflowOp::TeamMutationPythonMode:
                pythonCommand(
                    {{"op", "mode"}, {"team", QJsonArray::fromStringList(team)}, {"mode", parameter}});
                break;
            case rust::WorkflowOp::TeamMutationModuleForMembers:
                out[0] = modules_.contains(key(team));
                break;
            case rust::WorkflowOp::TeamMutationVerifyMembers:
                verifySources(team, modules_[key(team)]);
                break;
            case rust::WorkflowOp::TeamMutationAssignMode:
                config["code_mode"] = parameter;
                break;
            case rust::WorkflowOp::TeamMutationSaveConfig:
                saveConfig(team, config);
                break;
            case rust::WorkflowOp::TeamMutationDefaultConfig:
                config = {{"name", team.join(" / ")}, {"code_mode", "builtin"}};
                break;
            case rust::WorkflowOp::TeamMutationReadCatalog:
                items = catalog();
                break;
            case rust::WorkflowOp::TeamMutationCatalogCount:
                out[0] = double(items.size());
                break;
            case rust::WorkflowOp::TeamMutationCatalogItemPython:
                out[0] = items.at(int(a[0])).toObject()["python"].toBool();
                break;
            case rust::WorkflowOp::TeamMutationPythonAllModes:
                pythonCommand({{"op", "all_mode"}, {"mode", parameter}});
                break;
            case rust::WorkflowOp::TeamMutationSelectCatalogItem:
                item = items.at(int(a[0])).toObject();
                break;
            case rust::WorkflowOp::TeamMutationReadItemMembers:
                team = members(std::as_const(item)["team"]);
                break;
            case rust::WorkflowOp::TeamMutationAppendBeforeConfig:
                before.append({team, teamConfig(team)});
                break;
            case rust::WorkflowOp::TeamMutationBeforeCount:
                out[0] = double(before.size());
                break;
            case rust::WorkflowOp::TeamMutationSelectBeforeConfig:
                team = before.at(int(a[0])).first;
                config = before.at(int(a[0])).second;
                break;
            case rust::WorkflowOp::TeamMutationRestoreBeforeConfig:
                saveConfig(before.at(int(a[0])).first, before.at(int(a[0])).second);
                break;
            case rust::WorkflowOp::TeamMutationRemovalSource:
                removal = QDir::cleanPath(folder(team));
                break;
            case rust::WorkflowOp::TeamMutationRemovalDestination:
                destination = QDir::cleanPath(deletedDirectory_ + '/' + key(team) + '-' +
                                              QUuid::createUuid().toString(QUuid::Id128));
                break;
            case rust::WorkflowOp::TeamMutationRemovalSourceWithinRoot:
                out[0] = removal.startsWith(QDir::cleanPath(directory_) + '/', Qt::CaseInsensitive);
                break;
            case rust::WorkflowOp::TeamMutationRemovalDestinationWithinRoot:
                out[0] =
                    destination.startsWith(QDir::cleanPath(deletedDirectory_) + '/', Qt::CaseInsensitive);
                break;
            case rust::WorkflowOp::TeamMutationCreateDeletedDirectory:
                out[0] = QDir().mkpath(deletedDirectory_);
                break;
            case rust::WorkflowOp::TeamMutationMoveToDeleted:
                out[0] = QDir().rename(removal, destination);
                break;
            case rust::WorkflowOp::TeamMutationReturnRemovalDestination:
                result.string = destination;
                break;
            case rust::WorkflowOp::TeamMutationComputeKey:
                id = key(team);
                break;
            case rust::WorkflowOp::TeamMutationPythonSourceSuffix:
                out[0] = parameter.endsWith(".py");
                break;
            case rust::WorkflowOp::TeamMutationReadPythonMember:
                member = parameter.left(parameter.size() - 3);
                break;
            case rust::WorkflowOp::TeamMutationPythonMemberInTeam:
                out[0] = team.contains(member);
                break;
            case rust::WorkflowOp::TeamMutationPythonSave:
                pythonCommand({{"op", "save"}, {"team", value}, {"member", member}, {"code", text}});
                break;
            case rust::WorkflowOp::TeamMutationHasModule:
                out[0] = modules_.contains(id);
                break;
            case rust::WorkflowOp::TeamMutationModuleContainsSource:
                out[0] = modules_[id].manifest["sources"].toObject().contains(parameter);
                break;
            case rust::WorkflowOp::TeamMutationSourceSafe:
                out[0] = safeSource(parameter);
                break;
            case rust::WorkflowOp::TeamMutationUtf8Size:
                out[0] = double(text.toUtf8().size());
                break;
            case rust::WorkflowOp::TeamMutationSaveDraftBytes:
                saveBytes(folder(team) + "/drafts/" + parameter, text.toUtf8());
                break;
            case rust::WorkflowOp::TeamMutationZipPath:
                out[0] = parameter.endsWith(".zip", Qt::CaseInsensitive);
                break;
            case rust::WorkflowOp::TeamMutationPythonImport:
                pythonCommand({{"op", "import"}, {"path", parameter}});
                break;
            case rust::WorkflowOp::TeamMutationReadPackageBytes:
                raw = bytes(parameter, 96 * 1024 * 1024);
                break;
            case rust::WorkflowOp::TeamMutationParsePackage:
                package = QJsonDocument::fromJson(raw).object();
                break;
            case rust::WorkflowOp::TeamMutationPackageFormat:
                out[0] = std::as_const(package)["format"] == "okww-native-team-package";
                break;
            case rust::WorkflowOp::TeamMutationPackageAbi:
                out[0] = std::as_const(package)["abi"].toInt();
                break;
            case rust::WorkflowOp::TeamMutationReadPackageManifest:
                manifest = std::as_const(package)["manifest"].toObject();
                break;
            case rust::WorkflowOp::TeamMutationReadManifestMembers:
                team = members(std::as_const(manifest)["team"]);
                break;
            case rust::WorkflowOp::TeamMutationDecodeBinary:
                binary = QByteArray::fromBase64(std::as_const(package)["module"].toString().toLatin1(),
                                                QByteArray::AbortOnBase64DecodingErrors);
                break;
            case rust::WorkflowOp::TeamMutationBinaryEmpty:
                out[0] = binary.isEmpty();
                break;
            case rust::WorkflowOp::TeamMutationBinarySize:
                out[0] = double(binary.size());
                break;
            case rust::WorkflowOp::TeamMutationBinaryDigestMatches:
                out[0] = digest(binary) == std::as_const(package)["sha256"].toString();
                break;
            case rust::WorkflowOp::TeamMutationTargetPath:
                target = moduleDirectory_ + "/okww_team_" + digest(binary) + ".dll";
                break;
            case rust::WorkflowOp::TeamMutationTargetExists:
                out[0] = QFile::exists(target);
                break;
            case rust::WorkflowOp::TeamMutationSaveBinary:
                saveBytes(target, binary);
                break;
            case rust::WorkflowOp::TeamMutationTargetBytesMatch:
                out[0] = bytes(target) == binary;
                break;
            case rust::WorkflowOp::TeamMutationLoadTargetModule:
                module = loadModule(target);
                break;
            case rust::WorkflowOp::TeamMutationManifestMatches:
                out[0] = module.manifest == manifest;
                break;
            case rust::WorkflowOp::TeamMutationReceiptPath:
                receipt = folder(team) + "/native-source.json";
                break;
            case rust::WorkflowOp::TeamMutationOriginalFiles:
                originals = module.manifest["original_sources"].toObject().keys();
                break;
            case rust::WorkflowOp::TeamMutationOriginalFileCount:
                out[0] = double(originals.size());
                break;
            case rust::WorkflowOp::TeamMutationOriginalFileExists:
                out[0] = QFile::exists(folder(team) + '/' + originals.at(int(a[0])));
                break;
            case rust::WorkflowOp::TeamMutationVerifyModule:
                verifySources(team, module);
                break;
            case rust::WorkflowOp::TeamMutationMetadataConfig:
                config = {{"name", std::as_const(manifest)["name"]},
                          {"author", std::as_const(manifest)["author"]},
                          {"description", std::as_const(manifest)["description"]}};
                break;
            case rust::WorkflowOp::TeamMutationWriteReceipt:
                writeJson(receipt, {{"id", std::as_const(manifest)["id"]},
                                    {"original_sources", std::as_const(manifest)["original_sources"]}});
                break;
            case rust::WorkflowOp::TeamMutationRegistryPath:
                registry = moduleDirectory_ + "/active.json";
                break;
            case rust::WorkflowOp::TeamMutationRegistryExists:
                out[0] = QFile::exists(registry);
                break;
            case rust::WorkflowOp::TeamMutationReadRegistry:
                active = readJson(registry);
                break;
            case rust::WorkflowOp::TeamMutationRegisterTarget:
                active[key(team)] = QFileInfo(target).fileName();
                break;
            case rust::WorkflowOp::TeamMutationWriteRegistry:
                writeJson(registry, active);
                break;
            case rust::WorkflowOp::TeamMutationActivateModule:
                modules_[key(team)] = std::move(module);
                break;
            case rust::WorkflowOp::TeamMutationAssignImportMode:
                config["code_mode"] = "import";
                break;
            default:
                fail("Rust 队伍配置更改宿主无效");
            }
        });
    return result;
}
void TeamStore::setMode(const QJsonArray &value, const QString &mode) {
    std::lock_guard lock(mutex_);
    runMutation(int(rust::WorkflowMethod::TeamStoreSetMode), value, mode);
}
void TeamStore::createTeam(const QJsonArray &value) {
    std::lock_guard lock(mutex_);
    runMutation(int(rust::WorkflowMethod::TeamStoreCreateTeam), value);
}
void TeamStore::setAllModes(const QString &mode) {
    std::lock_guard lock(mutex_);
    runMutation(int(rust::WorkflowMethod::TeamStoreSetAllModes), {}, mode);
}
QString TeamStore::removeTeam(const QJsonArray &value) {
    std::lock_guard lock(mutex_);
    return runMutation(int(rust::WorkflowMethod::TeamStoreRemoveTeam), value).string;
}
void TeamStore::saveDraft(const QJsonArray &value, const QString &source, const QString &text) {
    std::lock_guard lock(mutex_);
    runMutation(int(rust::WorkflowMethod::TeamStoreSaveDraft), value, source, text);
}
void TeamStore::importTeam(const QString &path) {
    std::lock_guard lock(mutex_);
    runMutation(int(rust::WorkflowMethod::TeamStoreImportTeam), {}, path);
}
QJsonObject TeamStore::code(const QJsonArray &value, const QString &source) const {
    std::lock_guard lock(mutex_);
    QStringList team;
    QString id, draft, text;
    bool isDraft = false;
    QJsonObject sources, result;
    rust::workflowRun(
        rust::WorkflowMethod::TeamStoreCode, {}, [&](rust::WorkflowOp op, const double *a, double *out) {
            switch (op) {
            case rust::WorkflowOp::TeamMutationReadMembers:
                team = members(value);
                break;
            case rust::WorkflowOp::TeamMutationComputeKey:
                id = key(team);
                break;
            case rust::WorkflowOp::TeamMutationHasModule:
                out[0] = modules_.contains(id);
                break;
            case rust::WorkflowOp::TeamMutationBuiltinCodeMessage:
                result = {{"code", "此队伍使用内置 C++ 连招，尚无导入的原生源码。"}, {"read_only", true}};
                break;
            case rust::WorkflowOp::TeamMutationReadModuleSources:
                sources = modules_[id].manifest["sources"].toObject();
                break;
            case rust::WorkflowOp::TeamMutationSourceExists:
                out[0] = sources.contains(source);
                break;
            case rust::WorkflowOp::TeamMutationSourceSafe:
                out[0] = safeSource(source);
                break;
            case rust::WorkflowOp::TeamMutationDraftPath:
                draft = folder(team) + "/drafts/" + source;
                break;
            case rust::WorkflowOp::TeamMutationDraftExists:
                out[0] = QFile::exists(draft);
                break;
            case rust::WorkflowOp::TeamMutationReadDraft:
                text = QString::fromUtf8(bytes(draft, 8 * 1024 * 1024));
                break;
            case rust::WorkflowOp::TeamMutationReadBundledSource:
                text = std::as_const(sources)[source].toString();
                break;
            case rust::WorkflowOp::TeamMutationReadDraftFlag:
                isDraft = QFile::exists(draft);
                break;
            case rust::WorkflowOp::TeamMutationReturnCode:
                result = {{"code", text}, {"read_only", false}, {"draft", isDraft}, {"language", "cpp"}};
                break;
            case rust::WorkflowOp::TeamMutationFailure:
                teamMutationFailure(int(a[0]));
                break;
            default:
                fail("Rust 队伍代码读取宿主无效");
            }
        });
    return result;
}
void TeamStore::exportTeam(const QJsonArray &value, const QString &destination) const {
    std::lock_guard lock(mutex_);
    QStringList team;
    QString id;
    Module module;
    QByteArray binary;
    rust::workflowRun(rust::WorkflowMethod::TeamStoreExportTeam, {},
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::TeamMutationReadMembers:
                              team = members(value);
                              break;
                          case rust::WorkflowOp::TeamMutationComputeKey:
                              id = key(team);
                              break;
                          case rust::WorkflowOp::TeamMutationPythonTeam:
                              out[0] = pythonTeam(team);
                              break;
                          case rust::WorkflowOp::TeamMutationPythonExport:
                              pythonCommand({{"op", "export"}, {"team", value}, {"path", destination}});
                              break;
                          case rust::WorkflowOp::TeamMutationHasModule:
                              out[0] = modules_.contains(id);
                              break;
                          case rust::WorkflowOp::TeamMutationCopyModule:
                              module = modules_[id];
                              break;
                          case rust::WorkflowOp::TeamMutationVerifyModule:
                              verifySources(team, module);
                              break;
                          case rust::WorkflowOp::TeamMutationReadBinary:
                              binary = bytes(module.path);
                              break;
                          case rust::WorkflowOp::TeamMutationWriteExportPackage:
                              writeJson(destination, {{"format", "okww-native-team-package"},
                                                      {"abi", 1},
                                                      {"sha256", digest(binary)},
                                                      {"module", QString::fromLatin1(binary.toBase64())},
                                                      {"manifest", module.manifest}});
                              break;
                          case rust::WorkflowOp::TeamMutationFailure:
                              teamMutationFailure(int(a[0]));
                              break;
                          default:
                              fail("Rust 队伍包导出宿主无效");
                          }
                      });
}
} // namespace okww::runtime
