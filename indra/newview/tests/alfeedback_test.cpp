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

#include "../test/lltut.h"

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

namespace tut
{
    struct alfeedback_data
    {
    };

    typedef test_group<alfeedback_data> alfeedback_group;
    typedef alfeedback_group::object object;
    alfeedback_group alfeedbackgrp("alfeedback");

    template<> template<>
    void object::test<1>()
    {
        set_test_name("a sentry.io DSN sends to its project's envelope endpoint");
        const auto endpoint = ALFeedback::endpointFromDsn("https://0123abcd@o42.ingest.sentry.io/4501");
        ensure("parsed", endpoint.has_value());
        ensure_equals(endpoint->url, "https://o42.ingest.sentry.io/api/4501/envelope/");
        ensure_equals(endpoint->publicKey, "0123abcd");
    }

    template<> template<>
    void object::test<2>()
    {
        set_test_name("a self-hosted DSN keeps its port and path, and drops an old secret");
        const auto endpoint = ALFeedback::endpointFromDsn("https://key:secret@sentry.example.com:9000/prefix/7/");
        ensure("parsed", endpoint.has_value());
        ensure_equals(endpoint->url, "https://sentry.example.com:9000/prefix/api/7/envelope/");
        ensure_equals(endpoint->publicKey, "key");
    }

    template<> template<>
    void object::test<3>()
    {
        set_test_name("a DSN missing a part is refused");
        ensure("no scheme", !ALFeedback::endpointFromDsn("key@host/1"));
        ensure("another scheme", !ALFeedback::endpointFromDsn("ftp://key@host/1"));
        ensure("no key", !ALFeedback::endpointFromDsn("https://host/1"));
        ensure("empty key", !ALFeedback::endpointFromDsn("https://@host/1"));
        ensure("no project", !ALFeedback::endpointFromDsn("https://key@host"));
        ensure("empty host", !ALFeedback::endpointFromDsn("https://key@/1"));
        ensure("empty", !ALFeedback::endpointFromDsn(""));
    }

    template<> template<>
    void object::test<4>()
    {
        set_test_name("the auth header names the protocol, the client and the key");
        ensure_equals(ALFeedback::authHeader("0123abcd", "alchemy-feedback/26.4.0"),
                      "Sentry sentry_version=7, sentry_client=alchemy-feedback/26.4.0, sentry_key=0123abcd");
    }

    template<> template<>
    void object::test<5>()
    {
        set_test_name("keys, passwords and tokens stay out of the settings sent");
        ensure("translation key", ALFeedback::privateSetting("DeepLTranslateAPIKey"));
        ensure("any case", ALFeedback::privateSetting("someapikey"));
        ensure("password", ALFeedback::privateSetting("RememberPassword"));
        ensure("token", ALFeedback::privateSetting("ServiceToken"));
        ensure("secret", ALFeedback::privateSetting("ClientSecret"));
        ensure("an address", ALFeedback::privateSetting("AlchemyFeedbackEmail"));
        ensure("an ordinary setting goes", !ALFeedback::privateSetting("RenderQualityPerformance"));
        ensure("a key binding goes", !ALFeedback::privateSetting("ArrowKeysAlwaysMove"));
    }

    template<> template<>
    void object::test<6>()
    {
        set_test_name("an email is checked for its shape only");
        ensure("plain", ALFeedback::plausibleEmail("rye@alchemyviewer.org"));
        ensure("subdomain", ALFeedback::plausibleEmail("a.b+c@mail.example.co.uk"));
        ensure("no at", !ALFeedback::plausibleEmail("rye.alchemyviewer.org"));
        ensure("two ats", !ALFeedback::plausibleEmail("a@b@c.d"));
        ensure("no local part", !ALFeedback::plausibleEmail("@example.com"));
        ensure("no dot", !ALFeedback::plausibleEmail("rye@localhost"));
        ensure("dot last", !ALFeedback::plausibleEmail("rye@example."));
        ensure("dot first", !ALFeedback::plausibleEmail("rye@.com"));
        ensure("space", !ALFeedback::plausibleEmail("r ye@example.com"));
        ensure("empty", !ALFeedback::plausibleEmail(""));
    }

