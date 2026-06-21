#include "FluidSimShaders.h"
#include <QFile>
#include <QCoreApplication>
#include <QDir>

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
        // Relative to QML2_IMPORT_PATH / plugin deployment dir
        QDir::homePath() + "/.local/lib/qml/FluidSim/shaders/",
        // Resource path (for embedded Qt resources)
        ":/shaders/",
    };
    for (const QString &base : searchPaths) {
        QString path = base + name + ".qsb";
        if (QFile::exists(path))
            return path;
    }
    return name + ".qsb";
}
