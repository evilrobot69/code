#include "pebble_os.h"
#include "pebble_app.h"
#include "pebble_fonts.h"

#include "common.h"
#include "hmac_sha2.h"
#include "http.h"
#include "mac_key.h"  // for kMacKey / kMacKeyLength
#include "settings.h"

// TODO(ariw): Need to have separate versions of this UUID for iOS / Android.
// This allows Android to have multiple HTTPebble apps.
#define MY_UUID HTTP_UUID
PBL_APP_INFO(
    MY_UUID, "Falldown", "Ari Wilson", 1, 0 /* App version */,
    RESOURCE_ID_IMAGE_ICON, APP_INFO_STANDARD_APP);

extern const char* kMacKey;
extern const int kMacKeyLength;

// Size of temporary buffers.
const int kBufferSize = 256;

const bool kDebug = false;
const int kTextSize = 14;
const int kTextLength = 12;

const int kWidth = 144;
const int kHeight = 168;
const int kStatusBarHeight = 16;

// How often to update game state.
const int kUpdateMs = 33;

// Player circle constants.
const int kCircleRadius = 4;
// Should be able to get across the screen in kAcrossScreenMs:
const int kAcrossScreenMs = 1000;
// kWidth / (kAcrossScreenMs / kUpdateMs)
const float kCircleXVelocity = 4.752;
// Falling speed of circle.
const float kCircleYVelocity = 1;

// Line constants.
const int kDistanceBetweenLines = 30;
const int kLineThickness = 3;
const int kMaxHoles = 2;
// TODO(ariw): Different size holes?
const int kLineSegments = 6;
const int kLineSegmentWidth = 24;  // kWidth / kLineSegments
// ceil((kHeight - kStatusBarHeight) / (kLineThickness + kDistanceBetweenLines))
const int kLineCount = 5;
// Lines move up one full screen size once every kDownScreenMs:
const int kDownScreenMs = 8000;
// -(kHeight - kStatusBarHeight) / (kDownScreenMs / kUpdateMs)
const float kInitialLineVelocity = -0.627;
// Every kVelocityIncreaseMs, multiply line velocity by kVelocityIncrease:
const int kVelocityIncreaseMs = 15000;
const float kVelocityIncrease = 1.05;

extern FalldownSettings settings;
extern bool in_menu;

Window game_window;

TextLayer text_layer;
char text[12 /* kTextLength */];
int score = 0;

// Player circle data and functions.
typedef struct {
  Layer layer;
  float x;
  float y;
} Circle;
Circle circle;
float circle_x_velocity = 0;

AccelData filter = {
  .x = 0,
  .y = 0,
  .z = 0
};

void circle_update_proc(Circle* circle, GContext* ctx) {
  // TODO(ariw): Use an animated circle here instead of this function.
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(
      ctx, GPoint(kCircleRadius, kCircleRadius), kCircleRadius - 1);
}

void circle_init(Layer* parent_layer, int x, int y, Circle* circle) {
  layer_init(&circle->layer, GRect(
        circle->x, circle->y, kCircleRadius * 2, kCircleRadius * 2));
  layer_set_update_proc(&circle->layer, (LayerUpdateProc)circle_update_proc);
  layer_add_child(parent_layer, &circle->layer);
  circle->x = x;
  circle->y = y;
}

// Lines data and functions.
typedef struct {
  Layer layer;
  float y;  // location of this line on the screen
  int holes[2 /* kMaxHoles */];  // which segments have holes
  int holes_size;
} Line;
typedef Line Lines[5 /* kLineCount */];
Lines lines;
int elapsed_time_ms = 0;
float lines_velocity = -0.627;  // kInitialLineVelocity

void line_update_proc(Line* line, GContext* ctx) {
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(0, 0, kWidth, kLineThickness), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, GColorBlack);
  for (int i = 0; i < line->holes_size; ++i) {
    graphics_fill_rect(
        ctx,
        GRect(line->holes[i] * kLineSegmentWidth, 0, kLineSegmentWidth,
              kLineThickness),
        0,
        GCornerNone);
  }
}

