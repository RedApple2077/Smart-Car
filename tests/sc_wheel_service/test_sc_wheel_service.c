#include "sc_wheel_service.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression)                                                     \
  do                                                                          \
  {                                                                           \
    if (!(expression))                                                        \
    {                                                                         \
      fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__,     \
              #expression);                                                   \
      return 1;                                                               \
    }                                                                         \
  } while (0)

static sc_wheel_calibration_t default_calibration(void)
{
  sc_wheel_calibration_t calibration = {
    .left_encoder_sign = -1,
    .right_encoder_sign = 1,
    .left_counts_per_rev = 1040u,
    .right_counts_per_rev = 1040u,
    .left_wheel_diameter_um = 48000,
    .right_wheel_diameter_um = 48000
  };
  return calibration;
}

static sc_encoder_snapshot_t make_snapshot(
  uint32_t time_ms, uint32_t interval_ms, uint32_t sequence,
  int64_t left_total, int64_t right_total)
{
  sc_encoder_snapshot_t snapshot = {0};

  snapshot.sample_time_ms = time_ms;
  snapshot.interval_ms = interval_ms;
  snapshot.sequence = sequence;
  snapshot.left.total_count = left_total;
  snapshot.right.total_count = right_total;
  snapshot.left.valid = 1u;
  snapshot.right.valid = 1u;
  snapshot.valid = 1u;
  return snapshot;
}

static int push_snapshot(sc_wheel_service_t *service,
                         const sc_wheel_calibration_t *calibration,
                         const sc_encoder_snapshot_t *snapshot,
                         uint32_t now_ms, sc_wheel_feedback_t *feedback)
{
  return (int)sc_wheel_service_update(
    service, snapshot, calibration, now_ms, feedback);
}

static int test_precision_sign_and_five_frame_window(void)
{
  sc_wheel_calibration_t calibration = default_calibration();
  sc_wheel_service_t service;
  sc_wheel_feedback_t feedback;
  sc_encoder_snapshot_t snapshot;
  const int64_t left_totals[] = {0, -5, -9, -15, -18, -24, -31, -36};
  const int64_t right_totals[] = {0, 2, 4, 7, 9, 12, 15, 17};
  const uint32_t times[] = {
    10u, 20u, 30u, 40u, 50u, 60u, 70u, 80u
  };
  size_t index;

  CHECK(sc_wheel_service_init(&service, &calibration) ==
        SC_WHEEL_SERVICE_OK);

  for (index = 0u; index < 7u; index++)
  {
    sc_wheel_service_status_t status;

    snapshot = make_snapshot(
      times[index], 10u, (uint32_t)(index + 1u),
      left_totals[index], right_totals[index]);
    status = (sc_wheel_service_status_t)push_snapshot(
      &service, &calibration, &snapshot, times[index], &feedback);
    CHECK(status == ((index >= 5u) ? SC_WHEEL_SERVICE_OK :
                                    SC_WHEEL_SERVICE_WARMUP));
    if (index < 5u)
    {
      CHECK(feedback.valid == 0u);
    }
  }

  CHECK(feedback.valid == 1u);
  CHECK(feedback.left_valid == 1u);
  CHECK(feedback.right_valid == 1u);
  CHECK(feedback.left_window_delta_count == -26);
  CHECK(feedback.right_window_delta_count == 13);
  CHECK(feedback.left_window_span_ms == 50u);
  CHECK(feedback.right_window_span_ms == 50u);
  CHECK(feedback.left_speed_mm_s == 75);
  CHECK(feedback.right_speed_mm_s == 38);
  CHECK(feedback.sample_time_ms == 70u);
  CHECK(feedback.sequence == 7u);

  snapshot = make_snapshot(80u, 10u, 8u, -36, 17);
  CHECK(push_snapshot(
          &service, &calibration, &snapshot, 80u, &feedback) ==
        SC_WHEEL_SERVICE_OK);
  CHECK(feedback.left_window_delta_count == -27);
  CHECK(feedback.right_window_delta_count == 13);
  CHECK(feedback.left_speed_mm_s == 78);
  CHECK(feedback.right_speed_mm_s == 38);
  CHECK(feedback.sample_time_ms == 80u);
  CHECK(feedback.sequence == 8u);
  return 0;
}

