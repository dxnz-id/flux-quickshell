#pragma once
#include <QtGui/private/qrhi_p.h>
#include <QString>

class FluxShaders {
public:
    static QShader loadShader(const QString &name);
    static QString shaderPath(const QString &name);
};
