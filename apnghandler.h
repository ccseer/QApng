#pragma once

#include <QImage>
#include <QImageIOHandler>
#include <QVariant>

class APNGHandler : public QImageIOHandler {
public:
    static bool canRead(QIODevice *device);
    static bool ensureParsed(QIODevice *device,
                             int &loopCount,
                             QVector<QImage> &frames,
                             QVector<int> &delays);

    APNGHandler();
    ~APNGHandler() override = default;

    bool canRead() const override;
    bool read(QImage *image) override;

    bool supportsOption(ImageOption option) const override;
    QVariant option(ImageOption option) const override;

    int currentImageNumber() const override;
    int imageCount() const override;
    bool jumpToNextImage() override;
    bool jumpToImage(int imageNumber) override;
    int nextImageDelay() const override;
    int loopCount() const override;

private:
    bool ensureParsed() const;

private:
    bool m_parsed;
    QVector<QImage> m_frames;
    QVector<int> m_delays;
    int m_loopCount;
    int m_currentFrame;
};