static int test_sliding_window_actual_time(void)
{
  sc_wheel_calibration_t calibration = default_calibration();
  sc_wheel_service_t service;
  sc_wheel_feedback_t feedback;
  sc_encoder_snapshot_t snapshot;
  const int64_t left_totals[] = {0, -1, -2, -3, -13, -34, -50};
  const int64_t right_totals[] = {0, 1, 2, 3, 8, 17, 25};
  const uint32_t times[] = {10u, 12u, 14u, 16u, 30u, 60u, 90u};
  const uint32_t intervals[] = {10u, 2u, 2u, 2u, 14u, 30u, 30u};
  size_t index;

  CHECK(sc_wheel_service_init(&service, &calibration) ==
        SC_WHEEL_SERVICE_OK);
  for (index = 0u; index < 6u; index++)
  {
    snapshot = make_snapshot(
      times[index], intervals[index], (uint32_t)(index + 1u),
      left_totals[index], right_totals[index]);
    CHECK(push_snapshot(
            &service, &calibration, &snapshot, times[index],
            &feedback) ==
          ((index == 5u) ? SC_WHEEL_SERVICE_OK :
                           SC_WHEEL_SERVICE_WARMUP));
  }
  CHECK(feedback.left_speed_mm_s == 99);
  CHECK(feedback.right_speed_mm_s == 49);
  CHECK(feedback.left_window_span_ms == 50u);

  snapshot = make_snapshot(90u, 30u, 7u, -50, 25);
  CHECK(push_snapshot(
          &service, &calibration, &snapshot, 90u, &feedback) ==
        SC_WHEEL_SERVICE_OK);
  CHECK(feedback.left_window_delta_count == -49);
  CHECK(feedback.right_window_delta_count == 24);
  CHECK(feedback.left_window_span_ms == 78u);
  CHECK(feedback.right_window_span_ms == 78u);
  CHECK(feedback.left_speed_mm_s == 91);
  CHECK(feedback.right_speed_mm_s == 45);
  CHECK(feedback.sample_time_ms == 90u);
  return 0;
}

static int test_duplicate_sequence(void)
{
  sc_wheel_calibration_t calibration = default_calibration();
  sc_wheel_service_t service;
  sc_wheel_feedback_t feedback;
  sc_encoder_snapshot_t snapshot;

  CHECK(sc_wheel_service_init(&service, &calibration) ==
        SC_WHEEL_SERVICE_OK);
  snapshot = make_snapshot(10u, 10u, 12u, 0, 0);
  CHECK(push_snapshot(
          &service, &calibration, &snapshot, 10u, &feedback) ==
        SC_WHEEL_SERVICE_WARMUP);

  snapshot = make_snapshot(10u, 10u, 12u, -7, 3);
  CHECK(push_snapshot(
          &service, &calibration, &snapshot, 10u, &feedback) ==
        SC_WHEEL_SERVICE_ERR_DUPLICATE_SEQUENCE);
  CHECK(feedback.valid == 0u);
  CHECK(feedback.left_valid == 0u);
  CHECK(feedback.right_valid == 0u);
  return 0;
}

static int test_stale_snapshot(void)
{
  sc_wheel_calibration_t calibration = default_calibration();
  sc_wheel_service_t service;
  sc_wheel_feedback_t feedback;
  sc_encoder_snapshot_t snapshot;

  CHECK(sc_wheel_service_init(&service, &calibration) ==
        SC_WHEEL_SERVICE_OK);
  snapshot = make_snapshot(100u, 10u, 1u, 0, 0);
  CHECK(push_snapshot(
          &service, &calibration, &snapshot, 131u, &feedback) ==
        SC_WHEEL_SERVICE_ERR_STALE);
  CHECK(feedback.valid == 0u);

  snapshot = make_snapshot(200u, 20u, 2u, 0, 0);
  CHECK(push_snapshot(
          &service, &calibration, &snapshot, 231u, &feedback) ==
        SC_WHEEL_SERVICE_ERR_STALE);
  CHECK(feedback.valid == 0u);
  return 0;
}

