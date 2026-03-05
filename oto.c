#include <curses.h>
#include <fftw3.h>
#include <locale.h>
#include <math.h>
#include <pipewire/pipewire.h>
#include <spa/debug/types.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/audio/type-info.h>
#include <string.h>
#include <unistd.h>

#define FFT_FRAMES 3072
#define DECAY_RATE 0.90

#define DISPLAY_FRAMES 80
#define WIDTH 100

struct display_data {
  int index;
  int y, x;
  double *bass;
  double *peak_bass;
  double bass_;
  double peak_bass_;
  double scale;
};
typedef struct display_data DisplayData;

struct data {
  struct pw_main_loop *loop;
  struct pw_stream *stream;
  struct spa_audio_info format;

  uint32_t channels;
  uint32_t rate;

  short low_f;
  short high_f;

  uint32_t time_index;
  double timebuf[FFT_FRAMES];
  double window[FFT_FRAMES];
  fftw_complex frequency[FFT_FRAMES / 2 + 1];
  fftw_plan plan;
  double mag[FFT_FRAMES / 2 + 1];

  DisplayData display_data;
};
typedef struct data Data;

static void populate_hann_window(double *window, uint32_t n) {
  uint32_t i;
  for (i = 0; i < n; ++i) {
    window[i] = 0.5 - 0.5 * cos((2 * M_PI * i) / (n - 1));
  }
}

static void compute_mag(fftw_complex frequency[], double *mag, uint32_t n) {
  uint32_t i;
  double re, im;
  const double scale = 1.0 / FFT_FRAMES;
  for (i = 0; i < n; ++i) {
    re = frequency[i][0];
    im = frequency[i][1];
    mag[i] = sqrt(re * re + im * im) * scale;
  }
}

static double get_band_energy(double *mag, double freq_min, double freq_max,
                              double freq_resolution) {
  uint32_t bin_min, bin_max;
  bin_min = freq_min / freq_resolution;
  bin_max = freq_max / freq_resolution;
  bin_max = fmin(bin_max, FFT_FRAMES / 2);

  if (bin_min >= bin_max)
    return 0;

  double energy = 0;
  for (uint32_t i = bin_min; i < bin_max; ++i) {
    energy += mag[i];
  }
  energy = energy / (bin_max - bin_min);
  energy = fmin(energy, 1);
  return energy;
}

static double get_peak_energy(double peak_freq, double freq) {
  if (freq > peak_freq)
    return freq;
  return fmax(freq, peak_freq * DECAY_RATE);
}

static double bar_fill(double value, int width) {
  if (value < 0)
    value = 0;
  if (value > 1)
    value = 1;
  return value * width;
}

// TODO: this is only done for bass. Change it later.
static void make_band_bar(DisplayData *display_data) {
  double value;
  int idx;
  for (int j = 0; j < display_data->x; j++) {
    idx = display_data->index - j;
    if (idx < 0) {
      idx = display_data->x + idx;
    }
    value = display_data->bass[idx];
    double d_filled = bar_fill(value / display_data->scale, WIDTH);
    int filled = d_filled;
    for (int i = 0; i < filled; ++i) {
      mvwprintw(stdscr, display_data->y - i, display_data->x - j, "█");
    }
    d_filled = d_filled - (double)filled;
    if (d_filled > 0.875) {
      mvwprintw(stdscr, display_data->y - filled, display_data->x - j, "▇");
    } else if (d_filled > 0.75) {
      mvwprintw(stdscr, display_data->y - filled, display_data->x - j, "▆");
    } else if (d_filled > 0.625) {
      mvwprintw(stdscr, display_data->y - filled, display_data->x - j, "▅");
    } else if (d_filled > 0.5) {
      mvwprintw(stdscr, display_data->y - filled, display_data->x - j, "▄");
    } else if (d_filled > 0.375) {
      mvwprintw(stdscr, display_data->y - filled, display_data->x - j, "▃");
    } else if (d_filled > 0.25) {
      mvwprintw(stdscr, display_data->y - filled, display_data->x - j, "▂");
    } else if (d_filled > 0.125) {
      mvwprintw(stdscr, display_data->y - filled, display_data->x - j, "▁");
    }
  }
  wnoutrefresh(stdscr);
}

