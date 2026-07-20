// zbar_decode_test — standalone check that the native decode path (the exact zbar C
// API usage in camera_engine.cpp's decode_worker) decodes a real QR. Reads a raw
// grayscale (Y800) image from a file and runs it through zbar QR-only, printing the
// decoded payload. Autonomous decode validation independent of a live camera scene.
//
// Cross-compiled by scripts/build-camera-probe.sh (same sysroot + link recipe as the
// engine) and run on the device: ./zbar_decode_test qr480.gray 480 480
#include <zbar.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace zbar;

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("usage: %s <gray_file> <w> <h> [ystride]\n", argv[0]);
        return 1;
    }
    const char *path = argv[1];
    int w = atoi(argv[2]);
    int h = atoi(argv[3]);
    int ystride = (argc > 4) ? atoi(argv[4]) : w;  // optional: mimic the 512 stride

    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("cannot open %s\n", path);
        return 1;
    }
    std::vector<unsigned char> file((size_t)ystride * h);
    size_t got = fread(file.data(), 1, file.size(), f);
    fclose(f);
    printf("read %zu bytes (expected %zu)\n", got, file.size());

    // Stride-strip exactly like decode_worker (no-op when ystride==w).
    std::vector<unsigned char> gray((size_t)w * h);
    for (int y = 0; y < h; ++y) {
        memcpy(gray.data() + (size_t)y * w, file.data() + (size_t)y * ystride, w);
    }

    zbar_image_scanner_t *s = zbar_image_scanner_create();
    zbar_image_scanner_set_config(s, ZBAR_NONE, ZBAR_CFG_ENABLE, 0);
    zbar_image_scanner_set_config(s, ZBAR_QRCODE, ZBAR_CFG_ENABLE, 1);
    zbar_image_scanner_enable_cache(s, 0);

    zbar_image_t *img = zbar_image_create();
    zbar_image_set_format(img, zbar_fourcc('Y', '8', '0', '0'));
    zbar_image_set_size(img, w, h);
    zbar_image_set_data(img, gray.data(), (unsigned long)w * h, nullptr);

    int n = zbar_scan_image(s, img);
    printf("ZBAR symbols = %d\n", n);
    for (const zbar_symbol_t *sym = zbar_image_first_symbol(img); sym; sym = zbar_symbol_next(sym)) {
        int len = zbar_symbol_get_data_length(sym);
        printf("DECODED type=%s len=%d data=%.*s\n",
               zbar_get_symbol_name(zbar_symbol_get_type(sym)),
               len, len, zbar_symbol_get_data(sym));
    }
    zbar_image_destroy(img);
    zbar_image_scanner_destroy(s);
    return n > 0 ? 0 : 2;
}
