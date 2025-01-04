#include "apngplugin.h"

#include <QByteArray>
#include <QIODevice>

#include "apnghandler.h"

QImageIOPlugin::Capabilities APNGPlugin::capabilities(
    QIODevice *device, const QByteArray &format) const
{
    if (format == "apng") {
        return CanRead;
    }
    //  no detection for Seer
    // if (format.isEmpty() && APNGHandler::canRead(device)) {
    //     return CanRead;
    // }
    return {};
}

QImageIOHandler *APNGPlugin::create(QIODevice *device,
                                    const QByteArray &format) const
{
    APNGHandler *handler = new APNGHandler;
    handler->setDevice(device);
    handler->setFormat(format);
    return handler;
}