void line_generate(int y, Line* line) {
  line->y = y;
  line->holes_size = rand() % kMaxHoles + 1;
  common_shuffle_integers(line->holes_size, (int*)line->holes);
  common_insertion_sort((int*)line->holes, line->holes_size);
}

void line_init(Layer* parent_layer, int y, Line* line) {
  line_generate(y, line);
  layer_init(&line->layer, GRect(0, line->y, kWidth, kLineThickness));
  layer_set_update_proc(&line->layer, (LayerUpdateProc)line_update_proc);
  layer_add_child(parent_layer, &line->layer);
}

void lines_init(Layer* parent_layer, Lines* lines) {
  for (int i = 0; i < kLineCount; ++i) {
    line_init(
        parent_layer, (kDistanceBetweenLines + kLineThickness) * (i + 2),
        &((*lines)[i]));
  }
}

// Whether a circle intersects any line during the next move and whether this is
// due to its x velocity or y velocity (considered independently).
// relative_{x,y}_velocity represents the per update pixel {x,y} velocity
// between the lines and the circle.
void lines_circle_intersect(
    float relative_x_velocity, float relative_y_velocity, Lines* lines,
    Circle* circle, bool* intersects_x, bool* intersects_y) {
  *intersects_x = false;
  *intersects_y = false;
  for (int i = 0; i < kLineCount; ++i) {
    Line* line = &((*lines)[i]);
    int y = line->y;
    // Determine whether the circle is passing through a line. This happens only
    // if before the move, the top of the circle is either in or above the line
    // and, after the move, the bottom of the circle is either in or below the
    // line.
    if (circle->y < y + kLineThickness &&
        circle->y + kCircleRadius * 2 + relative_y_velocity >= y) {
      *intersects_y = true;
      // The circle is passing through a line. We need to check if our circle
      // fits through any holes in that line. Since holes are stored in
      // ascending order, we can simultaneously establish the boundaries of
      // larger holes and see if the circle fits through any of them.
      for (int j = 0; j < line->holes_size; ++j) {
        int hole_start_x = line->holes[j] * kLineSegmentWidth;
        while (j < line->holes_size - 1 &&
               line->holes[j] + 1 == line->holes[j + 1]) {
          ++j;
        }
        int hole_end_x = (line->holes[j] + 1) * kLineSegmentWidth;
        if (circle->x >= hole_start_x &&
            circle->x + kCircleRadius * 2 < hole_end_x) {
          if (circle->x + relative_x_velocity < hole_start_x ||
              circle->x + kCircleRadius * 2 + relative_x_velocity >=
                  hole_end_x) {
            *intersects_x = true;
          }
          *intersects_y = false;
        }
      }
      return;  // Circle can't be in more than one line since lines don't touch.
    }
  }
}

// Input handlers.
void up_single_click_handler(ClickRecognizerRef recognizer, Window *window) {
  (void)recognizer;
  (void)window;
  circle_x_velocity = kCircleXVelocity;
}

void down_single_click_handler(ClickRecognizerRef recognizer, Window *window) {
  (void)recognizer;
  (void)window;
  circle_x_velocity = -kCircleXVelocity;
}

void select_single_click_handler(ClickRecognizerRef recognizer, Window *window) {
  (void)recognizer;
  (void)window;
  display_settings();
}

void click_config_provider(ClickConfig **config, Window *window) {
  (void)window;

  config[BUTTON_ID_UP]->click.handler = (ClickHandler)up_single_click_handler;
  config[BUTTON_ID_UP]->click.repeat_interval_ms = kUpdateMs;

  config[BUTTON_ID_DOWN]->click.handler = (ClickHandler)down_single_click_handler;
  config[BUTTON_ID_DOWN]->click.repeat_interval_ms = kUpdateMs;

  // TODO(ariw): This pauses once and never lets you unpause. Fix!
  config[BUTTON_ID_SELECT]->click.handler = (ClickHandler)select_single_click_handler;
  // We want to not do anything upon button holds so configure really long
  // repeat interval.
  config[BUTTON_ID_SELECT]->click.repeat_interval_ms = 65535;
}


