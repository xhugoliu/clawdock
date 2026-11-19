#include <assert.h>
#include <stdio.h>
#include "app_ui_theme.h"
#include "sessions_clawd.h"

static sessions_clawd_t clawd;
static codex_snapshot_t snapshot = { .available = true };
static int64_t now_ms = 1000;

static void publish(void)
{
    snapshot.received_ms = now_ms;
    sessions_clawd_update(&clawd, &snapshot, false, 0, now_ms);
    now_ms += 50;
}

static void update(int elapsed_ms)
{
    now_ms += elapsed_ms;
    sessions_clawd_update(&clawd, &snapshot, false, 0, now_ms);
}

static void row(unsigned index, unsigned id, uint8_t status)
{
    snapshot.rows[index].id[0] = id;
    snapshot.rows[index].session.state = status;
}

static clawd_pose_t assert_action(unsigned mask, int64_t previous_start)
{
    assert(clawd.view.pose_started_ms > previous_start);
    assert(mask & (1U << clawd.view.pose));
    assert(clawd.view.pose == clawd.actions.recent[0] && clawd.view.pose != clawd.actions.recent[1]);
    return clawd.view.pose;
}

static void test_patrol(void)
{
    sessions_clawd_t patrol = {0};
    codex_snapshot_t active = { .available = true, .received_ms = 1000, .count = 1,
        .rows = {{ .id = {9}, .session = { .state = CODEX_RUNNING } }} };
    sessions_clawd_update(&patrol, &active, false, 0, 1000);
    assert(patrol.view.pose == CLAWD_WALKING);
    const int left = UI_LEFT_MARGIN + 60, right = UI_WIDTH - 60;
    assert(patrol.view.x == left);
    int64_t first_walk = patrol.patrol_ends_ms - patrol.view.pose_started_ms;
    assert(first_walk >= 1800 && first_walk <= 3600);
    int64_t first_breath = 0;
    bool varied_walk = false, varied_breath = false, paused_at_edge = false, turned_midway = false;

    for (int64_t now = 1050; now < 241000; now += 50) {
        clawd_view_t before = patrol.view;
        sessions_clawd_update(&patrol, &active, false, 0, now);
        if (patrol.view.pose == CLAWD_IDLE) {
            if (before.pose == CLAWD_IDLE) assert(patrol.view.x == before.x);
            else {
                int64_t breathing = patrol.patrol_ends_ms - now;
                assert(breathing >= 2000 && breathing <= 3200);
                if (!first_breath) first_breath = breathing;
                varied_breath |= breathing != first_breath;
                paused_at_edge |= patrol.view.x == left || patrol.view.x == right;
            }
        } else if (before.pose == CLAWD_IDLE) {
            assert(patrol.view.pose == CLAWD_WALKING);
            int64_t walking = patrol.patrol_ends_ms - now;
            assert(walking >= 1800 && walking <= 3600);
            varied_walk |= walking != first_walk;
            turned_midway |= before.x > left && before.x < right && before.face_left != patrol.view.face_left;
        }
    }
    assert(varied_walk && varied_breath && paused_at_edge && turned_midway);
}

static void test_pet(void)
{
    sessions_clawd_t pet = {0};
    codex_snapshot_t sessions = { .available = true, .received_ms = 1000 };
    sessions_clawd_update(&pet, &sessions, false, 0, 1000);
    assert(pet.view.pose == CLAWD_WALKING);
    int x = pet.view.x;

    unsigned seen = 0;
    for (int64_t now = 1050; now < 3450; now += 50) {
        clawd_pose_t last = pet.actions.recent[0], previous = pet.actions.recent[1];
        sessions_clawd_update(&pet, &sessions, true, 0, now);
        assert(pet.view.pose_started_ms == now && pet.view.x == x);
        assert(pet.view.pose != last && pet.view.pose != previous);
        seen |= 1U << pet.view.pose;
    }
    assert(seen == CLAWD_FUN_ACTIONS);

    sessions.count = 1;
    sessions.received_ms = 3500;
    sessions.rows[0] = (codex_row_t){ .id = {2}, .session = { .state = CODEX_RUNNING } };
    sessions_clawd_update(&pet, &sessions, true, 0, 3500);
    assert(pet.view.pose_started_ms == 3500 && pet.pending == SESSIONS_CLAWD_STARTED);
    int64_t end = pet.action_ends_ms;
    clawd_pose_t last = pet.actions.recent[0], previous = pet.actions.recent[1];
    sessions_clawd_update(&pet, &sessions, false, 0, end);
    const unsigned start_actions = (1U << CLAWD_EUREKA) | (1U << CLAWD_GROOVING) | (1U << CLAWD_JUGGLING);
    assert(pet.view.pose_started_ms == end && pet.pending == SESSIONS_CLAWD_NONE);
    assert((start_actions & (1U << pet.view.pose)) && pet.view.pose != last && pet.view.pose != previous);
}

