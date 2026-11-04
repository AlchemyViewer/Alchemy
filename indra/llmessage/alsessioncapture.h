/**
 * @file alsessioncapture.h
 * @brief A recording of a session's traffic and objects.
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

#pragma once

#include "llfile.h"
#include "llmutex.h"
#include "llsingleton.h"

#include <string>

class LLHost;
class LLSD;

namespace LLCore
{
    class HttpResponse;
}

// A dev build's recording of a session, for a client to replay and compare
// with what the viewer made of it. Only a tree configured with
// AL_SESSION_CAPTURE has it, which only the test channel may be, and never
// in its Release configuration; and it records only while the environment
// variable ALCHEMY_SESSION_CAPTURE_DIR names a directory. A recording holds
// session IDs, circuit codes and capability URLs, so it is written there
// alone, to a new file only its owner may read where the platform has the
// notion; the login password is never written.
//
// The file, session-<UTC date and time>-<process ID>.alcap, is the line
// "ALSESSIONCAPTURE 1\n", then records. Each record is a big-endian U32 byte
// count, then that many bytes: one LLSD value in binary with no header line,
// as LLSDSerialize::toBinary writes it. A record is a map whose type names
// what it holds, with time, the seconds since 1970 UTC it was recorded at,
// and, where the main thread recorded it, frame, LLFrameTimer's frame count
// then. The records come in the order the viewer met what they hold. Their
// types:
//
// - capture, the first: version, 1, and viewer, a map of channel, version and
//   platform, strings.
// - login, each login attempt once it has its final answer: request, the map
//   LLLogin posts, with uri and method and the login parameters in params,
//   but for passwd and mfa_hash; and response, the map the XML-RPC
//   transaction answered with: status, errorcode, error and transfer_rate,
//   and responses, the login response.
// - packet, each UDP message the message system receives or sends:
//   direction, "in" or "out"; host, the circuit's "a.b.c.d:port"; bytes,
//   the message after zero-decoding, from its flags byte, as the wire carried
//   it, through its body, with no acks; acks, an array of the packet IDs
//   appended after it on the wire, in their order there; wire_size, the bytes
//   it took on the wire; and injected, true for a message the viewer's
//   message builder put in rather than the network. A message received is
//   recorded before the message system validates it, so one it then drops is
//   in the recording too, though not a packet too short to hold a message or
//   whose acks run past it. A reliable message resent is not recorded again.
// - http, each capability response the coroutine adapter completes, before
//   it is parsed: method; url; status, LLCore::HttpStatus::getType, which is
//   the HTTP status, or the library's error where none came; success, the
//   status as a boolean; content_type; and body, its bytes.
// - event, each event queue event as it is dispatched: sender, the region's
//   "a.b.c.d:port"; message, its name; and body.
// - objects, a region's objects as the viewer holds them, every
//   ALCHEMY_SESSION_CAPTURE_INTERVAL seconds (30 where it is unset, none
//   where it is 0) once logged in, and at logout: reason, "interval" or
//   "logout"; region, a map of name, id, host and handle (ll_sd_from_U64's
//   eight bytes); and objects, an array of maps of
//   - id, a UUID, and local_id and pcode, integers, a U32 as the integer
//     with its bits;
//   - parent_id and parent_local_id, for an object with a parent;
//   - position, rotation and scale, as LLXform keeps them: position and
//     rotation relative to the parent, or to the region for an object with
//     none; position and scale arrays of three reals, rotation an array of
//     four, x, y, z then s;
//   - velocity, angular_velocity and acceleration, arrays of three reals;
//   - flags, LLViewerObject's update flags;
//   - volume, for an object with a volume, LLVolumeParams::asLLSD;
//   - textures, an array of each face's LLTextureEntry::asLLSD, and
//     material_overrides, an array of each face's GLTF material override as
//     LLGLTFMaterial::getOverrideLLSD writes it over the default material,
//     undefined for a face with none;
//   - extra_params, a map of each extra parameter in use, by name: flexible,
//     light, sculpt (which a mesh's is), light_image, extended_mesh and
//     reflection_probe, each its asLLSD, and render_material, an array of
//     maps of te and id;
//   - name_values, an array of each name-value pair as printNameValue writes
//     it;
//   - text and text_color, the hover text and its color as an array of four
//     reals;
//   - particles, for an object with a particle source, a map of its
//     LLPartSysData's fields as the message names them;
//   - texture_anim, for a volume with a texture animation, its
//     LLTextureAnim::asLLSD.
class ALSessionCapture : public LLSingleton<ALSessionCapture>
{
    LLSINGLETON_EMPTY_CTOR(ALSessionCapture);
    ~ALSessionCapture() override;

public:
    /// Starts recording where ALCHEMY_SESSION_CAPTURE_DIR names a directory,
    /// beginning with the capture record and viewer, a map of what the viewer
    /// says of itself.
    static void startFromEnvironment(const LLSD& viewer);

    /// Whether a recording is being made.
    static bool capturing();

    /// A UDP message: its bytes on the wire, and the message they carry once
    /// zero-decoded, acks off.
    void recordPacket(bool sent,
                      const LLHost& host,
                      const U8* wire,
                      S32 wire_size,
                      const U8* decoded,
                      S32 decoded_size,
                      bool injected);

    /// A capability response, before anything parses it.
    void recordHttp(const LLCore::HttpResponse& response);

    /// An event queue event, as it is dispatched.
    void recordEvent(const std::string& sender, const std::string& message, const LLSD& body);

    /// A login attempt and its final answer.
    void recordLogin(const LLSD& request, const LLSD& response);

    /// Any record, given its type, stamped and written as the rest are.
    void record(const std::string& type, LLSD fields);

    /// Writes what is held back to the file.
    void flush();

private:
    bool open(const std::string& directory);
    void writeLocked(const std::string& bytes);
    void flushLocked();

    LLMutex mLock;
    LLFile mFile;
    std::string mPending;
    F64 mLastFlush = 0.0;
};
