#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include <KLocalizedContext>
#include <KLocalizedString>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    KLocalizedString::setApplicationDomain("kweek");
    QGuiApplication::setOrganizationName(QStringLiteral("Kweek"));
    QGuiApplication::setApplicationName(QStringLiteral("Kweek"));
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.neldoreth.Kweek"));

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
    engine.loadFromModule("org.kweek.app", "Main");

    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    return app.exec();
}
