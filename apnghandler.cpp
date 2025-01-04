#include "apnghandler.h"

#include <QDebug>

#include "png.h"

#define eprint qDebug() << __LINE__ << Q_FUNC_INFO

APNGHandler::APNGHandler() : m_parsed(false), m_loopCount(0), m_currentFrame(0)
{
}

bool APNGHandler::canRead() const
{
    return canRead(device());
}

bool APNGHandler::canRead(QIODevice *device)
{
    if (!device || !device->isReadable()) {
        eprint;
        return false;
    }

    device->seek(0);

    constexpr int PNG_SIG_SIZE = 8;
    QByteArray sig             = device->peek(PNG_SIG_SIZE);
    if (sig.size() < PNG_SIG_SIZE
        || png_sig_cmp(reinterpret_cast<png_const_bytep>(sig.constData()), 0,
                       PNG_SIG_SIZE)
               != 0) {
        return false;
    }
    return true;
}

bool APNGHandler::read(QImage *image)
{
    if (!ensureParsed()) {
        eprint;
        return false;
    }

    if (m_frames.isEmpty()) {
        eprint;
        return false;
    }
    if (m_currentFrame < 0 || m_currentFrame >= m_frames.size()) {
        m_currentFrame = 0;
    }
    *image = m_frames.at(m_currentFrame++);
    return true;
}

bool APNGHandler::ensureParsed() const
{
    // 1) If already parsed, do nothing
    if (m_parsed) {
        return true;
    }
    auto p      = const_cast<APNGHandler *>(this);
    p->m_parsed = true;
    return ensureParsed(device(), p->m_loopCount, p->m_frames, p->m_delays);
}

int APNGHandler::currentImageNumber() const
{
    if (!ensureParsed()) {
        return 0;
    }
    return m_currentFrame;
}

int APNGHandler::imageCount() const
{
    if (!ensureParsed()) {
        eprint;
        return 0;
    }
    return m_frames.size();
}

bool APNGHandler::jumpToNextImage()
{
    if (!ensureParsed()) {
        return false;
    }
    if (++m_currentFrame < m_frames.size()) {
        return true;
    }
    return false;
}

bool APNGHandler::jumpToImage(int imageNumber)
{
    if (!ensureParsed() || imageNumber < 0) {
        eprint;
        return false;
    }
    m_currentFrame = imageNumber;
    return imageNumber < m_frames.size();
}

int APNGHandler::nextImageDelay() const
{
    if (!ensureParsed()) {
        eprint;
        return 0;
    }
    if (m_currentFrame <= 0 || m_currentFrame >= m_frames.size()) {
        return m_delays.at(0);
    }
    return m_delays.at(m_currentFrame - 1);
}

int APNGHandler::loopCount() const
{
    if (!ensureParsed()) {
        eprint;
        return 0;
    }
    return m_loopCount;
}

bool APNGHandler::supportsOption(ImageOption option) const
{
    switch (option) {
    case Animation:
    case Size:
        return true;
    default:
        return false;
    }
}
QVariant APNGHandler::option(ImageOption option) const
{
    if (!ensureParsed()) {
        return QVariant();
    }

    switch (option) {
    case Animation: {
        return !m_frames.empty();
    }
    case Size:
        if (!m_frames.isEmpty()) {
            return m_frames.first().size();
        }
        return QVariant();
    default:
        break;
    }

    return QVariant();
}

//////////////////////////////////////////////////////////////////////////
/// structs
struct FrameBuf {
    quint32 x      = 0;
    quint32 y      = 0;
    quint32 width  = 0;
    quint32 height = 0;

    quint16 delay_num = 0;
    quint16 delay_den = 100;  // default to avoid div-by-zero

    png_byte dispose_op = PNG_DISPOSE_OP_NONE;
    png_byte blend_op   = PNG_BLEND_OP_SOURCE;

    png_uint_32 rowbytes = 0;
    int channels         = 4;        // Typically RGBA
    png_bytep *rows      = nullptr;  // array of row pointers
    png_byte *p          = nullptr;  // raw pixel buffer
};

struct ApngContext {
    // Basic
    QIODevice *device  = nullptr;
    png_structp pngPtr = nullptr;
    png_infop infoPtr  = nullptr;
    bool hasError      = false;

    // Animation info
    bool isAnimated    = false;
    bool skipFirst     = false;
    quint32 frameCount = 1;

    // Current "composited" image & buffer for reading
    QImage lastImage;
    FrameBuf curFrame;

    // Results
    QVector<QImage> frames;
    QVector<int> delays;
    int loopCount = 0;  // 0 means infinite in APNG spec
};

/// helpers
static void freeFrameBuf(FrameBuf &f)
{
    if (f.rows) {
        delete[] f.rows;
        f.rows = nullptr;
    }
    if (f.p) {
        delete[] f.p;
        f.p = nullptr;
    }
}

