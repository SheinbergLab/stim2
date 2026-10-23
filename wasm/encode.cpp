/* Trial-video encoder. One straight RGBA frame in, one AVCC access unit out.
   Input rows are bottom-up, as gl.readPixels returns them. Limited-range
   4:2:0 matches the yuv420p files ffmpeg writes for the player. */

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include <x264.h>

static x264_t *enc = nullptr;
static x264_picture_t pic;
static int pic_ready = 0;
static int width = 0;
static int height = 0;
static int64_t next_pts = 0;
static std::vector<uint8_t> sample;
static std::vector<uint8_t> avcc;
static int sample_key = 0;
static char errbuf[512];

static void set_err(const char *msg) {
    std::snprintf(errbuf, sizeof errbuf, "%s", msg ? msg : "encoder error");
}

static void log_cb(void *, int level, const char *fmt, va_list ap) {
    if (level > X264_LOG_ERROR) return;
    std::vsnprintf(errbuf, sizeof errbuf, fmt, ap);
}

static uint8_t clamp_u8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

/* BT.601 limited range. WebGL row 0 is the bottom of the picture. */
static void rgba_to_i420(const uint8_t *rgba) {
    const int stride = width * 4;
    uint8_t *Y = pic.img.plane[0];
    uint8_t *U = pic.img.plane[1];
    uint8_t *V = pic.img.plane[2];
    const int ys = pic.img.i_stride[0];
    const int cs = pic.img.i_stride[1];

    for (int y = 0; y < height; y++) {
        const uint8_t *row = rgba + (size_t)(height - 1 - y) * stride;
        uint8_t *dy = Y + y * ys;
        for (int x = 0; x < width; x++) {
            int R = row[x * 4];
            int G = row[x * 4 + 1];
            int B = row[x * 4 + 2];
            dy[x] = clamp_u8(((66 * R + 129 * G + 25 * B + 128) >> 8) + 16);
        }
    }
    for (int y = 0; y < height; y += 2) {
        const uint8_t *r0 = rgba + (size_t)(height - 1 - y) * stride;
        const uint8_t *r1 = rgba + (size_t)(height - 2 - y) * stride;
        uint8_t *du = U + (y / 2) * cs;
        uint8_t *dv = V + (y / 2) * cs;
        for (int x = 0; x < width; x += 2) {
            int R = 0, G = 0, B = 0;
            const uint8_t *px[4] = {
                r0 + x * 4, r0 + (x + 1) * 4, r1 + x * 4, r1 + (x + 1) * 4
            };
            for (int i = 0; i < 4; i++) {
                R += px[i][0];
                G += px[i][1];
                B += px[i][2];
            }
            R /= 4;
            G /= 4;
            B /= 4;
            du[x / 2] = clamp_u8(((-38 * R - 74 * G + 112 * B + 128) >> 8) + 128);
            dv[x / 2] = clamp_u8(((112 * R - 94 * G - 18 * B + 128) >> 8) + 128);
        }
    }
}

static int store_sample(x264_nal_t *nals, int nnal, x264_picture_t *out) {
    sample.clear();
    sample_key = out->b_keyframe ? 1 : 0;
    for (int i = 0; i < nnal; i++) {
        if (nals[i].i_type == NAL_SPS || nals[i].i_type == NAL_PPS ||
            nals[i].i_type == NAL_SEI || nals[i].i_type == NAL_AUD)
            continue;
        if (nals[i].i_payload < 4) continue;
        sample.insert(sample.end(), nals[i].p_payload, nals[i].p_payload + nals[i].i_payload);
    }
    if (sample.empty()) {
        set_err("encoder produced an empty frame");
        return -1;
    }
    return 1;
}

static int encode_pic(x264_picture_t *in) {
    x264_nal_t *nals = nullptr;
    int nnal = 0;
    x264_picture_t out;
    int n = x264_encoder_encode(enc, &nals, &nnal, in, &out);
    if (n < 0) {
        set_err("x264_encoder_encode failed");
        return -1;
    }
    if (n == 0 || nnal == 0) return 0;
    return store_sample(nals, nnal, &out);
}

static int build_avcc(x264_nal_t *nals, int nnal) {
    const uint8_t *sps = nullptr;
    const uint8_t *pps = nullptr;
    int sps_len = 0;
    int pps_len = 0;
    for (int i = 0; i < nnal; i++) {
        if (nals[i].i_payload < 5) continue;
        const uint8_t *payload = nals[i].p_payload + 4;
        int len = nals[i].i_payload - 4;
        if (nals[i].i_type == NAL_SPS) {
            sps = payload;
            sps_len = len;
        } else if (nals[i].i_type == NAL_PPS) {
            pps = payload;
            pps_len = len;
        }
    }
    if (!sps || !pps) {
        set_err("encoder did not return SPS and PPS");
        return -1;
    }
    avcc.resize((size_t)8 + sps_len + 3 + pps_len);
    uint8_t *b = avcc.data();
    b[0] = 1;
    b[1] = sps[1];
    b[2] = sps[2];
    b[3] = sps[3];
    b[4] = 0xfc | 3;
    b[5] = 0xe0 | 1;
    b[6] = (uint8_t)(sps_len >> 8);
    b[7] = (uint8_t)sps_len;
    std::memcpy(b + 8, sps, (size_t)sps_len);
    int o = 8 + sps_len;
    b[o++] = 1;
    b[o++] = (uint8_t)(pps_len >> 8);
    b[o++] = (uint8_t)pps_len;
    std::memcpy(b + o, pps, (size_t)pps_len);
    return 0;
}

