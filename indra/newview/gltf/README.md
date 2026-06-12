# Linden Lab GLTF Implementation

Currently in prototype stage.  Much functionality is missing (blend shapes,
multiple texture coordinates, etc).

GLTF Specification can be found here: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html.
If this implementation disagrees with the GLTF Specification, the specification is correct.

Class structure and naming should match the GLTF Specification as closely as possible while
conforming to the LL coding standards.  All code in headers should be contained in the
LL::GLTF namespace.

The implementation serves both the client and the server.

## Design Principles

- The implementation MUST be capable of round-trip serialization with no data loss beyond F64 to F32 conversions.
- The implementation MUST use the same indexing scheme as the GLTF specification.  Do not store pointers where the
- GLTF specification stores indices, store indices.
- Limit dependencies on llcommon as much as possible.  Prefer std::, simdjson, and glm:: over LL facsimiles.
- Usage of LLSD is forbidden in the LL::GLTF namespace.
- Use "using namespace" liberally in .cpp files, but never in .h files.
- "using Foo = Bar" is permissible in .h files within the LL::GLTF namespace.

## Loading, Copying, and Serialization
JSON parsing uses simdjson ("Value" is an alias of simdjson::dom::element, a
read-only handle into a parsed document).  Serialization streams JSON text
through JsonWriter (common.h), a thin comma/colon-managing wrapper over
simdjson's string_builder -- members are emitted in call order, there is no
mutable document tree.

Each class should provide two functions (Primitive shown for example):

```cpp
// Append "this" in json form to the provided writer
// Do not serialize default values
void serialize(JsonWriter& obj) const;

// Initialize from a provided json value
const Primitive& operator=(const Value& src);
```

"serialize" implementations should use "write":

```cpp
void Primitive::serialize(JsonWriter& dst) const
{
    write(mMaterial, "material", dst, -1);
    write(mMode, "mode", dst, Mode::TRIANGLES);
    write(mIndices, "indices", dst, INVALID_INDEX);
    write(mAttributes, "attributes", dst);
}
```

And operator= implementations should use "copy":

```cpp
const Primitive& Primitive::operator=(const Value& src)
{
    if (src.is_object())
    {
        copy(src, "material", mMaterial);
        copy(src, "mode", mMode);
        copy(src, "indices", mIndices);
        copy(src, "attributes", mAttributes);

        mGLMode = gltf_mode_to_gl_mode(mMode);
    }
    return *this;
}
```

Parameters to "write" and "copy" MUST be ordered "src" before "dst"
so the code reads as "write src to dst" and "copy src to dst".

When reading string constants from GLTF json (i.e. "OPAQUE", "TRIANGLES"), these
strings should be converted to enums inside operator=.  It is permissible to
store the original strings during prototyping to aid in development, but eventually
we'll purge these strings from the implementation.  However, implementations MUST
preserve any and all "name" members.

"write" and "copy" implementations MUST be stored in buffer_util.h.
As implementers encounter new data types, you'll see compiler errors
pointing at templates in buffer_util.h.  See vec3 as a known good
example of how to add support for a new type (there are bad examples, so beware):

```cpp
// vec3
template<>
inline bool copy(const Value& src, vec3& dst)
{
    simdjson::dom::array arr;
    if (src.get_array().get(arr) == simdjson::SUCCESS && arr.size() == 3)
    {
        vec3 t;
        if (to_float(arr.at(0).value_unsafe(), t.x) &&
            to_float(arr.at(1).value_unsafe(), t.y) &&
            to_float(arr.at(2).value_unsafe(), t.z))
        {
            dst = t;
            return true;
        }
    }
    return false;
}

template<>
inline bool write(const vec3& src, JsonWriter& dst)
{
    dst.startArray();
    dst.value(src.x);
    dst.value(src.y);
    dst.value(src.z);
    dst.endArray();
    return true;
}

```

"write" MUST return true if ANY data was written
"copy" MUST return true if ANY data was copied

Speed is important, but so is safety.  In writers, stream values directly --
don't build temporary containers and copy them.

simdjson is used through its error-code interface: every accessor returns a
simdjson_result that must be checked with .get(out) == simdjson::SUCCESS before
the value is used.  DO NOT add exception handlers and DO NOT use the throwing
value() accessors in serialization paths.  If a type mismatch is possible, the
fix is to handle the error code.

DO NOT rely on existing type conversion tools in the LL codebase -- LL data models
conflict with the GLTF specification so we MUST provide conversions independent of
our existing implementations.

### JSON Serialization ###



NEVER include buffer_util.h from a header.

Loading from and saving to disk (import/export) is currently done using tinygltf, but this is not a long term
solution.  Eventually the implementation should rely solely on simdjson/JsonWriter for reading and writing .gltf
files and should handle .bin files natively.

When serializing Images and Buffers to the server, clients MUST store a single UUID "uri" field and nothing else.
The server MUST reject any data that violates this requirement.

Clients MUST remove any Images from Buffers prior to upload to the server.
Servers MAY reject Assets that contain Buffers with unreferenced data.

... to be continued.