static int test_time_wrap(void)
{
  sc_wheel_calibration_t calibration = default_calibration();
  sc_wheel_service_t service;
  sc_wheel_feedback_t feedback;
  sc_encoder_snapshot_t snapshot;
  const uint32_t times[] = {
    0xFFFFFFFAu, 0x00000004u, 0x0000000Eu,
    0x00000018u, 0x00000022u, 0x0000002Cu
  };
  const int64_t left_totals[] = {0, -7, -14, -20, -27, -34};
  const int64_t right_totals[] = {0, 3, 6, 10, 14, 17};
  size_t index;

  CHECK(sc_wheel_service_init(&service, &calibration) ==
        SC_WHEEL_SERVICE_OK);
  snapshot = make_snapshot(0xFFFFFFF0u, 0u, 1u, 0, 0);
  CHECK(push_snapshot(
          &service, &calibration, &snapshot, 0xFFFFFFF0u, &feedback) ==
        SC_WHEEL_SERVICE_ERR_RESET_FRAME);

  for (index = 0u; index < 6u; index++)
  {
    snapshot = make_snapshot(
      times[index], 10u, (uint32_t)(index + 2u),
      left_totals[index], right_totals[index]);
    CHECK(push_snapshot(
            &service, &calibration, &snapshot, times[index],
            &feedback) ==
          ((index == 5u) ? SC_WHEEL_SERVICE_OK :
                           SC_WHEEL_SERVICE_WARMUP));
  }
  CHECK(feedback.valid == 1u);
  CHECK(feedback.left_window_span_ms == 50u);
  CHECK(feedback.left_speed_mm_s == 99);
  CHECK(feedback.right_speed_mm_s == 49);
  return 0;
}

static int test_reset_frame(void)
{
  sc_wheel_calibration_t calibration = default_calibration();
  sc_wheel_service_t service;
  sc_wheel_feedback_t feedback;
  sc_encoder_snapshot_t snapshot;

  CHECK(sc_wheel_service_init(&service, &calibration) ==
        SC_WHEEL_SERVICE_OK);
  snapshot = make_snapshot(10u, 10u, 1u, 0, 0);
  CHECK(push_snapshot(
          &service, &calibration, &snapshot, 10u, &feedback) ==
        SC_WHEEL_SERVICE_WARMUP);
  snapshot = make_snapshot(20u, 10u, 2u, -7, 3);
  CHECK(push_snapshot(
          &service, &calibration, &snapshot, 20u, &feedback) ==
        SC_WHEEL_SERVICE_WARMUP);

  snapshot = make_snapshot(30u, 0u, 3u, 0, 0);
  CHECK(push_snapshot(
          &service, &calibration, &snapshot, 30u, &feedback) ==
        SC_WHEEL_SERVICE_ERR_RESET_FRAME);
  CHECK(feedback.valid == 0u);

  snapshot = make_snapshot(40u, 10u, 4u, 0, 0);
  CHECK(push_snapshot(
          &service, &calibration, &snapshot, 40u, &feedback) ==
        SC_WHEEL_SERVICE_WARMUP);
  CHECK(feedback.valid == 0u);
  CHECK(service.left.sample_count == 1u);
  CHECK(service.right.sample_count == 1u);
  return 0;
}

static int test_left_right_independent_validity(void)
{
  sc_wheel_calibration_t calibration = default_calibration();
  sc_wheel_service_t service;
  sc_wheel_feedback_t feedback;
  sc_encoder_snapshot_t snapshot;
  const int64_t left_totals[] = {0, -7, -14, -20, -27, -34};
  const uint32_t times[] = {10u, 20u, 30u, 40u, 50u, 60u};
  size_t index;

  CHECK(sc_wheel_service_init(&service, &calibration) ==
        SC_WHEEL_SERVICE_OK);
  for (index = 0u; index < 5u; index++)
  {
    snapshot = make_snapshot(
      times[index], 10u, (uint32_t)(index + 1u),
      left_totals[index], 0);
    CHECK(push_snapshot(
            &service, &calibration, &snapshot, times[index],
            &feedback) == SC_WHEEL_SERVICE_WARMUP);
  }

  snapshot = make_snapshot(60u, 10u, 6u, -34, 0);
  snapshot.left.valid = 1u;
  snapshot.right.valid = 0u;
  snapshot.right.lost_sync = 1u;
  snapshot.valid = 0u;
  CHECK(push_snapshot(
          &service, &calibration, &snapshot, 60u, &feedback) ==
        SC_WHEEL_SERVICE_ERR_LOST_SYNC);
  CHECK(feedback.left_valid == 1u);
  CHECK(feedback.right_valid == 0u);
  CHECK(feedback.valid == 0u);
  CHECK(feedback.left_speed_mm_s == 99);
  CHECK(feedback.right_speed_mm_s == 0);
  return 0;
}