static void test_waiting_for_mac(void)
{
    sessions_clawd_t pet = {0};
    const int center = UI_WIDTH / 2;
    sessions_clawd_update(&pet, NULL, false, 0, 1000);
    assert(pet.view.pose == CLAWD_WALKING && pet.view.x < center && !pet.view.face_left);
    for (int64_t now = 1050; now <= 7000; now += 50) {
        sessions_clawd_update(&pet, NULL, false, 0, now);
        assert(pet.view.x <= center);
    }
    assert(pet.view.pose == CLAWD_SLEEPING && pet.view.x == center);

    sessions_clawd_update(&pet, NULL, false, 1000, 7100);
    assert(pet.view.pose == CLAWD_WALKING && pet.view.x > center && !pet.view.face_left);
    sessions_clawd_update(&pet, NULL, false, 0, 7150);
    assert(pet.view.pose == CLAWD_WALKING && pet.view.x > center && pet.view.face_left);
    for (int64_t now = 7200; now <= 7500; now += 50) {
        sessions_clawd_update(&pet, NULL, false, 0, now);
        assert(pet.view.x >= center);
    }
    assert(pet.view.pose == CLAWD_SLEEPING && pet.view.x == center);

    sessions_clawd_update(&pet, NULL, true, 0, 7550);
    assert(CLAWD_FUN_ACTIONS & (1U << pet.view.pose));
    sessions_clawd_pause(&pet);
    assert(!pet.last_update_ms && !pet.action_ends_ms);
}

static void test_manual_walk(void)
{
    sessions_clawd_t pet = {0};
    codex_snapshot_t sessions = { .available = true, .received_ms = 1000 };
    sessions_clawd_update(&pet, &sessions, false, 139, 1000);
    assert(pet.view.pose == CLAWD_WALKING && pet.view.x == UI_LEFT_MARGIN + 60);
    int left = pet.view.x;
    sessions_clawd_update(&pet, &sessions, false, 1000, 1100);
    assert(pet.view.pose == CLAWD_WALKING && pet.view.x == left + 15 && !pet.view.face_left);
    sessions_clawd_update(&pet, &sessions, true, -500, 1200);
    assert(pet.view.pose == CLAWD_WALKING && pet.view.x == left + 7 && pet.view.face_left);
    assert(pet.pet_pending && pet.actions.recent[0] == CLAWD_IDLE);
    sessions_clawd_update(&pet, &sessions, false, -500, 1250);
    assert(pet.view.pose == CLAWD_WALKING && pet.pet_pending);
    sessions_clawd_update(&pet, &sessions, false, 0, 1300);
    assert((CLAWD_FUN_ACTIONS & (1U << pet.view.pose)) && !pet.pet_pending);

    sessions.count = 1;
    sessions.received_ms = 1350;
    sessions.rows[0] = (codex_row_t){ .id = {2}, .session = { .state = CODEX_RUNNING } };
    sessions_clawd_update(&pet, &sessions, false, 1000, 1350);
    assert(pet.view.pose == CLAWD_WALKING && pet.pending == SESSIONS_CLAWD_STARTED);
    sessions_clawd_update(&pet, &sessions, false, 0, 1400);
    assert((CLAWD_FUN_ACTIONS & (1U << pet.view.pose)) && pet.pending == SESSIONS_CLAWD_NONE);
    int64_t end = pet.action_ends_ms;
    sessions_clawd_update(&pet, &sessions, false, 0, end);
    assert(pet.view.pose == CLAWD_WALKING && pet.patrol_ends_ms > end);
}