extern "C" {

const char *enc_error(void) { return errbuf; }

int enc_open(int w, int h, int preset) {
    errbuf[0] = 0;
    if (enc) {
        set_err("encoder is already open");
        return -1;
    }
    if (w < 2 || h < 2 || (w & 1) || (h & 1)) {
        set_err("frame size must be even and at least 2");
        return -1;
    }
    const char *name = preset == 1 ? "veryfast" : (preset == 0 ? "superfast" : nullptr);
    if (!name) {
        set_err("preset must be 0 (superfast) or 1 (veryfast)");
        return -1;
    }
    x264_param_t param;
    if (x264_param_default_preset(&param, name, nullptr) < 0) {
        set_err("unknown x264 preset");
        return -1;
    }
    param.i_csp = X264_CSP_I420;
    param.i_width = w;
    param.i_height = h;
    param.i_fps_num = 60;
    param.i_fps_den = 1;
    param.i_timebase_num = 1;
    param.i_timebase_den = 60;
    param.b_vfr_input = 1;
    param.b_annexb = 0;
    param.b_repeat_headers = 0;
    param.b_aud = 0;
    param.i_keyint_max = 60;
    param.i_keyint_min = 60;
    param.i_scenecut_threshold = 0;
    param.i_bframe = 0;
    param.i_bframe_adaptive = 0;
    param.i_bframe_pyramid = X264_B_PYRAMID_NONE;
    param.i_threads = 1;
    param.b_sliced_threads = 0;
    param.i_sync_lookahead = 0;
    param.rc.i_rc_method = X264_RC_CRF;
    param.rc.f_rf_constant = 23;
    param.pf_log = log_cb;
    param.p_log_private = nullptr;
    param.i_log_level = X264_LOG_ERROR;
    if (x264_param_apply_profile(&param, "high") < 0) {
        set_err("x264 rejected the high profile");
        return -1;
    }
    enc = x264_encoder_open(&param);
    if (!enc) {
        if (!errbuf[0]) set_err("x264_encoder_open failed");
        return -1;
    }
    if (x264_picture_alloc(&pic, X264_CSP_I420, w, h) < 0) {
        x264_encoder_close(enc);
        enc = nullptr;
        set_err("x264_picture_alloc failed");
        return -1;
    }
    pic_ready = 1;
    width = w;
    height = h;
    next_pts = 0;
    x264_nal_t *headers = nullptr;
    int nheaders = 0;
    if (x264_encoder_headers(enc, &headers, &nheaders) < 0 ||
        build_avcc(headers, nheaders) != 0) {
        x264_picture_clean(&pic);
        pic_ready = 0;
        x264_encoder_close(enc);
        enc = nullptr;
        if (!errbuf[0]) set_err("x264_encoder_headers failed");
        return -1;
    }
    return 0;
}

int enc_rgba(const uint8_t *rgba, int hold) {
    if (!enc || !rgba) {
        set_err("encoder is not open");
        return -1;
    }
    if (hold < 1) {
        set_err("hold must be a positive frame count");
        return -1;
    }
    rgba_to_i420(rgba);
    pic.i_pts = next_pts;
    next_pts += hold;
    pic.i_type = X264_TYPE_AUTO;
    return encode_pic(&pic);
}

int enc_flush(void) {
    if (!enc) {
        set_err("encoder is not open");
        return -1;
    }
    if (x264_encoder_delayed_frames(enc) < 1) return 0;
    return encode_pic(nullptr);
}

const uint8_t *enc_sample(void) { return sample.empty() ? nullptr : sample.data(); }
int enc_sample_size(void) { return (int)sample.size(); }
int enc_sample_key(void) { return sample_key; }
const uint8_t *enc_avcc(void) { return avcc.empty() ? nullptr : avcc.data(); }
int enc_avcc_size(void) { return (int)avcc.size(); }

void enc_close(void) {
    if (pic_ready) {
        x264_picture_clean(&pic);
        pic_ready = 0;
    }
    if (enc) {
        x264_encoder_close(enc);
        enc = nullptr;
    }
    sample.clear();
    avcc.clear();
    sample_key = 0;
    width = 0;
    height = 0;
    next_pts = 0;
}

}
