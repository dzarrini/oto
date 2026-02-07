#include <fftw3.h>
#include <math.h>
#include <pipewire/pipewire.h>
#include <spa/debug/types.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/audio/type-info.h>

#define FFT_FRAMES 1024

struct data {
  struct pw_main_loop *loop;
  struct pw_stream *stream;
  struct spa_audio_info format;

  double window[FFT_FRAMES];
  fftw_complex frequency[FFT_FRAMES / 2 + 1];
  fftw_plan plan;
};

static void on_process(void *userdata) {
  struct data *data = userdata;
  struct pw_buffer *b;
  struct spa_buffer *buf;
  float *samples;
  uint32_t n, n_channels, n_samples;
  uint32_t i;
  double re, im;

  if ((b = pw_stream_dequeue_buffer(data->stream)) == NULL) {
    pw_log_warn("out of buffers: %m");
    return;
  }

  buf = b->buffer;
  if ((samples = buf->datas[0].data) == NULL) {
    pw_stream_queue_buffer(data->stream, b);
    return;
  }

  if (data->plan == NULL) {
    data->plan = fftw_plan_dft_r2c_1d(FFT_FRAMES, data->window, data->frequency,
                                      FFTW_MEASURE);
  }

  n_channels = data->format.info.raw.channels;
  n_samples = buf->datas[0].chunk->size / sizeof(float);

  fprintf(stdout, "captured %d samples\n", n_samples / n_channels);

  // TODO(zarrini): Only process 1 channel.
  for (n = 0, i = 0; n < n_samples; n += n_channels, i++) {
    data->window[i] = (double)samples[n];
  }
  fftw_execute(data->plan);
  for (i = 0; i <= FFT_FRAMES / 2; ++i) {
    re = data->frequency[i][0];
    im = data->frequency[i][1];
    fprintf(stdout, "[%d]: %f\n", i, sqrt(re * re + im * im));
  }
  fflush(stdout);
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

  if (data.plan != NULL) {
    fftw_destroy_plan(data.plan);
  }
  pw_main_loop_run(data.loop);
  pw_stream_destroy(data.stream);
  pw_main_loop_destroy(data.loop);
  pw_deinit();
  return 0;
}
