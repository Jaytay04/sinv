#include <stdint.h>
#include <string.h>

#include "control_session_protocol.h"
#include "test_common.h"

static HYPERVISOR_CONTROL_PROTOCOL_REQUEST ValidRequest(void)
{
    HYPERVISOR_CONTROL_PROTOCOL_REQUEST request;
    request.Magic = HYPERVISOR_CONTROL_PROTOCOL_MAGIC;
    request.Major = HYPERVISOR_CONTROL_PROTOCOL_MAJOR;
    request.Minor = HYPERVISOR_CONTROL_PROTOCOL_MINOR;
    request.Size = (uint32_t)sizeof(request);
    request.Flags = HYPERVISOR_CONTROL_PROTOCOL_REQUEST_FLAGS_NONE;
    return request;
}

static int test_authorizes_only_the_initial_attempt(void)
{
    TEST_ASSERT_UINT_EQ(
        ControlSessionProtocolAuthorizeAttempt(UINT8_C(0)),
        CONTROL_SESSION_PROTOCOL_ATTEMPT_ALLOWED,
        "fresh session negotiation");
    TEST_ASSERT_UINT_EQ(
        ControlSessionProtocolAuthorizeAttempt(UINT8_C(1)),
        CONTROL_SESSION_PROTOCOL_ATTEMPT_REPEATED,
        "repeated session negotiation");
    TEST_ASSERT_UINT_EQ(
        ControlSessionProtocolAuthorizeAttempt(UINT8_C(2)),
        CONTROL_SESSION_PROTOCOL_ATTEMPT_INVALID_STATE,
        "corrupt session negotiation state");
    TEST_ASSERT_UINT_EQ(
        ControlSessionProtocolAuthorizeAttempt(UINT8_MAX),
        CONTROL_SESSION_PROTOCOL_ATTEMPT_INVALID_STATE,
        "maximum corrupt session negotiation state");
    return 0;
}

static int test_accepts_exact_protocol(void)
{
    HYPERVISOR_CONTROL_PROTOCOL_REQUEST request = ValidRequest();
    HYPERVISOR_CONTROL_PROTOCOL_RESPONSE response;

    memset(&response, 0xA5, sizeof(response));
    TEST_ASSERT_UINT_EQ(
        ControlSessionProtocolNegotiate(
            &request, sizeof(request), sizeof(response), &response),
        CONTROL_SESSION_PROTOCOL_ACCEPTED, "exact negotiation");
    TEST_ASSERT_UINT_EQ(response.Magic, HYPERVISOR_CONTROL_PROTOCOL_MAGIC,
        "response magic");
    TEST_ASSERT_UINT_EQ(response.Major, HYPERVISOR_CONTROL_PROTOCOL_MAJOR,
        "response major");
    TEST_ASSERT_UINT_EQ(response.Minor, HYPERVISOR_CONTROL_PROTOCOL_MINOR,
        "response minor");
    TEST_ASSERT_UINT_EQ(response.Size, sizeof(response), "response size");
    TEST_ASSERT_UINT_EQ(
        response.Flags,
        HYPERVISOR_CONTROL_PROTOCOL_RESPONSE_NEGOTIATED,
        "response negotiated flag");
    return 0;
}

static int test_rejects_incompatible_versions_without_output(void)
{
    HYPERVISOR_CONTROL_PROTOCOL_REQUEST request = ValidRequest();
    HYPERVISOR_CONTROL_PROTOCOL_RESPONSE response;
    HYPERVISOR_CONTROL_PROTOCOL_RESPONSE unchanged;

    memset(&response, 0x5A, sizeof(response));
    unchanged = response;
    request.Major++;
    TEST_ASSERT_UINT_EQ(
        ControlSessionProtocolNegotiate(
            &request, sizeof(request), sizeof(response), &response),
        CONTROL_SESSION_PROTOCOL_INCOMPATIBLE, "wrong major");
    TEST_ASSERT(memcmp(&response, &unchanged, sizeof(response)) == 0,
        "incompatible request must not write output");

    request = ValidRequest();
    request.Minor++;
    TEST_ASSERT_UINT_EQ(
        ControlSessionProtocolNegotiate(
            &request, sizeof(request), sizeof(response), &response),
        CONTROL_SESSION_PROTOCOL_INCOMPATIBLE, "wrong minor");
    return 0;
}

static int test_rejects_malformed_requests_without_output(void)
{
    HYPERVISOR_CONTROL_PROTOCOL_REQUEST request = ValidRequest();
    HYPERVISOR_CONTROL_PROTOCOL_RESPONSE response;
    HYPERVISOR_CONTROL_PROTOCOL_RESPONSE unchanged;

    memset(&response, 0x3C, sizeof(response));
    unchanged = response;
    TEST_ASSERT_UINT_EQ(
        ControlSessionProtocolNegotiate(
            NULL, sizeof(request), sizeof(response), &response),
        CONTROL_SESSION_PROTOCOL_MALFORMED, "null request");
    TEST_ASSERT_UINT_EQ(
        ControlSessionProtocolNegotiate(
            &request, sizeof(request) - 1U, sizeof(response), &response),
        CONTROL_SESSION_PROTOCOL_MALFORMED, "short input");
    TEST_ASSERT_UINT_EQ(
        ControlSessionProtocolNegotiate(
            &request, sizeof(request), sizeof(response) + 1U, &response),
        CONTROL_SESSION_PROTOCOL_MALFORMED, "oversized output");
    TEST_ASSERT_UINT_EQ(
        ControlSessionProtocolNegotiate(
            &request, sizeof(request), sizeof(response), NULL),
        CONTROL_SESSION_PROTOCOL_MALFORMED, "null response");

    request = ValidRequest();
    request.Magic ^= UINT32_C(1);
    TEST_ASSERT_UINT_EQ(
        ControlSessionProtocolNegotiate(
            &request, sizeof(request), sizeof(response), &response),
        CONTROL_SESSION_PROTOCOL_MALFORMED, "wrong magic");
    request = ValidRequest();
    request.Size--;
    TEST_ASSERT_UINT_EQ(
        ControlSessionProtocolNegotiate(
            &request, sizeof(request), sizeof(response), &response),
        CONTROL_SESSION_PROTOCOL_MALFORMED, "wrong embedded size");
    request = ValidRequest();
    request.Flags = UINT32_C(1);
    TEST_ASSERT_UINT_EQ(
        ControlSessionProtocolNegotiate(
            &request, sizeof(request), sizeof(response), &response),
        CONTROL_SESSION_PROTOCOL_MALFORMED, "reserved request flags");
    TEST_ASSERT(memcmp(&response, &unchanged, sizeof(response)) == 0,
        "malformed requests must not write output");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(test_authorizes_only_the_initial_attempt);
    TEST_RUN(test_accepts_exact_protocol);
    TEST_RUN(test_rejects_incompatible_versions_without_output);
    TEST_RUN(test_rejects_malformed_requests_without_output);
TEST_MAIN_END()
