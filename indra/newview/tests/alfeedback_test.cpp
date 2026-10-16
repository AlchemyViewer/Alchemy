/**
 * @file alfeedback_test.cpp
 * @brief Tests for what a feedback report is made of
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
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
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "../alfeedback.h"

#include "llsd.h"
#include "llsdjson.h"

#include <catch2/catch_test_macros.hpp>

#include <zlib.h>

#include <limits>

namespace
{
    // The next newline-terminated line of an envelope from pos, or the
    // given number of bytes and the newline after them.
    std::string_view take(std::string_view envelope, size_t& pos, size_t length = std::string_view::npos)
    {
        if (length == std::string_view::npos)
        {
            const size_t end = envelope.find('\n', pos);
            length = end == std::string_view::npos ? envelope.size() - pos : end - pos;
        }
        const std::string_view piece = envelope.substr(pos, length);
        pos += length + 1;
        return piece;
    }

    LLSD json(std::string_view text)
    {
        LLSD out;
        LlsdFromJsonString(text, out);
        return out;
    }
}

namespace
{
    TEST_CASE("alfeedback: a sentry.io DSN sends to its project's envelope endpoint", "[alfeedback]")
    {
        const auto endpoint = ALFeedback::endpointFromDsn("https://0123abcd@o42.ingest.sentry.io/4501");
        REQUIRE(endpoint.has_value());
        CHECK(endpoint->url == "https://o42.ingest.sentry.io/api/4501/envelope/");
        CHECK(endpoint->publicKey == "0123abcd");
    }

    TEST_CASE("alfeedback: a self-hosted DSN keeps its port and path and drops an old secret", "[alfeedback]")
    {
        const auto endpoint = ALFeedback::endpointFromDsn("https://key:secret@sentry.example.com:9000/prefix/7/");
        REQUIRE(endpoint.has_value());
        CHECK(endpoint->url == "https://sentry.example.com:9000/prefix/api/7/envelope/");
        CHECK(endpoint->publicKey == "key");
    }

    // A path may hold an '@': it is the path's, after the host, and not a
    // user part.
    TEST_CASE("alfeedback: an @ in a self-hosted DSN's path stays in its path", "[alfeedback]")
    {
        const auto endpoint = ALFeedback::endpointFromDsn("https://k@sentry.example.com/team@blue/7");
        REQUIRE(endpoint.has_value());
        CHECK(endpoint->url == "https://sentry.example.com/team@blue/api/7/envelope/");
        CHECK(endpoint->publicKey == "k");
    }

    TEST_CASE("alfeedback: a DSN missing a part is refused", "[alfeedback]")
    {
        UNSCOPED_INFO("no scheme");
        REQUIRE_FALSE(ALFeedback::endpointFromDsn("key@host/1"));
        UNSCOPED_INFO("another scheme");
        REQUIRE_FALSE(ALFeedback::endpointFromDsn("ftp://key@host/1"));
        UNSCOPED_INFO("no key");
        REQUIRE_FALSE(ALFeedback::endpointFromDsn("https://host/1"));
        UNSCOPED_INFO("no key, and an @ in the path, which names no user");
        REQUIRE_FALSE(ALFeedback::endpointFromDsn("https://sentry.example.com/team@blue/7"));
        UNSCOPED_INFO("empty key");
        REQUIRE_FALSE(ALFeedback::endpointFromDsn("https://@host/1"));
        UNSCOPED_INFO("no project");
        REQUIRE_FALSE(ALFeedback::endpointFromDsn("https://key@host"));
        UNSCOPED_INFO("empty host");
        REQUIRE_FALSE(ALFeedback::endpointFromDsn("https://key@/1"));
        REQUIRE_FALSE(ALFeedback::endpointFromDsn(""));
    }

    TEST_CASE("alfeedback: the auth header names the protocol version and the client and key", "[alfeedback]")
    {
        CHECK(ALFeedback::authHeader("0123abcd", "alchemy-feedback/26.4.0") ==
                      "Sentry sentry_version=7, sentry_client=alchemy-feedback/26.4.0, sentry_key=0123abcd");
    }

    TEST_CASE("alfeedback: settings named for keys or passwords or secrets or emails stay private", "[alfeedback]")
    {
        CHECK(ALFeedback::privateSetting("DeepLTranslateAPIKey"));
        UNSCOPED_INFO("any case");
        CHECK(ALFeedback::privateSetting("someapikey"));
        CHECK(ALFeedback::privateSetting("RememberPassword"));
        CHECK(ALFeedback::privateSetting("ServiceToken"));
        CHECK(ALFeedback::privateSetting("ClientSecret"));
        UNSCOPED_INFO("an address");
        CHECK(ALFeedback::privateSetting("AlchemyFeedbackEmail"));
        UNSCOPED_INFO("an ordinary setting goes");
        CHECK_FALSE(ALFeedback::privateSetting("RenderQualityPerformance"));
        UNSCOPED_INFO("a key binding goes");
        CHECK_FALSE(ALFeedback::privateSetting("ArrowKeysAlwaysMove"));
    }

    TEST_CASE("alfeedback: an email is checked for its shape only", "[alfeedback]")
    {
        UNSCOPED_INFO("plain");
        CHECK(ALFeedback::plausibleEmail("rye@alchemyviewer.org"));
        UNSCOPED_INFO("subdomain");
        CHECK(ALFeedback::plausibleEmail("a.b+c@mail.example.co.uk"));
        UNSCOPED_INFO("no at");
        CHECK_FALSE(ALFeedback::plausibleEmail("rye.alchemyviewer.org"));
        UNSCOPED_INFO("two ats");
        CHECK_FALSE(ALFeedback::plausibleEmail("a@b@c.d"));
        UNSCOPED_INFO("no local part");
        CHECK_FALSE(ALFeedback::plausibleEmail("@example.com"));
        UNSCOPED_INFO("no dot");
        CHECK_FALSE(ALFeedback::plausibleEmail("rye@localhost"));
        UNSCOPED_INFO("dot last");
        CHECK_FALSE(ALFeedback::plausibleEmail("rye@example."));
        UNSCOPED_INFO("dot first");
        CHECK_FALSE(ALFeedback::plausibleEmail("rye@.com"));
        UNSCOPED_INFO("space");
        CHECK_FALSE(ALFeedback::plausibleEmail("r ye@example.com"));
        CHECK_FALSE(ALFeedback::plausibleEmail(""));
    }

    TEST_CASE("alfeedback: a short log goes whole", "[alfeedback]")
    {
        CHECK(std::string(ALFeedback::logTail("one\ntwo\n", 64)) == "one\ntwo\n");
    }

    TEST_CASE("alfeedback: a long log's tail starts at a line", "[alfeedback]")
    {
        const std::string log = "first line\nsecond line\nthird\n";
        UNSCOPED_INFO("mid-line cut moves to the next line");
        CHECK(std::string(ALFeedback::logTail(log, 15)) == "third\n");
        UNSCOPED_INFO("a cut on a line start stays");
        CHECK(std::string(ALFeedback::logTail(log, 18)) ==
                      "second line\nthird\n");
    }

    TEST_CASE("alfeedback: a tail inside one long line never starts inside a character", "[alfeedback]")
    {
        // Three two-byte characters; five bytes from the end cuts the first.
        const std::string log = "\xC3\xA9\xC3\xA9\xC3\xA9";
        const std::string tail(ALFeedback::logTail(log, 5));
        CHECK(tail == "\xC3\xA9\xC3\xA9");
    }

    TEST_CASE("alfeedback: an event id is 32 lower-case hex digits and new each time", "[alfeedback]")
    {
        const std::string id = ALFeedback::newEventId();
        REQUIRE(id.size() == 32u);
        REQUIRE(id.find_first_not_of("0123456789abcdef") == std::string::npos);
        CHECK(id != ALFeedback::newEventId());
    }

    TEST_CASE("alfeedback: the feedback event carries the message as its feedback context", "[alfeedback]")
    {
        ALFeedback::Message message;
        message.kind = ALFeedback::Kind::Idea;
        message.text = "More knobs";
        message.name = "Rye";
        message.email = "rye@example.com";
        message.associatedEventId = "0123456789abcdef0123456789abcdef";
        ALFeedback::Context context;
        context.release = "alchemy@26.4.0+64033";
        context.environment = "Alchemy Test";
        context.dist = "64033";
        context.userId = "a2e76fcd-9360-4f6d-a924-000000000003";
        context.userName = "rye.resident";
        context.osName = "Microsoft Windows 11 64-bit";
        context.gpuName = "GeForce";
        context.gpuVendor = "NVIDIA";
        context.gpuVersion = "4.6";
        context.tags = { { "run_id", "run" }, { "empty", "" } };

        const LLSD event = ALFeedback::feedbackEvent("feedc0de", "2026-10-09T12:00:00Z", message, context);
        CHECK(event["event_id"].asString() == "feedc0de");
        CHECK(event["timestamp"].asString() == "2026-10-09T12:00:00Z");
        CHECK(event["level"].asString() == "info");
        CHECK(event["release"].asString() == "alchemy@26.4.0+64033");
        CHECK(event["environment"].asString() == "Alchemy Test");
        CHECK(event["dist"].asString() == "64033");
        CHECK(event["user"]["id"].asString() == context.userId);
        CHECK(event["user"]["username"].asString() == "rye.resident");
        CHECK(event["tags"]["feedback_kind"].asString() == "idea");
        CHECK(event["tags"]["run_id"].asString() == "run");
        UNSCOPED_INFO("an empty tag is left out");
        REQUIRE_FALSE(event["tags"].has("empty"));
        const LLSD& feedback = event["contexts"]["feedback"];
        CHECK(feedback["message"].asString() == "More knobs");
        CHECK(feedback["contact_email"].asString() == "rye@example.com");
        CHECK(feedback["name"].asString() == "Rye");
        CHECK(feedback["associated_event_id"].asString() == message.associatedEventId);
        CHECK(event["contexts"]["os"]["name"].asString() == context.osName);
        CHECK(event["contexts"]["gpu"]["vendor_name"].asString() == "NVIDIA");
    }

    TEST_CASE("alfeedback: what the user left empty is not in the event", "[alfeedback]")
    {
        ALFeedback::Message message;
        message.text = "It broke";
        const LLSD event = ALFeedback::feedbackEvent("feedc0de", "2026-10-09T12:00:00Z", message, ALFeedback::Context());
        REQUIRE_FALSE(event.has("user"));
        REQUIRE_FALSE(event.has("release"));
        REQUIRE_FALSE(event["contexts"].has("os"));
        REQUIRE_FALSE(event["contexts"].has("gpu"));
        const LLSD& feedback = event["contexts"]["feedback"];
        REQUIRE_FALSE(feedback.has("contact_email"));
        REQUIRE_FALSE(feedback.has("name"));
        REQUIRE_FALSE(feedback.has("associated_event_id"));
        UNSCOPED_INFO("problem by default");
        CHECK(event["tags"]["feedback_kind"].asString() == "problem");
    }

    TEST_CASE("alfeedback: a tag value is one line cut on a character within Sentry's limit", "[alfeedback]")
    {
        ALFeedback::Context context;
        std::string long_value(ALFeedback::TAG_VALUE_MAX_BYTES - 1, 'a');
        long_value += "\xC3\xA9tail";
        context.tags = { { "long", long_value }, { "lines", "one\ntwo\r\n" } };
        const LLSD event = ALFeedback::feedbackEvent("id", "t", ALFeedback::Message(), context);
        UNSCOPED_INFO("cut before the character");
        CHECK(event["tags"]["long"].asString() ==
                      std::string(ALFeedback::TAG_VALUE_MAX_BYTES - 1, 'a'));
        UNSCOPED_INFO("one line");
        CHECK(event["tags"]["lines"].asString() == "one two  ");
    }

    TEST_CASE("alfeedback: the envelope is a header then the feedback then attachments by byte length", "[alfeedback]")
    {
        ALFeedback::Message message;
        message.text = "caf\xC3\xA9 \xF0\x9F\x99\x82\nsecond line";
        const LLSD event = ALFeedback::feedbackEvent("feedc0de", "2026-10-09T12:00:00Z", message, ALFeedback::Context());
        const std::string binary("\x89PNG\0\n\r\x1a", 8);
        const std::vector<ALFeedback::Attachment> attachments = {
            { "screenshot.png", "image/png", binary },
            { "Alchemy.log", "text/plain", "line\n" },
        };
        const std::string bytes = ALFeedback::envelope("feedc0de", "https://k@h/1", { "alchemy.feedback", "26.4.0" },
                                                       LlsdToJson(event), attachments);

        size_t pos = 0;
        const LLSD header = json(take(bytes, pos));
        CHECK(header["event_id"].asString() == "feedc0de");
        CHECK(header["dsn"].asString() == "https://k@h/1");
        // A kept envelope goes again as it is, days later maybe: a time it
        // was sent would be read as the client's clock being wrong.
        REQUIRE_FALSE(header.has("sent_at"));
        CHECK(header["sdk"]["name"].asString() == "alchemy.feedback");

        const LLSD feedback_item = json(take(bytes, pos));
        CHECK(feedback_item["type"].asString() == "feedback");
        const LLSD payload = json(take(bytes, pos, feedback_item["length"].asInteger()));
        UNSCOPED_INFO("the message round-trips");
        CHECK(payload["contexts"]["feedback"]["message"].asString() == message.text);

        const LLSD screenshot_item = json(take(bytes, pos));
        CHECK(screenshot_item["type"].asString() == "attachment");
        CHECK(screenshot_item["filename"].asString() == "screenshot.png");
        CHECK(screenshot_item["content_type"].asString() == "image/png");
        CHECK(std::string(take(bytes, pos, screenshot_item["length"].asInteger())) == binary);

        const LLSD log_item = json(take(bytes, pos));
        CHECK(std::string(take(bytes, pos, log_item["length"].asInteger())) == "line\n");
        UNSCOPED_INFO("nothing after");
        REQUIRE(pos == bytes.size());
    }

    TEST_CASE("alfeedback: gzip makes one member that inflates back to the data", "[alfeedback]")
    {
        std::string data;
        for (int i = 0; i < 2000; ++i)
        {
            data += "2026-10-09T12:00:00Z INFO: a log line that repeats\n";
        }
        std::string zipped;
        REQUIRE(ALFeedback::gzip(data, zipped));
        {
            INFO("gzip magic");
            REQUIRE(zipped.size() > 2);
            CHECK(zipped[0] == '\x1f');
            CHECK(zipped[1] == '\x8b');
        }
        UNSCOPED_INFO("smaller");
        REQUIRE(zipped.size() < data.size() / 4);

        std::string inflated(data.size(), '\0');
        z_stream stream = {};
        CHECK(inflateInit2(&stream, MAX_WBITS + 16) == Z_OK);
        stream.next_in = reinterpret_cast<Bytef*>(zipped.data());
        stream.avail_in = static_cast<uInt>(zipped.size());
        stream.next_out = reinterpret_cast<Bytef*>(inflated.data());
        stream.avail_out = static_cast<uInt>(inflated.size());
        UNSCOPED_INFO("inflated whole");
        CHECK(inflate(&stream, Z_FINISH) == Z_STREAM_END);
        inflateEnd(&stream);
        UNSCOPED_INFO("round trip");
        CHECK(inflated == data);
    }

    TEST_CASE("alfeedback: the server's answer reads as an outcome", "[alfeedback]")
    {
        using ALFeedback::Outcome;
        CHECK(ALFeedback::classify(200) == Outcome::Sent);
        CHECK(ALFeedback::classify(429) == Outcome::RateLimited);
        CHECK(ALFeedback::classify(413) == Outcome::TooLarge);
        CHECK(ALFeedback::classify(400) == Outcome::Rejected);
        CHECK(ALFeedback::classify(403) == Outcome::Rejected);
        CHECK(ALFeedback::classify(408) == Outcome::Unreachable);
        CHECK(ALFeedback::classify(503) == Outcome::Unreachable);
        UNSCOPED_INFO("no answer");
        CHECK(ALFeedback::classify(0) == Outcome::Unreachable);
        CHECK(ALFeedback::retryable(Outcome::RateLimited));
        CHECK(ALFeedback::retryable(Outcome::Unreachable));
        CHECK_FALSE(ALFeedback::retryable(Outcome::Rejected));
        CHECK_FALSE(ALFeedback::retryable(Outcome::TooLarge));
        CHECK_FALSE(ALFeedback::retryable(Outcome::Sent));
    }

    TEST_CASE("alfeedback: a kept report's wait doubles from 15 minutes to 6 hours or more if asked", "[alfeedback]")
    {
        const S64 now = 1000000;
        CHECK(ALFeedback::nextAttempt(now, 1, 0.f) == now + 15 * 60);
        CHECK(ALFeedback::nextAttempt(now, 2, 0.f) == now + 30 * 60);
        CHECK(ALFeedback::nextAttempt(now, 3, 0.f) == now + 60 * 60);
        UNSCOPED_INFO("capped");
        CHECK(ALFeedback::nextAttempt(now, 20, 0.f) == now + 6 * 60 * 60);
        UNSCOPED_INFO("the server's word");
        CHECK(ALFeedback::nextAttempt(now, 1, 3600.5f) == now + 3601);
    }

    TEST_CASE("alfeedback: a kept report is given up after a week or five tries", "[alfeedback]")
    {
        ALFeedback::Queued queued;
        queued.created = 0;
        queued.attempts = 4;
        UNSCOPED_INFO("young and tried four times");
        CHECK_FALSE(ALFeedback::expired(queued, ALFeedback::QUEUE_MAX_AGE_SECONDS));
        UNSCOPED_INFO("over a week");
        CHECK(ALFeedback::expired(queued, ALFeedback::QUEUE_MAX_AGE_SECONDS + 1));
        queued.attempts = 5;
        UNSCOPED_INFO("five tries");
        CHECK(ALFeedback::expired(queued, 0));
    }

    TEST_CASE("alfeedback: a kept report's record round-trips past 2038 too", "[alfeedback]")
    {
        ALFeedback::Queued queued;
        queued.eventId = "0123abcd89abcdef0123456789abcdef";
        queued.created = 4102444800; // 2100
        queued.attempts = 3;
        queued.nextAt = 4102448400;
        const auto back = ALFeedback::queuedFromJson(ALFeedback::queuedToJson(queued));
        REQUIRE(back.has_value());
        CHECK(back->eventId == queued.eventId);
        CHECK(back->created == queued.created);
        CHECK(back->attempts == queued.attempts);
        CHECK(back->nextAt == queued.nextAt);
        UNSCOPED_INFO("not a record");
        REQUIRE_FALSE(ALFeedback::queuedFromJson("{\"nothing\":1}"));
        UNSCOPED_INFO("not json");
        REQUIRE_FALSE(ALFeedback::queuedFromJson("garbage"));
    }

    TEST_CASE("alfeedback: a DSN sends in the clear only to this machine", "[alfeedback]")
    {
        const auto local = ALFeedback::endpointFromDsn("http://0123abcd@127.0.0.1:8765/1");
        UNSCOPED_INFO("loopback");
        REQUIRE(local.has_value());
        CHECK(local->url == "http://127.0.0.1:8765/api/1/envelope/");
        REQUIRE(ALFeedback::endpointFromDsn("http://k@localhost/1").has_value());
        UNSCOPED_INFO("IPv6 loopback");
        REQUIRE(ALFeedback::endpointFromDsn("http://k@[::1]:9000/1").has_value());
        UNSCOPED_INFO("another host");
        REQUIRE_FALSE(ALFeedback::endpointFromDsn("http://k@o42.ingest.sentry.io/4501"));
        UNSCOPED_INFO("a host that starts like loopback");
        REQUIRE_FALSE(ALFeedback::endpointFromDsn("http://k@127.0.0.1.example.com/1"));
        UNSCOPED_INFO("loopback as a user name");
        REQUIRE_FALSE(ALFeedback::endpointFromDsn("http://127.0.0.1@evil.example.com/1"));
        UNSCOPED_INFO("loopback in the path");
        REQUIRE_FALSE(ALFeedback::endpointFromDsn("http://k@evil.example.com/127.0.0.1/1"));
        // A second user part, after the key's, would make what follows it the
        // host the address names, whatever the loopback check took for one.
        UNSCOPED_INFO("loopback as the user name of another host");
        REQUIRE_FALSE(ALFeedback::endpointFromDsn("http://k@127.0.0.1:8765@evil.example.com/1"));
        UNSCOPED_INFO("IPv6 loopback as the user name of another host");
        REQUIRE_FALSE(ALFeedback::endpointFromDsn("http://k@[::1]@evil.example.com/1"));
        UNSCOPED_INFO("a second user part, encrypted or not");
        REQUIRE_FALSE(ALFeedback::endpointFromDsn("https://k@o42.ingest.sentry.io@evil.example.com/4501"));
    }

    TEST_CASE("alfeedback: a report's logs hide capability addresses and the given words", "[alfeedback]")
    {
        const std::string log =
            "INFO: Requesting seed from https://simhost-0a1b.agni.lindenlab.com:12043/cap/"
            "0f1e2d3c-4b5a-6978-8796-a5b4c3d2e1f0 region name Ahern\n"
            "INFO: Event poll url 'http://grid.example.org:9000/CAPS/0F1E2D3C-4B5A-6978-8796-A5B4C3D2E1F00000/'\n"
            "INFO: Attempting login as: Rye.Resident\n"
            "INFO: Logging out as agent: a2e76fcd-9360-4f6d-a924-000000000003\n";
        const std::string scrubbed =
            ALFeedback::scrubLog(log, { "rye resident", "rye.resident", "a2e76fcd-9360-4f6d-a924-000000000003", "" });
        UNSCOPED_INFO("a Second Life capability");
        REQUIRE(scrubbed.find("0f1e2d3c") == std::string::npos);
        UNSCOPED_INFO("an OpenSim capability, any case");
        REQUIRE(scrubbed.find("0F1E2D3C") == std::string::npos);
        UNSCOPED_INFO("what is not secret stays");
        REQUIRE(scrubbed.find("simhost-0a1b.agni.lindenlab.com:12043/cap/[hidden] region name Ahern")
                                               != std::string::npos);
        UNSCOPED_INFO("the path after it stays");
        REQUIRE(scrubbed.find("/CAPS/[hidden]/'") != std::string::npos);
        UNSCOPED_INFO("a name, any case");
        REQUIRE(scrubbed.find("Attempting login as: [hidden]\n") != std::string::npos);
        UNSCOPED_INFO("an id");
        REQUIRE(scrubbed.find("agent: [hidden]\n") != std::string::npos);
        UNSCOPED_INFO("nothing to hide is the log");
        CHECK(ALFeedback::scrubLog("plain line\n", {}) == "plain line\n");
    }

    TEST_CASE("alfeedback: only an id can name a file", "[alfeedback]")
    {
        CHECK(ALFeedback::validEventId(ALFeedback::newEventId()));
        UNSCOPED_INFO("upper case");
        CHECK_FALSE(ALFeedback::validEventId("0123456789ABCDEF0123456789abcdef"));
        UNSCOPED_INFO("short");
        CHECK_FALSE(ALFeedback::validEventId("0123456789abcdef"));
        UNSCOPED_INFO("a path");
        CHECK_FALSE(ALFeedback::validEventId("../../../../../../../../../../x"));
        UNSCOPED_INFO("dashed");
        CHECK_FALSE(ALFeedback::validEventId("01234567-89ab-cdef-0123-456789abcdef"));
        UNSCOPED_INFO("nil");
        CHECK_FALSE(ALFeedback::validEventId("00000000000000000000000000000000"));
        UNSCOPED_INFO("a reference is its start");
        CHECK(ALFeedback::reference("0123456789abcdef0123456789abcdef") == "01234567");
    }

    TEST_CASE("alfeedback: a changed setting's text shows only when it is plain", "[alfeedback]")
    {
        UNSCOPED_INFO("a number");
        CHECK(ALFeedback::settingText("RenderQualityPerformance", LLSD(3)) == "i3");
        UNSCOPED_INFO("a word");
        CHECK(ALFeedback::settingText("SkinCurrent", LLSD("alchemy")) == "'alchemy'");
        UNSCOPED_INFO("a path");
        CHECK(ALFeedback::settingText("InstantMessageLogPath", LLSD("C:\\Users\\rye\\chat")) ==
                      "(hidden)");
        UNSCOPED_INFO("a sentence");
        CHECK(ALFeedback::settingText("DoNotDisturbModeResponse", LLSD("Away right now")) ==
                      "(hidden)");
        UNSCOPED_INFO("an address");
        CHECK(ALFeedback::settingText("StreamURL", LLSD("http://radio.example.com/?k=1")) ==
                      "(hidden)");
        UNSCOPED_INFO("a name");
        CHECK(ALFeedback::settingText("LastName", LLSD("Resident")) == "(hidden)");
        UNSCOPED_INFO("a list");
        CHECK(ALFeedback::settingText("StreamList", LLSD::emptyArray()) == "(hidden)");
    }

    TEST_CASE("alfeedback: a wait the server asks for is a sane number of seconds", "[alfeedback]")
    {
        CHECK(ALFeedback::serverWait(0.f) == 0);
        CHECK(ALFeedback::serverWait(-5.f) == 0);
        CHECK(ALFeedback::serverWait(std::numeric_limits<F32>::quiet_NaN()) == 0);
        UNSCOPED_INFO("rounded up");
        CHECK(ALFeedback::serverWait(90.2f) == 91);
        UNSCOPED_INFO("forever is a week");
        CHECK(ALFeedback::serverWait(std::numeric_limits<F32>::infinity()) ==
                      ALFeedback::QUEUE_MAX_AGE_SECONDS);
        UNSCOPED_INFO("too long is a week");
        CHECK(ALFeedback::serverWait(1e30f) == ALFeedback::QUEUE_MAX_AGE_SECONDS);
        const S64 now = 1000000;
        UNSCOPED_INFO("never before now");
        CHECK(ALFeedback::nextAttempt(now, 1, std::numeric_limits<F32>::infinity()) > now);
    }

    TEST_CASE("alfeedback: a kept record keeps what the user wrote and refuses what is not one", "[alfeedback]")
    {
        ALFeedback::Queued queued;
        queued.eventId = "0123abcd89abcdef0123456789abcdef";
        queued.notBefore = 4102448500;
        queued.kind = ALFeedback::Kind::Idea;
        queued.message = "More knobs \xF0\x9F\x99\x82";
        const auto back = ALFeedback::queuedFromJson(ALFeedback::queuedToJson(queued));
        REQUIRE(back.has_value());
        CHECK(back->notBefore == queued.notBefore);
        CHECK(back->kind == ALFeedback::Kind::Idea);
        CHECK(back->message == queued.message);
        UNSCOPED_INFO("a path for an id");
        REQUIRE_FALSE(ALFeedback::queuedFromJson("{\"event_id\":\"../../x\",\"created\":1,\"attempts\":0}"));
        const auto wild = ALFeedback::queuedFromJson(
            "{\"event_id\":\"0123abcd89abcdef0123456789abcdef\",\"created\":1e300,\"next_at\":-5,\"attempts\":-3}");
        UNSCOPED_INFO("times that are not times");
        REQUIRE(wild.has_value());
        CHECK(wild->created == 0);
        CHECK(wild->nextAt == 0);
        CHECK(wild->attempts == 0);
    }

    TEST_CASE("alfeedback: a draft round-trips and drops what is not an id or a link", "[alfeedback]")
    {
        ALFeedback::Draft draft;
        draft.kind = ALFeedback::Kind::Other;
        draft.message = "Half a thought";
        draft.eventId = "0123abcd89abcdef0123456789abcdef";
        draft.associatedEventId = "fedcba9876543210fedcba9876543210";
        draft.linked = "freeze";
        draft.linkedAt = "2026-10-09T12:00:00Z";
        draft.linkedRunId = "run";
        const auto back = ALFeedback::draftFromJson(ALFeedback::draftToJson(draft));
        REQUIRE(back.has_value());
        CHECK(back->kind == ALFeedback::Kind::Other);
        CHECK(back->message == draft.message);
        CHECK(back->eventId == draft.eventId);
        CHECK(back->associatedEventId == draft.associatedEventId);
        CHECK(back->linked == "freeze");
        CHECK(back->linkedAt == draft.linkedAt);
        CHECK(back->linkedRunId == "run");

        const auto odd = ALFeedback::draftFromJson(
            "{\"message\":\"hi there\",\"event_id\":\"../x\",\"linked\":\"party\",\"kind\":\"nonsense\"}");
        REQUIRE(odd.has_value());
        REQUIRE(odd->eventId.empty());
        REQUIRE(odd->linked.empty());
        UNSCOPED_INFO("a problem");
        CHECK(odd->kind == ALFeedback::Kind::Problem);
        UNSCOPED_INFO("an empty draft is none");
        REQUIRE_FALSE(ALFeedback::draftFromJson("{\"message\":\"  \\n\"}"));
    }

    TEST_CASE("alfeedback: kinds and outcomes have names", "[alfeedback]")
    {
        CHECK(ALFeedback::kindFromName(ALFeedback::kindName(ALFeedback::Kind::Idea)) == ALFeedback::Kind::Idea);
        CHECK(ALFeedback::kindFromName("other") == ALFeedback::Kind::Other);
        UNSCOPED_INFO("anything else");
        CHECK(ALFeedback::kindFromName("bug") == ALFeedback::Kind::Problem);
        CHECK(std::string(ALFeedback::outcomeName(ALFeedback::Outcome::RateLimited)) == "rate limited");
    }
}
