#pragma once

#include <QImageIOPlugin>

class APNGPlugin : public QImageIOPlugin {
    Q_OBJECT

    Q_PLUGIN_METADATA(IID QImageIOHandlerFactoryInterface_iid FILE "apng.json")
public:
    Capabilities capabilities(QIODevice *device,
                              const QByteArray &format) const override;

    QImageIOHandler *create(QIODevice *device,
                            const QByteArray &format
                            = QByteArray()) const override;
};