static void visualize(Data *data) {
  erase();
  if (data->rate == 0)
    return;
  double freq_resolution = (double)data->rate / FFT_FRAMES;
  DisplayData *display_data = &data->display_data;
  display_data->index = (display_data->index + 1) % display_data->x;
  uint32_t index = display_data->index;
  double bass;
  double peak_bass;

  // Apply Hann Window.
  for (int j = 0; j < FFT_FRAMES; ++j) {
    data->timebuf[j] *= data->window[j];
  }
  fftw_execute(data->plan);
  compute_mag(data->frequency, data->mag, FFT_FRAMES / 2 + 1);

  // Extract raw frequency bands.
  bass = get_band_energy(data->mag, data->low_f, data->high_f, freq_resolution);

  peak_bass = get_peak_energy(display_data->peak_bass_, bass);
  // peak_mid = get_peak_energy(display_data->peak_mid_, mid);
  // peak_treble = get_peak_energy(display_data->peak_treble_, treble);

  // Set display data.
  display_data->bass[index] = (bass + display_data->bass_) / 2;
  display_data->bass_ = bass;
  display_data->peak_bass[index] = peak_bass;
  display_data->peak_bass_ = peak_bass;
  make_band_bar(display_data);
  doupdate();
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
    }
  }
  visualize(data);

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
  printf("  capturing rate: %dx%d\n", data->rate, data->channels);
  fflush(stdout);
}

static const struct pw_stream_events stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .param_changed = on_param_changed,
    .process = on_process,
};

int main(int argc, char *argv[]) {
  // Get arguments.
  short R, G, B;
  short low_f, high_f;
  double scale = 0.035;
  low_f = 20, high_f = 250;
  R = 875, G = 547, B = 449;
  int opt;
  while ((opt = getopt(argc, argv, "BMT")) != -1) {
    switch (opt) {
    case 'B':
      scale = 0.035;
      low_f = 20, high_f = 250;
      R = 16, G = 754, B = 883;
      break;
    case 'M':
      scale = 0.01;
      low_f = 250, high_f = 2000;
      R = 688, G = 766, B = 0;
      break;
    case 'T':
      scale = 0.004;
      low_f = 2000, high_f = 8000;
      R = 875, G = 547, B = 449;
      break;
    default:
      fprintf(stderr, "Usage %s [-B] [-M] [-T]\n", argv[0]);
      exit(0);
    }
  }
  // Init ncurses.
  setlocale(LC_ALL, "en_US.UTF-8");
  initscr();
  noecho();
  cbreak();

  noqiflush();
  keypad(stdscr, 1);
  curs_set(0);

  if (has_colors() == 0 && can_change_color() == 0) {
    endwin();
    exit(1);
  }

  // Init color pair for ncurses.
  use_default_colors();
  start_color();
  init_color(15, R, G, B);
  init_pair(1, 15, -1);
  attron(COLOR_PAIR(1));

  int y, x;
  getmaxyx(stdscr, y, x);

  // Init Pipewire.
  Data data;
  memset(&data, 0, sizeof(data));

  populate_hann_window(data.window, FFT_FRAMES);
  data.plan = fftw_plan_dft_r2c_1d(FFT_FRAMES, data.timebuf, data.frequency,
                                   FFTW_MEASURE);
  data.low_f = low_f;
  data.high_f = high_f;
  data.time_index = 0;
  data.display_data.scale = scale;
  data.display_data.index = 0;
  data.display_data.x = x;
  data.display_data.y = y;
  data.display_data.bass = malloc(x * sizeof(double));
  data.display_data.peak_bass = malloc(x * sizeof(double));
  memset(data.display_data.bass, 0, x * sizeof(double));
  memset(data.display_data.peak_bass, 0, x * sizeof(double));

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
  attroff(COLOR_PAIR(1));
  endwin();
  if (data.plan != NULL) {
    fftw_destroy_plan(data.plan);
  }
  pw_stream_destroy(data.stream);
  pw_main_loop_destroy(data.loop);
  pw_deinit();
  return 0;
}