int main(void)
{
    test_patrol();
    test_pet();
    test_waiting_for_mac();
    test_manual_walk();
    const unsigned started = (1U << CLAWD_GROOVING) | (1U << CLAWD_JUGGLING) | (1U << CLAWD_EUREKA);
    const unsigned done = (1U << CLAWD_HAPPY) | (1U << CLAWD_COFFEE) | (1U << CLAWD_SNACK);
    const unsigned failed = (1U << CLAWD_DIZZY) | (1U << CLAWD_HAT) | (1U << CLAWD_JUGGLING);
    const unsigned cancelled = (1U << CLAWD_HAT) | (1U << CLAWD_GROOVING) | (1U << CLAWD_SNACK);

    publish();
    const int center = UI_WIDTH / 2;
    for (unsigned i = 0; i < 120 && clawd.view.pose != CLAWD_SLEEPING; ++i) update(50);
    assert(clawd.view.pose == CLAWD_SLEEPING && clawd.view.x == center);
    snapshot.count = 2;
    row(0, 1, CODEX_RUNNING);
    row(1, 2, CODEX_RUNNING);
    publish();
    clawd_pose_t first = assert_action(started, 0);
    int64_t playing_since = clawd.view.pose_started_ms;
    publish();
    assert(clawd.view.pose_started_ms == playing_since && clawd.pending == SESSIONS_CLAWD_NONE);
    update(clawd_action_duration(first));
    assert(clawd.view.pose == CLAWD_WALKING && clawd.view.x == center);
    update(50);
    assert(clawd.view.x > center && !clawd.view.face_left);

    /* Reordering the same rows and introducing an already completed row do not make events. */
    snapshot.count = 3;
    row(0, 2, CODEX_RUNNING);
    row(1, 1, CODEX_RUNNING);
    row(2, 3, CODEX_COMPLETED);
    publish();
    assert(clawd.view.pose == CLAWD_WALKING);

    /* Competing changes yield one reaction per snapshot, with failure first. */
    row(0, 2, CODEX_FAILED);
    row(1, 1, CODEX_COMPLETED);
    publish();
    clawd_pose_t second = assert_action(failed, playing_since);
    assert(second != first);
    playing_since = clawd.view.pose_started_ms;

    /* A change during an action waits until that action ends. */
    row(1, 1, CODEX_RUNNING);
    publish();
    assert(clawd.view.pose == second && clawd.pending == SESSIONS_CLAWD_STARTED);
    row(1, 1, CODEX_COMPLETED);
    publish();
    assert(clawd.view.pose == second && clawd.pending == SESSIONS_CLAWD_COMPLETED);
    update(clawd_action_duration(second));
    clawd_pose_t third = assert_action(done, playing_since);
    assert(third != first && third != second);
    playing_since = clawd.view.pose_started_ms;

    /* Await a real state change even if a known ID briefly disappears. */
    snapshot.count = 0;
    publish();
    snapshot.count = 1;
    row(0, 1, CODEX_COMPLETED);
    publish();
    assert(clawd.view.pose_started_ms == playing_since && clawd.pending == SESSIONS_CLAWD_NONE);
    update(clawd_action_duration(third));
    for (unsigned i = 0; i < 120 && clawd.view.pose != CLAWD_SLEEPING; ++i) update(50);
    assert(clawd.view.pose == CLAWD_SLEEPING && clawd.view.x == center);

    /* The first snapshot after reopening seeds the session history. */
    sessions_clawd_pause(&clawd);
    row(0, 4, CODEX_RUNNING);
    publish();
    assert(clawd.view.pose == CLAWD_WALKING && clawd.view.x == center);
    update(50);
    assert(clawd.view.x < center && clawd.view.face_left);
    row(0, 4, CODEX_CANCELLED);
    now_ms += 50;
    publish();
    assert_action(cancelled, playing_since);

    puts("Sessions Clawd tests passed");
}
