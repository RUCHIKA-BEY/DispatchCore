/*
 * test_compatibility.c — unit tests for the compatibility rules and the
 * "compatible AND available" eligibility filter (Phase 4).
 *
 * Uses the reference fleet from blueprint §10:
 *   R1 AMBULANCE@0  R2 AMBULANCE@6  R3 FIRE_UNIT@2
 *   R4 RESCUE_UNIT@4  R5 FIRE_UNIT@7  R6 AMBULANCE@8 (on the island)
 *
 * Build and run:   make test
 */
#include "compatibility.h"
#include "test_util.h"

#include <stddef.h>

#define FLEET_SIZE 6

static void build_fleet(Resource fleet[FLEET_SIZE])
{
    const ResourceType types[FLEET_SIZE] = {
        RES_AMBULANCE, RES_AMBULANCE, RES_FIRE_UNIT,
        RES_RESCUE_UNIT, RES_FIRE_UNIT, RES_AMBULANCE
    };
    const int locations[FLEET_SIZE] = {0, 6, 2, 4, 7, 8};
    for (int i = 0; i < FLEET_SIZE; i++) {
        Resource r = {i + 1, types[i], locations[i], RES_AVAILABLE, RESOURCE_NO_INCIDENT, 0, 0};
        fleet[i] = r;
    }
}

/* Writes a 0/1 eligibility mask for the fleet and compares with expected. */
static void check_eligible(const Resource fleet[FLEET_SIZE], IncidentType it,
                           const int expected[FLEET_SIZE], const char *label)
{
    int mismatches = 0;
    for (int i = 0; i < FLEET_SIZE; i++) {
        if (compat_is_eligible(&fleet[i], it) != expected[i]) {
            if (mismatches == 0)
                printf("  %s: R%d eligibility %d, expected %d\n",
                       label, fleet[i].id, !expected[i], expected[i]);
            mismatches++;
        }
    }
    CHECK_EQ_INT(mismatches, 0);
}

static void test_matrix(void)
{
    TEST_SECTION("every cell of the compatibility matrix");
    const int expected[INC_TYPE_COUNT][RES_TYPE_COUNT] = {
        /*                 AMB FIRE RESCUE */
        /* MEDICAL    */ { 1,   0,   0 },
        /* ROAD_ACC   */ { 1,   0,   1 },
        /* FIRE       */ { 0,   1,   0 },
        /* STRUCTURAL */ { 0,   0,   1 },
    };
    int mismatches = 0;
    for (int it = 0; it < INC_TYPE_COUNT; it++)
        for (int rt = 0; rt < RES_TYPE_COUNT; rt++)
            if (compat_is_compatible((IncidentType)it, (ResourceType)rt) != expected[it][rt])
                mismatches++;
    CHECK_EQ_INT(mismatches, 0);

    /* out-of-range enum values must not read outside the table */
    CHECK_EQ_INT(compat_is_compatible((IncidentType)-1, RES_AMBULANCE), 0);
    CHECK_EQ_INT(compat_is_compatible(INC_TYPE_COUNT, RES_AMBULANCE), 0);
    CHECK_EQ_INT(compat_is_compatible(INC_MEDICAL, RES_TYPE_COUNT), 0);
    CHECK_EQ_INT(compat_is_compatible(INC_MEDICAL, (ResourceType)-1), 0);
}

static void test_counts_and_completeness(void)
{
    TEST_SECTION("options per incident type; table completeness");
    CHECK_EQ_INT(compat_count_options(INC_MEDICAL), 1);
    CHECK_EQ_INT(compat_count_options(INC_ROAD_ACCIDENT), 2);
    CHECK_EQ_INT(compat_count_options(INC_FIRE), 1);
    CHECK_EQ_INT(compat_count_options(INC_STRUCTURAL_RESCUE), 1);
    CHECK_EQ_INT(compat_count_options(INC_TYPE_COUNT), 0);
    CHECK(compat_table_is_complete());   /* no incident type is unservable by design */

    printf("\n");
    compat_print_matrix(stdout);
    printf("\n  ROAD_ACCIDENT can be served by: ");
    compat_print_options(stdout, INC_ROAD_ACCIDENT);
    printf("\n\n");
}

static void test_eligibility_with_reference_fleet(void)
{
    TEST_SECTION("eligibility = compatible AND available (reference fleet)");
    Resource fleet[FLEET_SIZE];
    build_fleet(fleet);

    /*                                     R1 R2 R3 R4 R5 R6 */
    const int medical[FLEET_SIZE]       = {1, 1, 0, 0, 0, 1};
    const int road[FLEET_SIZE]          = {1, 1, 0, 1, 0, 1};
    const int fire[FLEET_SIZE]          = {0, 0, 1, 0, 1, 0};
    const int structural[FLEET_SIZE]    = {0, 0, 0, 1, 0, 0};
    check_eligible(fleet, INC_MEDICAL, medical, "MEDICAL");
    check_eligible(fleet, INC_ROAD_ACCIDENT, road, "ROAD_ACCIDENT");
    check_eligible(fleet, INC_FIRE, fire, "FIRE");
    check_eligible(fleet, INC_STRUCTURAL_RESCUE, structural, "STRUCTURAL_RESCUE");

    /* R5 (fire unit at 7) is the closest unit to Old Town (5) but must never
     * be eligible for a medical emergency, whatever the distance. */
    CHECK(!compat_is_eligible(&fleet[4], INC_MEDICAL));

    /* R2 is dispatched and R6 goes out of service: only R1 remains for MEDICAL. */
    CHECK_EQ_INT(resource_assign(&fleet[1], 1001, 11), DC_OK);
    CHECK_EQ_INT(resource_set_out_of_service(&fleet[5]), DC_OK);
    const int medical_after[FLEET_SIZE] = {1, 0, 0, 0, 0, 0};
    check_eligible(fleet, INC_MEDICAL, medical_after, "MEDICAL after changes");

    /* R2 released: eligible again */
    CHECK_EQ_INT(resource_release(&fleet[1], 5, 10), DC_OK);
    CHECK(compat_is_eligible(&fleet[1], INC_MEDICAL));

    CHECK(!compat_is_eligible(NULL, INC_MEDICAL));
}

int main(void)
{
    test_matrix();
    test_counts_and_completeness();
    test_eligibility_with_reference_fleet();
    return TEST_SUMMARY();
}