static void copyFrameToImage(QImage &dest, const FrameBuf &f, bool source)
{
    // Copy RGBA pixels from f.rows into `dest`, at offsets (f.x, f.y).
    // If `source==true`, we overwrite.
    // If `source==false`, we might do some other logic (but not used here).
    for (quint32 y = 0; y < f.height; y++) {
        const png_bytep row = f.rows[y];
        for (quint32 x = 0; x < f.width; x++) {
            quint32 px = x * 4;  // B,G,R,A if we used png_set_bgr
            QColor c;
            c.setBlue(row[px + 0]);
            c.setGreen(row[px + 1]);
            c.setRed(row[px + 2]);
            c.setAlpha(row[px + 3]);
            dest.setPixelColor(x + f.x, y + f.y, c);
        }
    }
}

static void blendFrame(QImage &dest, const FrameBuf &f)
{
    // "Over" blend the current frame onto `dest`.
    for (quint32 y = 0; y < f.height; y++) {
        const png_bytep row = f.rows[y];
        for (quint32 x = 0; x < f.width; x++) {
            quint32 px = x * 4;
            QColor src;
            src.setBlue(row[px + 0]);
            src.setGreen(row[px + 1]);
            src.setRed(row[px + 2]);
            src.setAlpha(row[px + 3]);

            if (src.alpha() == 0xff) {
                // fully opaque => just overwrite
                dest.setPixelColor(x + f.x, y + f.y, src);
            }
            else if (src.alpha() != 0x00) {
                // do a simple alpha blend with existing pixel
                QColor dst = dest.pixelColor(x + f.x, y + f.y);
                int outA
                    = src.alpha() + dst.alpha() * (0xff - src.alpha()) / 0xff;

                if (outA != 0) {
                    int outR = (src.red() * src.alpha()
                                + dst.red() * dst.alpha() * (0xff - src.alpha())
                                      / 0xff)
                               / outA * 0xff;
                    int outG = (src.green() * src.alpha()
                                + dst.green() * dst.alpha()
                                      * (0xff - src.alpha()) / 0xff)
                               / outA * 0xff;
                    int outB = (src.blue() * src.alpha()
                                + dst.blue() * dst.alpha()
                                      * (0xff - src.alpha()) / 0xff)
                               / outA * 0xff;

                    // clamp
                    outR = qBound(0, outR, 255);
                    outG = qBound(0, outG, 255);
                    outB = qBound(0, outB, 255);
                    // outA is 0..255
                    dest.setPixelColor(x + f.x, y + f.y,
                                       QColor(outR, outG, outB, outA));
                }
                else {
                    // alpha=0 => fully transparent => do nothing
                }
            }
            // else alpha=0 => fully transparent => do nothing
        }
    }
}

//  read length bytes from device, pass to libpng
//  If length==0, read exactly one chunk (size+type+data+CRC).
static bool readChunk(ApngContext *ctx, quint32 length = 0)
{
    if (!ctx->device || ctx->device->atEnd()) {
        return false;
    }

    QByteArray data;
    if (length == 0) {
        // read chunk length (4 bytes)
        QByteArray sizeBytes = ctx->device->read(4);
        if (sizeBytes.size() < 4)
            return false;

        quint32 chunkLen = qFromBigEndian<quint32>(sizeBytes.constData());
        chunkLen += 8;  // +4 for 'type', +4 for 'CRC'
        // read chunk data
        data = sizeBytes + ctx->device->read(chunkLen);
        // minus the 4 we already have
        if ((int)chunkLen > data.size() - 4) {
            return false;
        }
    }
    else {
        data = ctx->device->read(length);
        if (data.size() < (int)length) {
            return false;
        }
    }

    png_process_data(ctx->pngPtr, ctx->infoPtr,
                     reinterpret_cast<png_bytep>(data.data()),
                     static_cast<png_size_t>(data.size()));
    return !ctx->device->atEnd();
}

/// callbacks
// APNG: Called at the start of each animation frame
static void frameInfoCallback(png_structp pngPtr, png_uint_32 /*frame_num*/)
{
    auto ctx     = reinterpret_cast<ApngContext *>(png_get_io_ptr(pngPtr));
    auto infoPtr = ctx->infoPtr;

    // Collect next frame offsets etc.
    FrameBuf &f  = ctx->curFrame;
    f.x          = png_get_next_frame_x_offset(pngPtr, infoPtr);
    f.y          = png_get_next_frame_y_offset(pngPtr, infoPtr);
    f.width      = png_get_next_frame_width(pngPtr, infoPtr);
    f.height     = png_get_next_frame_height(pngPtr, infoPtr);
    f.delay_num  = png_get_next_frame_delay_num(pngPtr, infoPtr);
    f.delay_den  = png_get_next_frame_delay_den(pngPtr, infoPtr);
    f.dispose_op = png_get_next_frame_dispose_op(pngPtr, infoPtr);
    f.blend_op   = png_get_next_frame_blend_op(pngPtr, infoPtr);
}