AccelData average_accel(const PebbleAccelEvent* accel) {
  AccelData average = { 0, 0, 0 };
  AccelData accel_data[32];
  accel_service_read_data(accel_data, event->accel.count);

  for (int i = 0; i < accel->count; i++) {
    average.x += accel_data[i].x;
    average.y += accel_data[i].y;
    average.z += accel_data[i].z;
  }
  average.x /= accel->count;
  average.y /= accel->count;
  average.z /= accel->count;

  return average;
}

AccelData filter_accel(const AccelData& accel, AccelData* filter) {
  AccelData filtered_accel;
  const float kFilteringFactor = 0.1;
  filter->x = accel.x * kFilteringFactor + filter->x * (1 - kFilteringFactor);
  filtered_accel.x = accel.x - filter->x;
  filter->y = accel.y * kFilteringFactor + filter->y * (1 - kFilteringFactor);
  filtered_accel.y = accel.y - filter->y;
  filter->z = accel.z * kFilteringFactor + filter->z * (1 - kFilteringFactor);
  filtered_accel.z = accel.z - filter->z;
  return filtered_accel;
}

void handle_accel(PebbleEvent* event) {
  if (!settings->accelerometer_control) return;

  AccelData accel = filter_accel(average_accel(&event->accel), &filter);
  // TODO(ariw): HARD WORK GOES HERE (of determining velocity using only noisy
  // acceleration).
}

void get_mac(const char* game, int score, const char* nonce, char* mac) {
  char message[kBufferSize];
  int message_length;
  if (nonce) {
    message_length = snprintf(
        message, kBufferSize, "%s%d%s", game, score, nonce);
  } else {
    message_length = snprintf(message, kBufferSize, "%s%d", game, score);
  }
  char binary_mac[SHA256_DIGEST_SIZE];
  hmac_sha256(
      (unsigned char*)kMacKey, kMacKeyLength, (unsigned char*)message,
      message_length, (unsigned char*)binary_mac, SHA256_DIGEST_SIZE);
  // Convert binary MAC to hexdigest.
  for (int i = 0; i < SHA256_DIGEST_SIZE; ++i) {
    snprintf(mac + i * 2, 3, "%02x", binary_mac[i]);
  }
}

void http_success(
    int32_t cookie, int http_status, DictionaryIterator* received,
    void* context) {
  // Are we in a nonce callback or a score callback?
  if (cookie < 0) return;
  int score = cookie;
  char* nonce = dict_find(received, 1)->value->cstring;
  static const char* kGameName = "Falldown";
  char mac[SHA256_DIGEST_SIZE * 2 + 1];  // sha256 in hex and terminating \0.
  get_mac(kGameName, score, nonce, (char*)mac);
  DictionaryIterator* body;
  http_out_get(
      "http://pebblescores.appspot.com/submit", -1, &body);
  dict_write_cstring(body, 1, kGameName);
  dict_write_int32(body, 2, (int32_t)score);
  dict_write_cstring(body, 3, mac);
  dict_write_cstring(body, 4, nonce);
  http_out_send();
}

void send_score(int score) {
  DictionaryIterator* body;
  http_out_get(
      "http://pebblescores.appspot.com/nonce", score, &body);
  http_out_send();
}

// TODO(ariw): Merge this with the circle/line init functions.
void reset() {
  // Reset the score.
  score = 0;

  // Reset player circle.
  circle.x = kWidth / 2 - kCircleRadius;
  circle.y = 0;
  circle_x_velocity = 0;

  // Reset the lines.
  for (int i = 0; i < kLineCount; ++i) {
    line_generate(
        (kDistanceBetweenLines + kLineThickness) * (i + 2), &lines[i]);
  }

  // Reset our speed.
  elapsed_time_ms = 0;
  lines_velocity = kInitialLineVelocity;
}