    template<> template<>
    void object::test<7>()
    {
        set_test_name("a short log goes whole");
        ensure_equals(std::string(ALFeedback::logTail("one\ntwo\n", 64)), "one\ntwo\n");
    }

    template<> template<>
    void object::test<8>()
    {
        set_test_name("a long log's tail starts at a line");
        const std::string log = "first line\nsecond line\nthird\n";
        ensure_equals("mid-line cut moves to the next line", std::string(ALFeedback::logTail(log, 15)), "third\n");
        ensure_equals("a cut on a line start stays", std::string(ALFeedback::logTail(log, 18)),
                      "second line\nthird\n");
    }

    template<> template<>
    void object::test<9>()
    {
        set_test_name("a tail inside one long line never starts inside a character");
        // Three two-byte characters; five bytes from the end cuts the first.
        const std::string log = "\xC3\xA9\xC3\xA9\xC3\xA9";
        const std::string tail(ALFeedback::logTail(log, 5));
        ensure_equals(tail, "\xC3\xA9\xC3\xA9");
    }

    template<> template<>
    void object::test<10>()
    {
        set_test_name("an event id is 32 lower-case hex digits, new each time");
        const std::string id = ALFeedback::newEventId();
        ensure_equals("length", id.size(), 32u);
        ensure("hex", id.find_first_not_of("0123456789abcdef") == std::string::npos);
        ensure("new", id != ALFeedback::newEventId());
    }

    template<> template<>
    void object::test<11>()
    {
        set_test_name("the feedback event carries the message as its feedback context");
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
        ensure_equals("id", event["event_id"].asString(), "feedc0de");
        ensure_equals("timestamp", event["timestamp"].asString(), "2026-10-09T12:00:00Z");
        ensure_equals("level", event["level"].asString(), "info");
        ensure_equals("release", event["release"].asString(), "alchemy@26.4.0+64033");
        ensure_equals("environment", event["environment"].asString(), "Alchemy Test");
        ensure_equals("dist", event["dist"].asString(), "64033");
        ensure_equals("user id", event["user"]["id"].asString(), context.userId);
        ensure_equals("user name", event["user"]["username"].asString(), "rye.resident");
        ensure_equals("kind", event["tags"]["feedback_kind"].asString(), "idea");
        ensure_equals("tag", event["tags"]["run_id"].asString(), "run");
        ensure("an empty tag is left out", !event["tags"].has("empty"));
        const LLSD& feedback = event["contexts"]["feedback"];
        ensure_equals("message", feedback["message"].asString(), "More knobs");
        ensure_equals("email", feedback["contact_email"].asString(), "rye@example.com");
        ensure_equals("name", feedback["name"].asString(), "Rye");
        ensure_equals("associated", feedback["associated_event_id"].asString(), message.associatedEventId);
        ensure_equals("os", event["contexts"]["os"]["name"].asString(), context.osName);
        ensure_equals("gpu", event["contexts"]["gpu"]["vendor_name"].asString(), "NVIDIA");
    }

    template<> template<>
    void object::test<12>()
    {
        set_test_name("what the user left empty is not in the event");
        ALFeedback::Message message;
        message.text = "It broke";
        const LLSD event = ALFeedback::feedbackEvent("feedc0de", "2026-10-09T12:00:00Z", message, ALFeedback::Context());
        ensure("no user", !event.has("user"));
        ensure("no release", !event.has("release"));
        ensure("no os", !event["contexts"].has("os"));
        ensure("no gpu", !event["contexts"].has("gpu"));
        const LLSD& feedback = event["contexts"]["feedback"];
        ensure("no email", !feedback.has("contact_email"));
        ensure("no name", !feedback.has("name"));
        ensure("no association", !feedback.has("associated_event_id"));
        ensure_equals("problem by default", event["tags"]["feedback_kind"].asString(), "problem");
    }

