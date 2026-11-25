#include <assert.h>
#include <stdio.h>
#include "agents_screen.h"

#define MINUTE_MS (60LL * 1000)
#define NAP_DURATION_MS (10 * MINUTE_MS)

static bool update_screen(agents_screen_t *screen, agents_screen_mode_t mode, int64_t now_ms, bool input)
{
    const lab_touch_state_t touch = { .ready = true };
    return agents_screen_update(screen, mode, now_ms, input, true, 0, 0, &touch);
}

static void test_nap_deadline_and_input(void)
{
    agents_screen_t screen = {0};
    const int64_t first_nap = 60 * MINUTE_MS;
    const int64_t manual_wake = first_nap + NAP_DURATION_MS + MINUTE_MS;
    const int64_t return_to_agents = 201 * MINUTE_MS;
    assert(update_screen(&screen, AGENTS_SCREEN_WAITING_FOR_NAP, 0, false));
    assert(update_screen(&screen, AGENTS_SCREEN_WAITING_FOR_NAP, first_nap, false));
    assert(update_screen(&screen, AGENTS_SCREEN_NAPPING, first_nap, false));
    assert(update_screen(&screen, AGENTS_SCREEN_NAPPING, first_nap + NAP_DURATION_MS - 1, false));
    assert(!update_screen(&screen, AGENTS_SCREEN_NAPPING, first_nap + NAP_DURATION_MS, false));
    assert(update_screen(&screen, AGENTS_SCREEN_NAPPING, manual_wake, true));
    assert(update_screen(&screen, AGENTS_SCREEN_NAPPING, manual_wake + NAP_DURATION_MS - 1, false));
    assert(!update_screen(&screen, AGENTS_SCREEN_NAPPING, manual_wake + NAP_DURATION_MS, false));

    /* Leaving Agents or opening the picker clears its idle time. */
    assert(update_screen(&screen, AGENTS_SCREEN_KEEP_AWAKE, 122 * MINUTE_MS, false));
    assert(update_screen(&screen, AGENTS_SCREEN_KEEP_AWAKE, 200 * MINUTE_MS, false));
    assert(update_screen(&screen, AGENTS_SCREEN_NAPPING, return_to_agents, false));
    assert(update_screen(&screen, AGENTS_SCREEN_NAPPING, return_to_agents + NAP_DURATION_MS - 1, false));
}

static void test_session_sync_and_wake(void)
{
    agents_screen_t screen = {0};
    codex_summary_t sessions = {0};
    assert(update_screen(&screen, agents_screen_mode(&sessions, false, 1), 1, false));
    const int64_t nap_started = 6000;
    assert(update_screen(&screen, agents_screen_mode(&sessions, true, nap_started), nap_started, false));
    int64_t now = nap_started + MINUTE_MS;
    assert(update_screen(&screen, agents_screen_mode(&sessions, true, now), now, false));

    /* Availability changes during a nap share one deadline. */
    sessions.received_ms = now;
    assert(update_screen(&screen, agents_screen_mode(&sessions, true, now), now, false));
    now = nap_started + NAP_DURATION_MS - 1;
    assert(update_screen(&screen, agents_screen_mode(&sessions, true, now), now, false));
    ++now;
    sessions.sources_unavailable = true;
    assert(!update_screen(&screen, agents_screen_mode(&sessions, true, now), now, false));

    now += MINUTE_MS;
    sessions.received_ms = now;
    sessions.sources_unavailable = false;
    sessions.count = 1;
    sessions.states[0] = CODEX_COMPLETED;
    assert(agents_screen_mode(&sessions, false, now) == AGENTS_SCREEN_WAITING_FOR_NAP);
    assert(!update_screen(&screen, agents_screen_mode(&sessions, true, now), now, false));

    /* An active row wakes the panel, even when it is behind a finished row. */
    sessions.count = 2;
    sessions.states[1] = CODEX_WAITING_FOR_ANSWER;
    sessions.sources_unavailable = true;
    assert(!update_screen(&screen, agents_screen_mode(&sessions, true, now), now, false));
    sessions.sources_unavailable = false;
    assert(!update_screen(&screen, agents_screen_mode(&sessions, true, now + CODEX_TIMEOUT_MS + 1),
                          now + CODEX_TIMEOUT_MS + 1, false));
    now += MINUTE_MS;
    sessions.received_ms = now;
    assert(update_screen(&screen, agents_screen_mode(&sessions, true, now), now, false));
    assert(agents_screen_mode(&sessions, false, now + CODEX_TIMEOUT_MS) == AGENTS_SCREEN_KEEP_AWAKE);
    sessions.states[1] = CODEX_RUNNING;
    assert(agents_screen_mode(&sessions, false, now) == AGENTS_SCREEN_KEEP_AWAKE);

    /* The last running session ends, then Clawd walks back and starts napping. */
    now += MINUTE_MS;
    sessions.received_ms = now;
    sessions.states[1] = CODEX_COMPLETED;
    assert(update_screen(&screen, agents_screen_mode(&sessions, false, now), now, false));
    now += 4000;
    assert(update_screen(&screen, agents_screen_mode(&sessions, true, now), now, false));
    now += NAP_DURATION_MS - 1;
    sessions.received_ms = now;
    assert(update_screen(&screen, agents_screen_mode(&sessions, true, now), now, false));
    assert(!update_screen(&screen, agents_screen_mode(&sessions, true, now + 1), now + 1, false));
}