static int test_arguments_and_calibration(void)
{
  sc_wheel_calibration_t calibration = default_calibration();
  sc_wheel_service_t service;
  sc_wheel_feedback_t feedback;
  sc_encoder_snapshot_t snapshot;

  CHECK(sc_wheel_service_init(NULL, &calibration) ==
        SC_WHEEL_SERVICE_ERR_ARGUMENT);
  calibration.left_encoder_sign = 0;
  CHECK(sc_wheel_service_init(&service, &calibration) ==
        SC_WHEEL_SERVICE_ERR_CALIBRATION);

  calibration = default_calibration();
  CHECK(sc_wheel_service_init(&service, &calibration) ==
        SC_WHEEL_SERVICE_OK);
  snapshot = make_snapshot(10u, 10u, 1u, 0, 0);
  CHECK(sc_wheel_service_update(
          NULL, &snapshot, &calibration, 10u, &feedback) ==
        SC_WHEEL_SERVICE_ERR_ARGUMENT);
  CHECK(sc_wheel_service_update(
          &service, NULL, &calibration, 10u, &feedback) ==
        SC_WHEEL_SERVICE_ERR_ARGUMENT);
  CHECK(sc_wheel_service_update(
          &service, &snapshot, NULL, 10u, &feedback) ==
        SC_WHEEL_SERVICE_ERR_ARGUMENT);
  CHECK(sc_wheel_service_update(
          &service, &snapshot, &calibration, 10u, NULL) ==
        SC_WHEEL_SERVICE_ERR_ARGUMENT);

  calibration.left_counts_per_rev = 0u;
  CHECK(sc_wheel_service_update(
          &service, &snapshot, &calibration, 10u, &feedback) ==
        SC_WHEEL_SERVICE_ERR_CALIBRATION);

  calibration = default_calibration();
  calibration.left_wheel_diameter_um = 48600;
  calibration.right_wheel_diameter_um = 48600;
  CHECK(sc_wheel_service_init(&service, &calibration) ==
        SC_WHEEL_SERVICE_OK);
  for (uint32_t index = 0u; index < 6u; index++)
  {
    snapshot = make_snapshot(
      10u * (index + 1u), 10u, index + 1u,
      -7 * (int64_t)(index + 1u),
      3 * (int64_t)(index + 1u));
    CHECK(push_snapshot(
            &service, &calibration, &snapshot,
            10u * (index + 1u), &feedback) ==
          ((index == 5u) ? SC_WHEEL_SERVICE_OK :
                           SC_WHEEL_SERVICE_WARMUP));
  }
  CHECK(feedback.left_speed_mm_s == 103);
  CHECK(feedback.right_speed_mm_s == 44);
  return 0;
}

typedef struct {
  const char *name;
  int (*run)(void);
} test_case_t;

static const test_case_t s_tests[] = {
  {"precision_sign_and_five_frame_window",
   test_precision_sign_and_five_frame_window},
  {"sliding_window_actual_time", test_sliding_window_actual_time},
  {"duplicate_sequence", test_duplicate_sequence},
  {"stale_snapshot", test_stale_snapshot},
  {"time_wrap", test_time_wrap},
  {"reset_frame", test_reset_frame},
  {"left_right_independent_validity",
   test_left_right_independent_validity},
  {"arguments_and_calibration", test_arguments_and_calibration}
};

int main(int argc, char **argv)
{
  size_t index;

  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <test-case>\n", argv[0]);
    return 2;
  }
  for (index = 0u; index < (sizeof(s_tests) / sizeof(s_tests[0])); index++)
  {
    if (strcmp(argv[1], s_tests[index].name) == 0)
    {
      return s_tests[index].run();
    }
  }
  fprintf(stderr, "unknown test case: %s\n", argv[1]);
  return 2;
}
