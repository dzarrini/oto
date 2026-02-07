#include <fftw3.h>
#include <math.h>
#include <pipewire/pipewire.h>
#include <spa/debug/types.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/audio/type-info.h>

#define FFT_FRAMES 2048
#define BUCKETS 64
#define DB_FLOOR -80.0
#define DB_CEIL  -20.0

struct data {
  struct pw_main_loop *loop;
  struct pw_stream *stream;
  struct spa_audio_info format;

  uint32_t time_index;
  double timebuf[FFT_FRAMES];
  double window[FFT_FRAMES];
  fftw_complex frequency[FFT_FRAMES / 2 + 1];
  fftw_plan plan;

  double power[FFT_FRAMES / 2 + 1];
  double bars[BUCKETS];
};

static void draw_bars_vertical(const double *bars, int nbars, int H) {
  uint32_t i = 0;
  for (i = 0; i < nbars; ++i) {
    printf("%d: [%f]\n", i, bars[i]);
  }
  fflush(stdout);
}

static void populate_hann_window(double* window, uint32_t n) {
  uint32_t i;
  for (i = 0; i < n; ++i) {
    window[i] = 0.5 - 0.5*cos((2 * M_PI * i) / (n - 1));
  }
}

static void compute_power(fftw_complex frequency[], double* power, uint32_t n) {
  uint32_t i;
  double re, im;
  for (i = 0; i < n; ++i) {
    re = frequency[i][0];
    im = frequency[i][1];
    power[i] = re * re + im * im;
  }
}

static void compute_bars(double* power, double* bars, uint32_t n) {
  double sum;
  uint32_t i, count;
  uint32_t b0, b1;
  uint32_t stride = n / BUCKETS;
  for (i = 0; i < BUCKETS; ++i) {
    b0 = i * stride;
    b1 = (i == BUCKETS - 1) ? n : (b0 + stride);

    sum = 0;
    count = 0;
    for (uint32_t b = b0; b < b1; b++) {
      if (b == 0) continue;
      sum += power[b];
      count++;
    }
    bars[i] = (count > 0) ? sum / (double)count : 0.0;
    bars[i] = 10.0 * log10(1e-12 + bars[i]);
    bars[i] = (bars[i] - DB_FLOOR) / (DB_CEIL - DB_FLOOR);
    if (bars[i] < 0) bars[i] = 0;
    if (bars[i] > 1) bars[i] = 1;
  }
}

static void on_process(void *userdata) {
  struct data *data = userdata;
  struct pw_buffer *b;
  struct spa_buffer *buf;
  float *samples;
  uint32_t n, n_channels, n_samples;
  uint32_t i;

  if ((b = pw_stream_dequeue_buffer(data->stream)) == NULL) {
    pw_log_warn("out of buffers: %m");
    return;
  }

  buf = b->buffer;
  if ((samples = buf->datas[0].data) == NULL) {
    pw_stream_queue_buffer(data->stream, b);
    return;
  }

  n_channels = data->format.info.raw.channels;
  n_samples = buf->datas[0].chunk->size / sizeof(float);

  // TODO(zarrini): Only process 1 channel only.
  for (n = 0, i = 0; n < n_samples; n += n_channels, i++) {
    data->timebuf[data->time_index++] = (double)samples[n];
    if (data->time_index == FFT_FRAMES) {
      data->time_index = 0;
      for(int j = 0; j < FFT_FRAMES; ++j) {
        data->timebuf[j] *= data->window[j];
      }
      fftw_execute(data->plan);
      compute_power(data->frequency, data->power, FFT_FRAMES / 2 + 1);
      compute_bars(data->power, data->bars, FFT_FRAMES / 2 + 1);
    }
  }
  draw_bars_vertical(data->bars, BUCKETS, 80);
  pw_stream_queue_buffer(data->stream, b);
}

static void on_param_changed(void *userdata, uint32_t id,
                             const struct spa_pod *param) {
  struct data *data = userdata;

  if (param == NULL || id != SPA_PARAM_Format)
    return;

  if (spa_format_parse(param, &data->format.media_type,
                       &data->format.media_subtype) < 0)
    return;

  if (data->format.media_type != SPA_MEDIA_TYPE_audio ||
      data->format.media_subtype != SPA_MEDIA_SUBTYPE_raw)
    return;

  if (spa_format_audio_raw_parse(param, &data->format.info.raw) < 0)
    return;

  if (data->format.info.raw.format != SPA_AUDIO_FORMAT_F32_LE) {
    fprintf(stderr, "unexpected format: exepcted F32_LE; got %s\n",
            spa_debug_type_find_name(spa_type_audio_format,
                                     data->format.info.raw.format));
    exit(1);
  }

  printf("got audio format:\n");
  printf("  format: %d (%s)\n", data->format.info.raw.format,
         spa_debug_type_find_name(spa_type_audio_format,
                                  data->format.info.raw.format));
  printf("  capturing rate: %dx%d\n", data->format.info.raw.rate,
         data->format.info.raw.channels);
}

static const struct pw_stream_events stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .param_changed = on_param_changed,
    .process = on_process,
};

int main(int argc, char *argv[]) {
  struct data data;
  memset(&data, 0, sizeof(data));

  populate_hann_window(data.window, FFT_FRAMES);
  data.plan = fftw_plan_dft_r2c_1d(FFT_FRAMES, data.timebuf, data.frequency,
                                    FFTW_MEASURE);
  data.time_index = 0;

  const struct spa_pod *params[1];
  uint8_t buffer[1024];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

  pw_init(&argc, &argv);

  data.loop = pw_main_loop_new(NULL);

  data.stream = pw_stream_new_simple(
      pw_main_loop_get_loop(data.loop), "audio-capture",
      pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY,
                        "Capture", PW_KEY_MEDIA_ROLE, "Music", NULL),
      &stream_events, &data);

  params[0] = spa_format_audio_raw_build(
      &b, SPA_PARAM_EnumFormat,
      &SPA_AUDIO_INFO_RAW_INIT(.format = SPA_AUDIO_FORMAT_F32_LE));

  pw_stream_connect(data.stream, PW_DIRECTION_INPUT, PW_ID_ANY,
                    PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
                        PW_STREAM_FLAG_RT_PROCESS,
                    params, 1);

  pw_main_loop_run(data.loop);

  // cleanup.
  if (data.plan != NULL) {
    fftw_destroy_plan(data.plan);
  }
  pw_stream_destroy(data.stream);
  pw_main_loop_destroy(data.loop);
  pw_deinit();
  return 0;
}