    template<> template<>
    void object::test<13>()
    {
        set_test_name("a tag value is one line within Sentry's limit, cut on a character");
        ALFeedback::Context context;
        std::string long_value(ALFeedback::TAG_VALUE_MAX_BYTES - 1, 'a');
        long_value += "\xC3\xA9tail";
        context.tags = { { "long", long_value }, { "lines", "one\ntwo\r\n" } };
        const LLSD event = ALFeedback::feedbackEvent("id", "t", ALFeedback::Message(), context);
        ensure_equals("cut before the character", event["tags"]["long"].asString(),
                      std::string(ALFeedback::TAG_VALUE_MAX_BYTES - 1, 'a'));
        ensure_equals("one line", event["tags"]["lines"].asString(), "one two  ");
    }

    template<> template<>
    void object::test<14>()
    {
        set_test_name("the envelope is a header, the feedback, then each attachment, lengths in bytes");
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
        ensure_equals("header id", header["event_id"].asString(), "feedc0de");
        ensure_equals("header dsn", header["dsn"].asString(), "https://k@h/1");
        // A kept envelope goes again as it is, days later maybe: a time it
        // was sent would be read as the client's clock being wrong.
        ensure("no time sent", !header.has("sent_at"));
        ensure_equals("header sdk", header["sdk"]["name"].asString(), "alchemy.feedback");

        const LLSD feedback_item = json(take(bytes, pos));
        ensure_equals("feedback type", feedback_item["type"].asString(), "feedback");
        const LLSD payload = json(take(bytes, pos, feedback_item["length"].asInteger()));
        ensure_equals("the message round-trips", payload["contexts"]["feedback"]["message"].asString(), message.text);

        const LLSD screenshot_item = json(take(bytes, pos));
        ensure_equals("attachment type", screenshot_item["type"].asString(), "attachment");
        ensure_equals("filename", screenshot_item["filename"].asString(), "screenshot.png");
        ensure_equals("content type", screenshot_item["content_type"].asString(), "image/png");
        ensure_equals("binary bytes", std::string(take(bytes, pos, screenshot_item["length"].asInteger())), binary);

        const LLSD log_item = json(take(bytes, pos));
        ensure_equals("log bytes", std::string(take(bytes, pos, log_item["length"].asInteger())), "line\n");
        ensure_equals("nothing after", pos, bytes.size());
    }

    template<> template<>
    void object::test<15>()
    {
        set_test_name("gzip makes one member that inflates back to the data");
        std::string data;
        for (int i = 0; i < 2000; ++i)
        {
            data += "2026-10-09T12:00:00Z INFO: a log line that repeats\n";
        }
        std::string zipped;
        ensure("compressed", ALFeedback::gzip(data, zipped));
        ensure("gzip magic", zipped.size() > 2 && zipped[0] == '\x1f' && zipped[1] == '\x8b');
        ensure("smaller", zipped.size() < data.size() / 4);

        std::string inflated(data.size(), '\0');
        z_stream stream = {};
        ensure_equals("inflate init", inflateInit2(&stream, MAX_WBITS + 16), Z_OK);
        stream.next_in = reinterpret_cast<Bytef*>(zipped.data());
        stream.avail_in = static_cast<uInt>(zipped.size());
        stream.next_out = reinterpret_cast<Bytef*>(inflated.data());
        stream.avail_out = static_cast<uInt>(inflated.size());
        ensure_equals("inflated whole", inflate(&stream, Z_FINISH), Z_STREAM_END);
        inflateEnd(&stream);
        ensure("round trip", inflated == data);
    }

