#include "mainwindow.h"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("FileSync"));
    QApplication::setOrganizationName(QStringLiteral("smizu-clang"));

    const int bundledFont = QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/NotoSansJP-Regular.ttf"));
    if (bundledFont >= 0) {
        const QStringList fontFamilies = QFontDatabase::applicationFontFamilies(bundledFont);
        if (!fontFamilies.isEmpty()) {
            QFont font(fontFamilies.first());
            font.setStyleHint(QFont::SansSerif);
            QApplication::setFont(font);
        }
    }

    MainWindow window;
    window.show();
    return app.exec();
}