void handle_init(AppContextRef ctx) {
  (void)ctx;
  srand(time(NULL));

  window_init(&game_window, "Falldown");
  window_set_background_color(&game_window, GColorBlack);
  window_stack_push(&game_window, true /* Animated */);

  Layer* root_layer = window_get_root_layer(&game_window);

  // Initialize HTTPebble.
  http_set_app_id(532013811);
  HTTPCallbacks http_callbacks = {
    .success = http_success,
  };
  http_register_callbacks(http_callbacks, (void*)NULL);

  // Initialize the lines to fall down.
  lines_init(root_layer, &lines);

  // Initialize the score.
  text_layer_init(&text_layer, GRect(0, 0, kWidth, kTextSize));
  text_layer_set_text_alignment(&text_layer, GTextAlignmentRight);
  text_layer_set_background_color(&text_layer, GColorClear);
  text_layer_set_text_color(&text_layer, GColorWhite);
  layer_add_child(root_layer, (Layer*)&text_layer);

  // Initialize the player circle.
  circle_init(root_layer, kWidth / 2 - kCircleRadius,  0, &circle);

  // Attach our desired button functionality
  window_set_click_config_provider(
      &game_window, (ClickConfigProvider)click_config_provider);

  // Attach our desired acceleration provider.
  AccelServiceSetting accel_settings = {
    .sampling_rate = ACCEL_SAMPLING_50HZ,
    // Try to update approximately once every kUpdateMs.
    .samples_per_update = 2,
  }
  accel_service_update_settings(&accel_settings);
  app_event_service_subscribe(ctx, PEBBLE_ACCEL_EVENT, &handle_accel);

  init_settings();

  // Start updating the game.
  app_timer_send_event(ctx, kUpdateMs, 0);
}

void handle_timer(AppContextRef ctx, AppTimerHandle handle, uint32_t cookie) {
  (void)ctx;

  // Check to see if game is over yet.
  if (circle.y < 0) {
    send_score(score);
    reset();
    // Don't update the screen for a bit to let the user see their score after
    // a game over.
    app_timer_send_event(ctx, 3000, 0);
    return;
  }
  app_timer_send_event(ctx, kUpdateMs, 0);

  if (in_menu) return;

  // Update the text.
  if (!kDebug) {
    snprintf(text, kTextLength, "%d", score);
  }
  text_layer_set_text(&text_layer, text);

  // Update the player circle.
  bool intersects_x = false, intersects_y = false;
  lines_circle_intersect(
      circle_x_velocity, kCircleYVelocity - lines_velocity, &lines,
      &circle, &intersects_x, &intersects_y);
  if (!intersects_x &&
      circle.x + circle_x_velocity >= 0 &&
      circle.x + kCircleRadius * 2 + circle_x_velocity < kWidth) {
    circle.x += circle_x_velocity;
  }
  circle_x_velocity = 0;
  if (!intersects_y &&
      circle.y + kCircleRadius * 2 + kCircleYVelocity <=
          kHeight - kStatusBarHeight) {
    // Fall down!
    circle.y += kCircleYVelocity;
  }
  if (intersects_y) {
    // Can't fall down yet, move up with the line.
    circle.y += lines_velocity;
  }
  layer_set_frame(&circle.layer,
                  GRect((int)circle.x, (int)circle.y, kCircleRadius * 2,
                        kCircleRadius * 2));

  // Update the lines as they move upward.
  for (int i = 0; i < kLineCount; ++i) {
    lines[i].y += lines_velocity;
    if (lines[i].y < 0) {
      line_generate(
          lines[common_mod(i - 1, kLineCount)].y + kDistanceBetweenLines +
              kLineThickness,
          &lines[i]);
      score += 10;
    }
    layer_set_frame(&lines[i].layer,
                    GRect(0, (int)lines[i].y, kWidth, kLineThickness));
  }

  // Increase our speed sometimes.
  // TODO(ariw): Update this by actual elapsed number of ms so game time matches
  // real time.
  elapsed_time_ms += kUpdateMs;
  if (elapsed_time_ms % kVelocityIncreaseMs < kUpdateMs) {
    lines_velocity *= kVelocityIncrease;
  }
}

void pbl_main(void *params) {
  PebbleAppHandlers handlers = {
    .init_handler = &handle_init,
    .timer_handler = &handle_timer,
    .messaging_info = {
      .buffer_sizes = {
        // TODO(ariw): Are these too big?
        .inbound = kBufferSize,
        .outbound = kBufferSize,
      }
    },
  };
  app_event_loop(params, &handlers);
}