    template<> template<>
    void object::test<16>()
    {
        set_test_name("the server's answer reads as an outcome");
        using ALFeedback::Outcome;
        ensure("200", ALFeedback::classify(200) == Outcome::Sent);
        ensure("429", ALFeedback::classify(429) == Outcome::RateLimited);
        ensure("413", ALFeedback::classify(413) == Outcome::TooLarge);
        ensure("400", ALFeedback::classify(400) == Outcome::Rejected);
        ensure("403", ALFeedback::classify(403) == Outcome::Rejected);
        ensure("408", ALFeedback::classify(408) == Outcome::Unreachable);
        ensure("503", ALFeedback::classify(503) == Outcome::Unreachable);
        ensure("no answer", ALFeedback::classify(0) == Outcome::Unreachable);
        ensure("retry when limited", ALFeedback::retryable(Outcome::RateLimited));
        ensure("retry when unreachable", ALFeedback::retryable(Outcome::Unreachable));
        ensure("no retry when refused", !ALFeedback::retryable(Outcome::Rejected));
        ensure("no retry when too large", !ALFeedback::retryable(Outcome::TooLarge));
        ensure("no retry when sent", !ALFeedback::retryable(Outcome::Sent));
    }

    template<> template<>
    void object::test<17>()
    {
        set_test_name("a kept report waits a quarter hour, doubling to six hours, never less than asked");
        const S64 now = 1000000;
        ensure_equals("first", ALFeedback::nextAttempt(now, 1, 0.f), now + 15 * 60);
        ensure_equals("second", ALFeedback::nextAttempt(now, 2, 0.f), now + 30 * 60);
        ensure_equals("third", ALFeedback::nextAttempt(now, 3, 0.f), now + 60 * 60);
        ensure_equals("capped", ALFeedback::nextAttempt(now, 20, 0.f), now + 6 * 60 * 60);
        ensure_equals("the server's word", ALFeedback::nextAttempt(now, 1, 3600.5f), now + 3601);
    }

    template<> template<>
    void object::test<18>()
    {
        set_test_name("a kept report is given up after a week or five tries");
        ALFeedback::Queued queued;
        queued.created = 0;
        queued.attempts = 4;
        ensure("young and tried four times", !ALFeedback::expired(queued, ALFeedback::QUEUE_MAX_AGE_SECONDS));
        ensure("over a week", ALFeedback::expired(queued, ALFeedback::QUEUE_MAX_AGE_SECONDS + 1));
        queued.attempts = 5;
        ensure("five tries", ALFeedback::expired(queued, 0));
    }

    template<> template<>
    void object::test<19>()
    {
        set_test_name("a kept report's record round-trips, past 2038 too");
        ALFeedback::Queued queued;
        queued.eventId = "0123abcd89abcdef0123456789abcdef";
        queued.created = 4102444800; // 2100
        queued.attempts = 3;
        queued.nextAt = 4102448400;
        const auto back = ALFeedback::queuedFromJson(ALFeedback::queuedToJson(queued));
        ensure("parsed", back.has_value());
        ensure_equals("id", back->eventId, queued.eventId);
        ensure_equals("created", back->created, queued.created);
        ensure_equals("attempts", back->attempts, queued.attempts);
        ensure_equals("next", back->nextAt, queued.nextAt);
        ensure("not a record", !ALFeedback::queuedFromJson("{\"nothing\":1}"));
        ensure("not json", !ALFeedback::queuedFromJson("garbage"));
    }

    template<> template<>
    void object::test<20>()
    {
        set_test_name("a DSN sends in the clear only to this machine");
        const auto local = ALFeedback::endpointFromDsn("http://0123abcd@127.0.0.1:8765/1");
        ensure("loopback", local.has_value());
        ensure_equals(local->url, "http://127.0.0.1:8765/api/1/envelope/");
        ensure("localhost", ALFeedback::endpointFromDsn("http://k@localhost/1").has_value());
        ensure("IPv6 loopback", ALFeedback::endpointFromDsn("http://k@[::1]:9000/1").has_value());
        ensure("another host", !ALFeedback::endpointFromDsn("http://k@o42.ingest.sentry.io/4501"));
        ensure("a host that starts like loopback", !ALFeedback::endpointFromDsn("http://k@127.0.0.1.example.com/1"));
        ensure("loopback as a user name", !ALFeedback::endpointFromDsn("http://k@evil.example.com/127.0.0.1/1"));
    }

