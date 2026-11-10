#pragma once
#include "team_macro.hpp"
#include <QLibrary>
namespace okww::runtime {
class WorkshopFileSession;
// Loaded modules remain alive until every combat session using their vtables
// has ended. A team is selected by its members, independent of party slot order.
class TeamStore {
    struct Module {
        std::shared_ptr<QLibrary> library;
        QJsonObject manifest;
        QString path;
        using Create = TeamMacro *(*)(CombatHost *);
        Create create = nullptr;
    };
    QString root_, data_, directory_, moduleDirectory_, deletedDirectory_;
    mutable std::recursive_mutex mutex_;
    QMap<QString, Module> modules_;
    mutable QByteArray validatedPythonSources_;
    mutable std::shared_ptr<WorkshopFileSession> fileSession_;
    static QStringList members(const QJsonValue &value);
    static QString key(const QStringList &team);
    QString folder(const QStringList &team) const;
    QJsonObject teamConfig(const QStringList &team) const;
    Module loadModule(const QString &path) const;
    void verifySources(const QStringList &team, const Module &module) const;
    void saveConfig(const QStringList &team, QJsonObject config);
    bool pythonTeam(const QStringList &team) const;
    QJsonObject pythonCommand(QJsonObject request) const;

  public:
    TeamStore(QString root, QString data);
    QJsonArray catalog() const;
    void validateEnabled() const;
    QString selection(CombatHost &host) const;
    std::unique_ptr<TeamMacro> create(const QString &selection, CombatHost &host) const;
    void setMode(const QJsonArray &team, const QString &mode);
    void setAllModes(const QString &mode);
    QString removeTeam(const QJsonArray &team);
    void createTeam(const QJsonArray &team);
    QJsonObject code(const QJsonArray &team, const QString &source) const;
    QJsonObject editor(const QJsonArray &team, const QString &member, const QString &source = {},
                       const QString &view = "active") const;
    void saveDraft(const QJsonArray &team, const QString &source, const QString &code);
    void exportTeam(const QJsonArray &team, const QString &destination) const;
    void importTeam(const QString &path);
};
} // namespace okww::runtime