// APNG: Called when a frame is complete
static void frameEndCallback(png_structp pngPtr, png_uint_32 frame_num)
{
    auto ctx    = reinterpret_cast<ApngContext *>(png_get_io_ptr(pngPtr));
    FrameBuf &f = ctx->curFrame;
    QImage &img = ctx->lastImage;

    // If the APNG's "first frame is hidden" bit is set, skip that first frame
    if (frame_num == 0 && ctx->skipFirst) {
        return;
    }

    // If this is effectively the "first displayed frame", we ensure correct
    // blend/dispose for some edge cases:
    if ((frame_num == 0 && !ctx->skipFirst)
        || (frame_num == 1 && ctx->skipFirst)) {
        // Force SOURCE blend on first visible frame
        f.blend_op = PNG_BLEND_OP_SOURCE;
        // If dispose_op was PREVIOUS, use BACKGROUND instead
        if (f.dispose_op == PNG_DISPOSE_OP_PREVIOUS) {
            f.dispose_op = PNG_DISPOSE_OP_BACKGROUND;
        }
    }

    // Possibly store the "before" image if disposal=PREVIOUS
    QImage temp;
    if (f.dispose_op == PNG_DISPOSE_OP_PREVIOUS) {
        temp = img;
    }

    // Composite this frame into `img`
    if (f.blend_op == PNG_BLEND_OP_OVER) {
        blendFrame(img, f);
    }
    else {
        copyFrameToImage(img, f, true);
    }
    // Add resulting frame to the list
    int delayMs = 0;
    if (f.delay_den > 0) {
        delayMs = static_cast<int>((1000.0 * f.delay_num) / f.delay_den);
    }
    ctx->frames.push_back(img);
    ctx->delays.push_back(delayMs);

    // If disposal=PREVIOUS, restore the old image
    if (f.dispose_op == PNG_DISPOSE_OP_PREVIOUS) {
        img = temp;
    }
    // If disposal=BACKGROUND, clear the region to transparent
    else if (f.dispose_op == PNG_DISPOSE_OP_BACKGROUND) {
        for (quint32 yy = 0; yy < f.height; yy++) {
            for (quint32 xx = 0; xx < f.width; xx++) {
                img.setPixelColor(xx + f.x, yy + f.y, Qt::transparent);
            }
        }
    }
}

// Called once the PNG header is read:
static void infoCallback(png_structp pngPtr, png_infop infoPtr)
{
    auto ctx = reinterpret_cast<ApngContext *>(png_get_io_ptr(pngPtr));

    // Expand to RGBA, remove 16-bit, etc. (like the original code)
    png_set_expand(pngPtr);
    png_set_strip_16(pngPtr);
    png_set_gray_to_rgb(pngPtr);
    png_set_add_alpha(pngPtr, 0xFF, PNG_FILLER_AFTER);
    png_set_bgr(pngPtr);  // optional: make channels BGR(A)

    // Handle interlace
    (void)png_set_interlace_handling(pngPtr);

    // Update info for reading
    png_read_update_info(pngPtr, infoPtr);

    // Grab final width/height
    quint32 width  = png_get_image_width(pngPtr, infoPtr);
    quint32 height = png_get_image_height(pngPtr, infoPtr);

    ctx->lastImage = QImage(width, height, QImage::Format_ARGB32);
    ctx->lastImage.fill(Qt::transparent);

    // Prepare current frame buffer
    FrameBuf &f  = ctx->curFrame;
    f.x          = 0;
    f.y          = 0;
    f.width      = width;
    f.height     = height;
    f.channels   = png_get_channels(pngPtr, infoPtr);
    f.delay_num  = 0;
    f.delay_den  = 10;  // default or fallback
    f.dispose_op = PNG_DISPOSE_OP_NONE;
    f.blend_op   = PNG_BLEND_OP_SOURCE;
    f.rowbytes   = png_get_rowbytes(pngPtr, infoPtr);

    f.p    = new png_byte[f.height * f.rowbytes];
    f.rows = new png_bytep[f.height];
    for (quint32 j = 0; j < f.height; j++) {
        f.rows[j] = f.p + j * f.rowbytes;
    }

    // Check if file is APNG
    if (png_get_valid(pngPtr, infoPtr, PNG_INFO_acTL)) {
        ctx->isAnimated = true;

        quint32 plays = 0;
        png_get_acTL(pngPtr, infoPtr, &ctx->frameCount, &plays);
        if (plays == 0) {
            // APNG infinite
            ctx->loopCount = -1;  // QMovie infinite
        }
        else {
            // For a positive 'plays', we subtract 1
            ctx->loopCount = int(plays) - 1;
        }
        // Check if first frame is hidden
        ctx->skipFirst = (png_get_first_frame_is_hidden(pngPtr, infoPtr) != 0);

        // Use frame callbacks
        png_set_progressive_frame_fn(
            pngPtr,
            [](png_structp p, png_uint_32 fnum) { frameInfoCallback(p, fnum); },
            [](png_structp p, png_uint_32 fnum) { frameEndCallback(p, fnum); });

        // If the first frame is not hidden, we explicitly call
        // frameInfoCallback for it right now. (Equivalent to what's in
        // ApngReader).
        if (!ctx->skipFirst) {
            frameInfoCallback(pngPtr, 0);
        }
    }
    else {
        // Not animated => single image
        ctx->isAnimated = false;
    }
}

