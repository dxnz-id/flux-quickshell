#include "FluidSimShaders.h"
#include <QFile>
#include <QCoreApplication>
#include <QDir>
#include <QProcessEnvironment>

static QStringList qmlImportPaths()
{
    QStringList paths;
    QString env = QProcessEnvironment::systemEnvironment().value("QML2_IMPORT_PATH");
    for (const QString &p : env.split(':', Qt::SkipEmptyParts))
        paths << p;
    return paths;
}

QShader FluidSimShaders::loadShader(const QString &name)
{
    QString path = shaderPath(name);
    QFile f(path);
    if (f.open(QIODevice::ReadOnly)) {
        QShader s = QShader::fromSerialized(f.readAll());
        if (s.isValid())
            return s;
    }
    // Fallback: search relative to plugin dir
    QDir dir(QCoreApplication::applicationDirPath());
    QString rel = dir.relativeFilePath(path);
    QFile f2(rel);
    if (f2.open(QIODevice::ReadOnly)) {
        QShader s = QShader::fromSerialized(f2.readAll());
        if (s.isValid())
            return s;
    }
    return QShader();
}

QString FluidSimShaders::shaderPath(const QString &name)
{
    // Search paths ordered by likelihood
    QStringList searchPaths = {
        // Relative to app dir (qml6 tool)
        QCoreApplication::applicationDirPath() + "/shaders/",
        QCoreApplication::applicationDirPath() + "/../shaders/",
        QCoreApplication::applicationDirPath() + "/qml/FluidSim/shaders/",
        // Relative to current dir
        "shaders/",
        "FluidSim/shaders/",
        // Relative to QML2_IMPORT_PATH / plugin deployment dir
        QDir::homePath() + "/.local/lib/qml/FluidSim/shaders/",
        // Resource path (for embedded Qt resources)
        ":/shaders/",
    };
    // Add paths from QML2_IMPORT_PATH env var
    for (const QString &ip : qmlImportPaths())
        searchPaths.prepend(ip + "/FluidSim/shaders/");
    for (const QString &base : searchPaths) {
        QString path = base + name + ".qsb";
        if (QFile::exists(path))
            return path;
    }
    return name + ".qsb";
}