    template<> template<>
    void object::test<21>()
    {
        set_test_name("a report's logs hide capability addresses and the given words");
        const std::string log =
            "INFO: Requesting seed from https://simhost-0a1b.agni.lindenlab.com:12043/cap/"
            "0f1e2d3c-4b5a-6978-8796-a5b4c3d2e1f0 region name Ahern\n"
            "INFO: Event poll url 'http://grid.example.org:9000/CAPS/0F1E2D3C-4B5A-6978-8796-A5B4C3D2E1F00000/'\n"
            "INFO: Attempting login as: Rye.Resident\n"
            "INFO: Logging out as agent: a2e76fcd-9360-4f6d-a924-000000000003\n";
        const std::string scrubbed =
            ALFeedback::scrubLog(log, { "rye resident", "rye.resident", "a2e76fcd-9360-4f6d-a924-000000000003", "" });
        ensure("a Second Life capability", scrubbed.find("0f1e2d3c") == std::string::npos);
        ensure("an OpenSim capability, any case", scrubbed.find("0F1E2D3C") == std::string::npos);
        ensure("what is not secret stays", scrubbed.find("simhost-0a1b.agni.lindenlab.com:12043/cap/[hidden] region name Ahern")
                                               != std::string::npos);
        ensure("the path after it stays", scrubbed.find("/CAPS/[hidden]/'") != std::string::npos);
        ensure("a name, any case", scrubbed.find("Attempting login as: [hidden]\n") != std::string::npos);
        ensure("an id", scrubbed.find("agent: [hidden]\n") != std::string::npos);
        ensure_equals("nothing to hide is the log", ALFeedback::scrubLog("plain line\n", {}), "plain line\n");
    }

    template<> template<>
    void object::test<22>()
    {
        set_test_name("only an id can name a file");
        ensure("an id", ALFeedback::validEventId(ALFeedback::newEventId()));
        ensure("upper case", !ALFeedback::validEventId("0123456789ABCDEF0123456789abcdef"));
        ensure("short", !ALFeedback::validEventId("0123456789abcdef"));
        ensure("a path", !ALFeedback::validEventId("../../../../../../../../../../x"));
        ensure("dashed", !ALFeedback::validEventId("01234567-89ab-cdef-0123-456789abcdef"));
        ensure("nil", !ALFeedback::validEventId("00000000000000000000000000000000"));
        ensure_equals("a reference is its start", ALFeedback::reference("0123456789abcdef0123456789abcdef"), "01234567");
    }

    template<> template<>
    void object::test<23>()
    {
        set_test_name("a changed setting's text shows only when it is plain");
        ensure_equals("a number", ALFeedback::settingText("RenderQualityPerformance", LLSD(3)), "i3");
        ensure_equals("a word", ALFeedback::settingText("SkinCurrent", LLSD("alchemy")), "'alchemy'");
        ensure_equals("a path", ALFeedback::settingText("InstantMessageLogPath", LLSD("C:\\Users\\rye\\chat")),
                      "(hidden)");
        ensure_equals("a sentence", ALFeedback::settingText("DoNotDisturbModeResponse", LLSD("Away right now")),
                      "(hidden)");
        ensure_equals("an address", ALFeedback::settingText("StreamURL", LLSD("http://radio.example.com/?k=1")),
                      "(hidden)");
        ensure_equals("a name", ALFeedback::settingText("LastName", LLSD("Resident")), "(hidden)");
        ensure_equals("a list", ALFeedback::settingText("StreamList", LLSD::emptyArray()), "(hidden)");
    }

    template<> template<>
    void object::test<24>()
    {
        set_test_name("a wait the server asks for is a sane number of seconds");
        ensure_equals("none", ALFeedback::serverWait(0.f), 0);
        ensure_equals("negative", ALFeedback::serverWait(-5.f), 0);
        ensure_equals("not a number", ALFeedback::serverWait(std::numeric_limits<F32>::quiet_NaN()), 0);
        ensure_equals("rounded up", ALFeedback::serverWait(90.2f), 91);
        ensure_equals("forever is a week", ALFeedback::serverWait(std::numeric_limits<F32>::infinity()),
                      ALFeedback::QUEUE_MAX_AGE_SECONDS);
        ensure_equals("too long is a week", ALFeedback::serverWait(1e30f), ALFeedback::QUEUE_MAX_AGE_SECONDS);
        const S64 now = 1000000;
        ensure("never before now", ALFeedback::nextAttempt(now, 1, std::numeric_limits<F32>::infinity()) > now);
    }