static void test_stick_movement(void)
{
    agents_screen_t screen = {0};
    const lab_touch_state_t touch = {0};
    const agents_screen_mode_t mode = AGENTS_SCREEN_NAPPING;
    const int64_t deadline = MINUTE_MS + NAP_DURATION_MS;
    assert(agents_screen_update(&screen, mode, MINUTE_MS, false, true, 500, 0, &touch));
    assert(!agents_screen_update(&screen, mode, deadline, false, true, 502, 0, &touch));
    assert(!agents_screen_update(&screen, mode, deadline + 1, false, false, 0, 0, &touch));
    assert(!agents_screen_update(&screen, mode, deadline + 2, false, true, 529, 0, &touch));
    assert(agents_screen_update(&screen, mode, deadline + 3, false, true, 530, 0, &touch));
    assert(agents_screen_update(&screen, mode, deadline + 3 + NAP_DURATION_MS, false, true, 0, 0, &touch));
}

static void test_touch_events(void)
{
    agents_screen_t screen = {0};
    const agents_screen_mode_t mode = AGENTS_SCREEN_NAPPING;
    const int64_t deadline = MINUTE_MS + NAP_DURATION_MS;
    lab_touch_state_t touch = {
        .ready = true, .reported = 2, .valid = 2,
        .points = {{ .x = 40, .y = 50, .id = 1 }, { .x = 60, .y = 70, .id = 2 }},
    };
    assert(agents_screen_update(&screen, mode, MINUTE_MS, false, true, 0, 0, &touch));
    lab_touch_point_t first = touch.points[0];
    touch.points[0] = touch.points[1];
    touch.points[1] = first;
    ++touch.points[1].x;
    assert(agents_screen_update(&screen, mode, deadline - 1, false, true, 0, 0, &touch));
    const lab_touch_state_t failed = {0};
    assert(!agents_screen_update(&screen, mode, deadline, false, true, 0, 0, &failed));
    assert(!agents_screen_update(&screen, mode, deadline + 1, false, true, 0, 0, &touch));
    touch.points[1].x += 2;
    assert(agents_screen_update(&screen, mode, deadline + 2, false, true, 0, 0, &touch));
    touch.reported = touch.valid = 0;
    assert(agents_screen_update(&screen, mode, deadline + 2 + NAP_DURATION_MS, false, true, 0, 0, &touch));
}

int main(void)
{
    test_nap_deadline_and_input();
    test_session_sync_and_wake();
    test_stick_movement();
    test_touch_events();
    puts("Agents screen tests passed");
}
