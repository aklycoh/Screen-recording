#include "app/ApplicationContext.h"
#include "ui/MainWindow.h"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    ApplicationContext context;
    MainWindow window(context);
    window.show();
    return app.exec();
}