    template<> template<>
    void object::test<25>()
    {
        set_test_name("a kept record keeps what the user wrote, and refuses what is not one");
        ALFeedback::Queued queued;
        queued.eventId = "0123abcd89abcdef0123456789abcdef";
        queued.notBefore = 4102448500;
        queued.kind = ALFeedback::Kind::Idea;
        queued.message = "More knobs \xF0\x9F\x99\x82";
        const auto back = ALFeedback::queuedFromJson(ALFeedback::queuedToJson(queued));
        ensure("parsed", back.has_value());
        ensure_equals("not before", back->notBefore, queued.notBefore);
        ensure("kind", back->kind == ALFeedback::Kind::Idea);
        ensure_equals("message", back->message, queued.message);
        ensure("a path for an id",
               !ALFeedback::queuedFromJson("{\"event_id\":\"../../x\",\"created\":1,\"attempts\":0}"));
        const auto wild = ALFeedback::queuedFromJson(
            "{\"event_id\":\"0123abcd89abcdef0123456789abcdef\",\"created\":1e300,\"next_at\":-5,\"attempts\":-3}");
        ensure("times that are not times", wild.has_value());
        ensure_equals("created", wild->created, 0);
        ensure_equals("next", wild->nextAt, 0);
        ensure_equals("attempts", wild->attempts, 0);
    }

    template<> template<>
    void object::test<26>()
    {
        set_test_name("a draft round-trips, and what is not an id or a link is dropped");
        ALFeedback::Draft draft;
        draft.kind = ALFeedback::Kind::Other;
        draft.message = "Half a thought";
        draft.eventId = "0123abcd89abcdef0123456789abcdef";
        draft.associatedEventId = "fedcba9876543210fedcba9876543210";
        draft.linked = "freeze";
        draft.linkedAt = "2026-10-09T12:00:00Z";
        draft.linkedRunId = "run";
        const auto back = ALFeedback::draftFromJson(ALFeedback::draftToJson(draft));
        ensure("parsed", back.has_value());
        ensure("kind", back->kind == ALFeedback::Kind::Other);
        ensure_equals("message", back->message, draft.message);
        ensure_equals("id", back->eventId, draft.eventId);
        ensure_equals("associated", back->associatedEventId, draft.associatedEventId);
        ensure_equals("linked", back->linked, "freeze");
        ensure_equals("when", back->linkedAt, draft.linkedAt);
        ensure_equals("run", back->linkedRunId, "run");

        const auto odd = ALFeedback::draftFromJson(
            "{\"message\":\"hi there\",\"event_id\":\"../x\",\"linked\":\"party\",\"kind\":\"nonsense\"}");
        ensure("parsed", odd.has_value());
        ensure("no id", odd->eventId.empty());
        ensure("no link", odd->linked.empty());
        ensure("a problem", odd->kind == ALFeedback::Kind::Problem);
        ensure("an empty draft is none", !ALFeedback::draftFromJson("{\"message\":\"  \\n\"}"));
    }

    template<> template<>
    void object::test<27>()
    {
        set_test_name("kinds and outcomes have names");
        ensure("idea", ALFeedback::kindFromName(ALFeedback::kindName(ALFeedback::Kind::Idea)) == ALFeedback::Kind::Idea);
        ensure("other", ALFeedback::kindFromName("other") == ALFeedback::Kind::Other);
        ensure("anything else", ALFeedback::kindFromName("bug") == ALFeedback::Kind::Problem);
        ensure_equals(std::string(ALFeedback::outcomeName(ALFeedback::Outcome::RateLimited)), "rate limited");
    }
}
