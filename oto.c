#include <fftw3.h>
#include <math.h>
#include <pipewire/pipewire.h>
#include <spa/debug/types.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/audio/type-info.h>
#include <string.h>

#define FFT_FRAMES 2048
#define DECAY_RATE 0.90
#define BAR_WIDTH 20
#define MAG_SCALE 0.02

struct data {
  struct pw_main_loop *loop;
  struct pw_stream *stream;
  struct spa_audio_info format;

  uint32_t channels;
  uint32_t rate;

  uint32_t time_index;
  double timebuf[FFT_FRAMES];
  double window[FFT_FRAMES];
  fftw_complex frequency[FFT_FRAMES / 2 + 1];
  fftw_plan plan;
  double mag[FFT_FRAMES / 2 + 1];

  double peak_bass;
  double peak_mid;
  double peak_treble;
};
typedef struct data Data;

static void populate_hann_window(double* window, uint32_t n) {
  uint32_t i;
  for (i = 0; i < n; ++i) {
    window[i] = 0.5 - 0.5*cos((2 * M_PI * i) / (n - 1));
  }
}

static void compute_mag(fftw_complex frequency[], double* mag, uint32_t n) {
  uint32_t i;
  double re, im;
  const double scale = 1.0 / FFT_FRAMES;
  for (i = 0; i < n; ++i) {
    re = frequency[i][0];
    im = frequency[i][1];
    mag[i] = sqrt(re * re + im * im) * scale;
  }
}

static double get_band_energy(
    double* mag,
    double freq_min,
    double freq_max,
    double freq_resolution) {
  uint32_t bin_min, bin_max;
  bin_min = freq_min / freq_resolution;
  bin_max = freq_max / freq_resolution;
  bin_max = fmin(bin_max, FFT_FRAMES / 2); 

  if (bin_min >= bin_max) return 0;

  double energy = 0;
  for (uint32_t i = bin_min; i < bin_max; ++i) {
    energy += mag[i];
  }
  energy = energy / (bin_max - bin_min);
  energy = fmin(energy, 1);
  return energy;
}

static double get_peak_energy(double peak_freq, double freq) {
  if (freq > peak_freq) return freq;
  return fmax(freq, peak_freq * DECAY_RATE);
}

static int bar_fill(double value, int width) {
  if (value < 0) value = 0;
  if (value > 1) value = 1;
  return (int)lround(value * width);
}

static void make_band_bar(char *out, size_t out_size, double value, double peak, int width) {
  int filled = bar_fill(value / MAG_SCALE, width);
  int peak_pos = bar_fill(peak / MAG_SCALE, width) - 1;

  if (out_size < (size_t)(width + 1)) return;

  for (int i = 0; i < width; ++i) {
    out[i] = (i < filled) ? '#' : '.';
  }

  if (peak_pos >= 0 && peak_pos < width) {
    out[peak_pos] = '|';
  }
  out[width] = '\0';
}

static void visualize(Data* data) {
    if (data->rate == 0) return;
    double freq_resolution = (double)data->rate / FFT_FRAMES;
    char bass_bar[BAR_WIDTH + 1];
    char mid_bar[BAR_WIDTH + 1];
    char treble_bar[BAR_WIDTH + 1];

    // Apply Hann Window.
    for(int j = 0; j < FFT_FRAMES; ++j) {
      data->timebuf[j] *= data->window[j];
    }
    fftw_execute(data->plan);
    compute_mag(data->frequency, data->mag, FFT_FRAMES / 2 + 1);

    // Extract raw frequency bands.
    double bass_raw = get_band_energy(data->mag, 20, 250, freq_resolution);
    double mid_raw = get_band_energy(data->mag, 250, 2000, freq_resolution);
    double treble_raw = get_band_energy(data->mag, 2000, 8000, freq_resolution);

    data->peak_bass = get_peak_energy(data->peak_bass, bass_raw);
    data->peak_mid = get_peak_energy(data->peak_mid, mid_raw);
    data->peak_treble = get_peak_energy(data->peak_treble, treble_raw);

    make_band_bar(bass_bar, sizeof(bass_bar), bass_raw, data->peak_bass, BAR_WIDTH);
    make_band_bar(mid_bar, sizeof(mid_bar), mid_raw, data->peak_mid, BAR_WIDTH);
    make_band_bar(treble_bar, sizeof(treble_bar), treble_raw, data->peak_treble, BAR_WIDTH);

    printf("\r\033[2K");
    printf("Bass   [%s] r:%0.3f p:%0.3f  ", bass_bar, bass_raw, data->peak_bass);
    printf("Mid   [%s] r:%0.3f p:%0.3f  ", mid_bar, mid_raw, data->peak_mid);
    printf("Treble [%s] r:%0.3f p:%0.3f", treble_bar, treble_raw, data->peak_treble);
    fflush(stdout);
}

static void on_process(void *userdata) {
  Data *data = userdata;
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

  n_channels = data->channels;
  n_samples = buf->datas[0].chunk->size / sizeof(float);

  // TODO: Only process 1 channel only.
  for (n = 0, i = 0; n < n_samples; n += n_channels, i++) {
    data->timebuf[data->time_index++] = (double)samples[n];
    if (data->time_index == FFT_FRAMES) {
      data->time_index = 0;
      visualize(data);
    }
  }

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
    fprintf(stderr, "unexpected format: expected F32_LE; got %s\n",
            spa_debug_type_find_name(spa_type_audio_format,
                                     data->format.info.raw.format));
    return;
  }

  data->rate = data->format.info.raw.rate;
  data->channels = data->format.info.raw.channels;
  printf("got audio format:\n");
  printf("  format: %d (%s)\n", data->format.info.raw.format,
         spa_debug_type_find_name(spa_type_audio_format,
                                  data->format.info.raw.format));
  printf("  capturing rate: %dx%d\n", data->rate,
         data->channels);
  fflush(stdout);
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
  printf("\n");
  return 0;
}
