#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QDir>

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;

    // Load QML from filesystem so relative shader paths resolve to the same directory
    QString qmlPath = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("main.qml");
    engine.load(QUrl::fromLocalFile(qmlPath));
    if (engine.rootObjects().isEmpty())
        return -1;
    return app.exec();
}
