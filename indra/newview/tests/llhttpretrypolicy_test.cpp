/**
 * @file llhttpretrypolicy_test.cpp
 * @brief Header tests to exercise the LLHTTPRetryPolicy classes.
 *
 * $LicenseInfo:firstyear=2013&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2013, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "../llviewerprecompiledheaders.h"
#include "../llhttpretrypolicy.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "altestmatchers.h"

namespace
{
TEST_CASE("retry_policy: a policy of zero retries never retries", "[retry_policy]")
{
    LLAdaptiveRetryPolicy never_retry(1.0,1.0,1.0,0);
    LLSD headers;
    F32 wait_seconds;

    // No retry until we've failed a try.
    CHECK_FALSE(never_retry.shouldRetry(wait_seconds));

    // 0 retries max.
    never_retry.onFailure(500,headers);
    CHECK_FALSE(never_retry.shouldRetry(wait_seconds));
}

TEST_CASE("retry_policy: a 404 is retried only when 4xx retries are enabled", "[retry_policy]")
{
    LLSD headers;
    F32 wait_seconds;

    // Normally only retry on server error (5xx)
    LLAdaptiveRetryPolicy noRetry404(1.0,2.0,3.0,10);
    noRetry404.onFailure(404,headers);
    CHECK_FALSE(noRetry404.shouldRetry(wait_seconds));

    // Can retry on 4xx errors if enabled by flag.
    bool do_retry_4xx = true;
    LLAdaptiveRetryPolicy doRetry404(1.0,2.0,3.0,10,do_retry_4xx);
    doRetry404.onFailure(404,headers);
    CHECK(doRetry404.shouldRetry(wait_seconds));
}

TEST_CASE("retry_policy: waits double to a cap and retries stop at the limit until a success", "[retry_policy]")
{
    // Should retry after 1.0, 2.0, 3.0, 3.0 seconds.
    LLAdaptiveRetryPolicy basic_retry(1.0,3.0,2.0,4);
    LLSD headers;
    F32 wait_seconds;
    bool should_retry;
    U32 frac_bits = 6;

    // No retry until we've failed a try.
    CHECK_FALSE(basic_retry.shouldRetry(wait_seconds));

    // Starting wait 1.0
    basic_retry.onFailure(500,headers);
    should_retry = basic_retry.shouldRetry(wait_seconds);
    CHECK(should_retry);
    CHECK_THAT(wait_seconds, ll_test::WithinBits(1.0F, frac_bits));

    // Double wait to 2.0
    basic_retry.onFailure(500,headers);
    should_retry = basic_retry.shouldRetry(wait_seconds);
    CHECK(should_retry);
    CHECK_THAT(wait_seconds, ll_test::WithinBits(2.0F, frac_bits));

    // Hit max wait of 3.0 (4.0 clamped to max 3)
    basic_retry.onFailure(500,headers);
    should_retry = basic_retry.shouldRetry(wait_seconds);
    CHECK(should_retry);
    CHECK_THAT(wait_seconds, ll_test::WithinBits(3.0F, frac_bits));

    // At max wait, should stay at 3.0
    basic_retry.onFailure(500,headers);
    should_retry = basic_retry.shouldRetry(wait_seconds);
    CHECK(should_retry);
    CHECK_THAT(wait_seconds, ll_test::WithinBits(3.0F, frac_bits));

    // Max retries, should fail now.
    basic_retry.onFailure(500,headers);
    should_retry = basic_retry.shouldRetry(wait_seconds);
    CHECK_FALSE(should_retry);

    // Max retries, should fail now.
    basic_retry.onFailure(500,headers);
    should_retry = basic_retry.shouldRetry(wait_seconds);
    CHECK_FALSE(should_retry);

    // After a success, should reset to the starting state.
    basic_retry.onSuccess();

    // No retry until we've failed a try.
    CHECK_FALSE(basic_retry.shouldRetry(wait_seconds));

    // Starting wait 1.0
    basic_retry.onFailure(500,headers);
    should_retry = basic_retry.shouldRetry(wait_seconds);
    CHECK(should_retry);
    CHECK_THAT(wait_seconds, ll_test::WithinBits(1.0F, frac_bits));

    // Double wait to 2.0
    basic_retry.onFailure(500,headers);
    should_retry = basic_retry.shouldRetry(wait_seconds);
    CHECK(should_retry);
    CHECK_THAT(wait_seconds, ll_test::WithinBits(2.0F, frac_bits));
}

TEST_CASE("retry_policy: retries stop for good at the first non-5xx error", "[retry_policy]")
{
    // Should retry after 1.0, 2.0, 3.0, 3.0 seconds.
    LLAdaptiveRetryPolicy killer404(1.0,3.0,2.0,4);
    LLSD headers;
    F32 wait_seconds;
    bool should_retry;
    U32 frac_bits = 6;

    // Starting wait 1.0
    killer404.onFailure(500,headers);
    should_retry = killer404.shouldRetry(wait_seconds);
    CHECK(should_retry);
    CHECK_THAT(wait_seconds, ll_test::WithinBits(1.0F, frac_bits));

    // Double wait to 2.0
    killer404.onFailure(500,headers);
    should_retry = killer404.shouldRetry(wait_seconds);
    CHECK(should_retry);
    CHECK_THAT(wait_seconds, ll_test::WithinBits(2.0F, frac_bits));

    // Should fail on non-5xx
    killer404.onFailure(404,headers);
    should_retry = killer404.shouldRetry(wait_seconds);
    CHECK_FALSE(should_retry);

    // After a non-5xx, should keep failing.
    killer404.onFailure(500,headers);
    should_retry = killer404.shouldRetry(wait_seconds);
    CHECK_FALSE(should_retry);
}

// Test handling of "retry-after" header. If present, this header
// value overrides the computed delay, but does not affect the
// progression of delay values.  For example, if the normal
// progression of delays would be 1,2,4,8..., but the 2nd and 3rd calls
// get a retry header of 33, the pattern would become 1,33,33,8...
TEST_CASE("retry_policy: a retry-after header overrides the wait but not its progression", "[retry_policy]")
{
    LLAdaptiveRetryPolicy policy(1.0,25.0,2.0,6);
    LLSD headers_with_retry;
    headers_with_retry[HTTP_IN_HEADER_RETRY_AFTER] = "666";
    LLSD headers_without_retry;
    F32 wait_seconds;
    bool should_retry;
    U32 frac_bits = 6;

    policy.onFailure(500,headers_without_retry);
    should_retry = policy.shouldRetry(wait_seconds);
    CHECK(should_retry);
    CHECK_THAT(wait_seconds, ll_test::WithinBits(1.0F, frac_bits));

    policy.onFailure(500,headers_without_retry);
    should_retry = policy.shouldRetry(wait_seconds);
    CHECK(should_retry);
    CHECK_THAT(wait_seconds, ll_test::WithinBits(2.0F, frac_bits));

    policy.onFailure(500,headers_with_retry);
    should_retry = policy.shouldRetry(wait_seconds);
    CHECK(should_retry);
    // 4.0 overrides by header -> 666.0
    CHECK_THAT(wait_seconds, ll_test::WithinBits(666.0F, frac_bits));

    policy.onFailure(500,headers_with_retry);
    should_retry = policy.shouldRetry(wait_seconds);
    CHECK(should_retry);
    // 8.0 overrides by header -> 666.0
    CHECK_THAT(wait_seconds, ll_test::WithinBits(666.0F, frac_bits));

    policy.onFailure(500,headers_without_retry);
    should_retry = policy.shouldRetry(wait_seconds);
    CHECK(should_retry);
    CHECK_THAT(wait_seconds, ll_test::WithinBits(16.0F, frac_bits));

    policy.onFailure(500,headers_without_retry);
    should_retry = policy.shouldRetry(wait_seconds);
    CHECK(should_retry);
    CHECK_THAT(wait_seconds, ll_test::WithinBits(25.0F, frac_bits));

    policy.onFailure(500,headers_with_retry);
    should_retry = policy.shouldRetry(wait_seconds);
    CHECK_FALSE(should_retry);
}

// Test getSecondsUntilRetryAfter(const std::string& retry_after, F32& seconds_to_wait),
// used by header parsing of the retry policy.
TEST_CASE("retry_policy: getSecondsUntilRetryAfter parses seconds and an HTTP date", "[retry_policy]")
{
    F32 seconds_to_wait;
    bool success;

    std::string str1("0");
    seconds_to_wait = F32_MAX;
    success = LLAdaptiveRetryPolicy::getSecondsUntilRetryAfter(str1, seconds_to_wait);
    CHECK(success);
    CHECK(seconds_to_wait == 0.0);

    std::string str2("999.9");
    seconds_to_wait = F32_MAX;
    success = LLAdaptiveRetryPolicy::getSecondsUntilRetryAfter(str2, seconds_to_wait);
    CHECK(success);
    CHECK_THAT(seconds_to_wait, ll_test::WithinBits(999.9F, 8));

    time_t nowseconds;
    time(&nowseconds);
    std::string str3 = LLDate((F64)(nowseconds+44)).asRFC1123();
    seconds_to_wait = F32_MAX;
    success = LLAdaptiveRetryPolicy::getSecondsUntilRetryAfter(str3, seconds_to_wait);
    std::cerr << " str3 [" << str3 << "]" << std::endl;
    CHECK(success);
    CHECK_THAT(seconds_to_wait, Catch::Matchers::WithinAbs(44.0F, 2.0F));
}

TEST_CASE("retry_policy: retry-after is honoured in both llmessage and CoreHttp headers", "[retry_policy]")
{
    LLSD sd_headers;
    time_t nowseconds;
    time(&nowseconds);
    LLAdaptiveRetryPolicy policy(17.0,644.0,3.0,5);
    F32 seconds_to_wait;
    bool should_retry;

    // No retry until we've failed a try.
    CHECK_FALSE(policy.shouldRetry(seconds_to_wait));

    // no retry header, use default.
    policy.onFailure(500,LLSD());
    should_retry = policy.shouldRetry(seconds_to_wait);
    CHECK(should_retry);
    CHECK_THAT(seconds_to_wait, ll_test::WithinBits(17.0F, 6));

    // A retry-after date 7 seconds off overrides the default: a delay of
    // about 7 seconds.
    std::string date_string = LLDate((F64)(nowseconds+7)).asRFC1123();
    sd_headers[HTTP_IN_HEADER_RETRY_AFTER] = date_string;
    policy.onFailure(503,sd_headers);
    should_retry = policy.shouldRetry(seconds_to_wait);
    CHECK(should_retry);
    CHECK_THAT(seconds_to_wait, Catch::Matchers::WithinAbs(7.0F, 2.0F));

    LLCore::HttpResponse *response;
    LLCore::HttpHeaders::ptr_t headers;

    response = new LLCore::HttpResponse();
    headers = LLCore::HttpHeaders::ptr_t(new LLCore::HttpHeaders());
    response->setStatus(503);
    response->setHeaders(headers);
    headers->append(HTTP_IN_HEADER_RETRY_AFTER, std::string("600"));
    policy.onFailure(response);
    should_retry = policy.shouldRetry(seconds_to_wait);
    CHECK(should_retry);
    CHECK_THAT(seconds_to_wait, ll_test::WithinBits(600.0F, 6));
    response->release();

    response = new LLCore::HttpResponse();
    headers = LLCore::HttpHeaders::ptr_t(new LLCore::HttpHeaders());
    response->setStatus(503);
    response->setHeaders(headers);
    time(&nowseconds);
    date_string = LLDate((F64)(nowseconds+77)).asRFC1123();
    std::cerr << "date_string [" << date_string << "]" << std::endl;
    headers->append(HTTP_IN_HEADER_RETRY_AFTER,date_string);
    policy.onFailure(response);
    should_retry = policy.shouldRetry(seconds_to_wait);
    CHECK(should_retry);
    CHECK_THAT(seconds_to_wait, Catch::Matchers::WithinAbs(77.0F, 2.0F));
    response->release();

    // Timeout should be clamped at max.
    policy.onFailure(500,LLSD());
    should_retry = policy.shouldRetry(seconds_to_wait);
    CHECK(should_retry);
    CHECK_THAT(seconds_to_wait, ll_test::WithinBits(644.0F, 6));

    // No more retries.
    policy.onFailure(500,LLSD());
    should_retry = policy.shouldRetry(seconds_to_wait);
    CHECK_FALSE(should_retry);
}

}