// Called whenever a row’s worth of data is available
static void rowCallback(png_structp pngPtr,
                        png_bytep newRow,
                        png_uint_32 rowNum,
                        int /*pass*/)
{
    auto ctx    = reinterpret_cast<ApngContext *>(png_get_io_ptr(pngPtr));
    FrameBuf &f = ctx->curFrame;

    // Combine row into our row buffer
    png_progressive_combine_row(pngPtr, f.rows[rowNum], newRow);
}

// Called when the entire (single-frame) image is done
static void endCallback(png_structp pngPtr, png_infop)
{
    auto ctx = reinterpret_cast<ApngContext *>(png_get_io_ptr(pngPtr));
    if (ctx->isAnimated) {
        // We'll handle adding frames in frameEndCallback for APNG.
        // But if it's APNG with only 1 frame, frameEndCallback also occurs.
        return;
    }

    // Single-frame PNG => copy entire buffer to QImage
    copyFrameToImage(ctx->lastImage, ctx->curFrame, true /*source*/);
    ctx->frames.push_back(ctx->lastImage);
    ctx->delays.push_back(0);  // single-frame => no delay

    freeFrameBuf(ctx->curFrame);
}

bool APNGHandler::ensureParsed(QIODevice *device,
                               int &loopCount,
                               QVector<QImage> &frames,
                               QVector<int> &delays)
{
    // 2) Check PNG signature
    if (!canRead(device)) {
        qWarning() << "no read";
        return false;
    }
    // Create a local context
    ApngContext ctx;
    ctx.device = device;
    // 3) Create libpng read structs
    ctx.pngPtr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr,
                                        nullptr);
    if (!ctx.pngPtr) {
        qWarning() << "ensureParsed: png_create_read_struct failed";
        return false;
    }
    ctx.infoPtr = png_create_info_struct(ctx.pngPtr);
    if (!ctx.infoPtr) {
        qWarning() << "ensureParsed: png_create_info_struct failed";
        png_destroy_read_struct(&ctx.pngPtr, nullptr, nullptr);
        return false;
    }

    // setjmp for libpng error handling
    if (setjmp(png_jmpbuf(ctx.pngPtr))) {
        qWarning() << "ensureParsed: libpng error during parse";
        png_destroy_read_struct(&ctx.pngPtr, &ctx.infoPtr, nullptr);
        freeFrameBuf(ctx.curFrame);
        return false;
    }

    // 4) Register progressive callbacks
    png_set_progressive_read_fn(
        ctx.pngPtr,    // png struct
        &ctx,          // your custom pointer
        infoCallback,  // called after reading initial header
        rowCallback,   // called whenever a row is finished
        endCallback    // called after the single image is finished
                       // (for APNG, multiple frames => use frame callbacks)
    );

    // 5) Feed the signature first
    bool done = false;
    {
        // read the first 8 bytes (signature)
        readChunk(&ctx, 8);

        // keep reading chunk by chunk until we have read all frames
        // or the file ends, or we encounter an error.
        while (!device->atEnd()) {
            // Attempt to read exactly 1 chunk
            if (!readChunk(&ctx, 0))
                break;

            // If it's APNG and we already have the official number of frames,
            // we can check if we got them all:
            if (ctx.isAnimated && (uint)ctx.frames.size() == ctx.frameCount) {
                done = true;
                break;
            }
        }
    }

    // 6) Done. Clean up
    png_destroy_read_struct(&ctx.pngPtr, &ctx.infoPtr, nullptr);
    freeFrameBuf(ctx.curFrame);
    // If we got at least one frame, parse was successful
    if (!ctx.frames.isEmpty()) {
        loopCount = ctx.loopCount;
        frames    = ctx.frames;
        delays    = ctx.delays;
        return true;
    }
    return done;  // or false if no frames
}
