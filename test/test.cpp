#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>

#include "../apnghandler.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QFile f("a.apng");
    if (!f.open(f.ReadOnly)) {
        qDebug() << f.errorString();
        return -1;
    }
    APNGHandler p;
    int loopCount = 0;
    QVector<QImage> frames;
    QVector<int> delays;
    qDebug() << p.ensureParsed(&f, loopCount, frames, delays);
    qDebug() << loopCount << frames.size() << frames;  //<< delays;
    f.close();
    return 0;
}
