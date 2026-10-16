#pragma once

/**
 * @file buffer_util.inl
 * @brief LL GLTF Implementation
 *
 * $LicenseInfo:firstyear=2024&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2024, Linden Research, Inc.
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

// inline template implementations for copying data out of GLTF buffers
// DO NOT include from header files to avoid the need to rebuild the whole project
// whenever we add support for more types

#ifdef _MSC_VER
#define LL_FUNCSIG __FUNCSIG__
#else
#define LL_FUNCSIG __PRETTY_FUNCTION__
#endif

#include "accessor.h"

namespace LL
{
    namespace GLTF
    {

        using string_view = std::string_view;

        // copy one Scalar from src to dst
        template<class S, class T>
        inline void copyScalar(S* src, T& dst)
        {
            LL_ERRS() << "TODO: implement " << LL_FUNCSIG << LL_ENDL;
        }

        // copy one vec2 from src to dst
        template<class S, class T>
        inline void copyVec2(S* src, T& dst)
        {
            LL_ERRS() << "TODO: implement " << LL_FUNCSIG << LL_ENDL;
        }

        // copy one vec3 from src to dst
        template<class S, class T>
        inline void copyVec3(S* src, T& dst)
        {
            LL_ERRS() << "TODO: implement " << LL_FUNCSIG << LL_ENDL;
        }

        // copy one vec4 from src to dst
        template<class S, class T>
        inline void copyVec4(S* src, T& dst)
        {
            LL_ERRS() << "TODO: implement " << LL_FUNCSIG << LL_ENDL;
        }

        // copy one mat2 from src to dst
        template<class S, class T>
        inline void copyMat2(S* src, T& dst)
        {
            LL_ERRS() << "TODO: implement " << LL_FUNCSIG << LL_ENDL;
        }

        // copy one mat3 from src to dst
        template<class S, class T>
        inline void copyMat3(S* src, T& dst)
        {
            LL_ERRS() << "TODO: implement " << LL_FUNCSIG << LL_ENDL;
        }

        // copy one mat4 from src to dst
        template<class S, class T>
        inline void copyMat4(S* src, T& dst)
        {
            LL_ERRS() << "TODO: implement " << LL_FUNCSIG << LL_ENDL;
        }

        //=========================================================================================================
        // concrete implementations for different types of source and destination
        //=========================================================================================================

        template<>
        inline void copyScalar<F32, F32>(F32* src, F32& dst)
        {
            dst = *src;
        }

        template<>
        inline void copyScalar<U32, U32>(U32* src, U32& dst)
        {
            dst = *src;
        }

        template<>
        inline void copyScalar<U32, U16>(U32* src, U16& dst)
        {
            dst = *src;
        }

        template<>
        inline void copyScalar<U16, U16>(U16* src, U16& dst)
        {
            dst = *src;
        }

        template<>
        inline void copyScalar<U16, U32>(U16* src, U32& dst)
        {
            dst = *src;
        }

        template<>
        inline void copyScalar<U8, U16>(U8* src, U16& dst)
        {
            dst = *src;
        }

        template<>
        inline void copyScalar<U8, U32>(U8* src, U32& dst)
        {
            dst = *src;
        }

        template<>
        inline void copyScalar<U32, LLVector4a>(U32* src, LLVector4a& dst)
        {
            dst.set((F32)*src, 0.f, 0.f, 0.f);
        }

        template<>
        inline void copyScalar<U16, LLVector4a>(U16* src, LLVector4a& dst)
        {
            dst.set((F32)*src, 0.f, 0.f, 0.f);
        }

        template<>
        inline void copyScalar<U8, LLVector4a>(U8* src, LLVector4a& dst)
        {
            dst.set((F32)*src, 0.f, 0.f, 0.f);
        }

        template<>
        inline void copyScalar<U32, LLVector2>(U32* src, LLVector2& dst)
        {
            dst.set((F32)*src, 0.f);
        }

        template<>
        inline void copyScalar<U16, LLVector2>(U16* src, LLVector2& dst)
        {
            dst.set((F32)*src, 0.f);
        }

        template<>
        inline void copyScalar<U8, LLVector2>(U8* src, LLVector2& dst)
        {
            dst.set((F32)*src, 0.f);
        }

        template<>
        inline void copyVec2<F32, LLVector2>(F32* src, LLVector2& dst)
        {
            dst.set(src[0], src[1]);
        }

        template<>
        inline void copyVec3<F32, LLVector2>(F32* src, LLVector2& dst)
        {
            dst.set(src[0], src[1]);
        }

        template<>
        inline void copyVec3<F32, vec3>(F32* src, vec3& dst)
        {
            dst.set(src);
        }

        template<>
        inline void copyVec3<F32, LLVector4a>(F32* src, LLVector4a& dst)
        {
            dst.load3(src);
        }

        template<>
        inline void copyVec3<F32, LLColor4U>(F32* src, LLColor4U& dst)
        {
            dst.set((U8)(src[0] * 255.f), (U8)(src[1] * 255.f), (U8)(src[2] * 255.f), 255);
        }

        template<>
        inline void copyVec3<U16, LLColor4U>(U16* src, LLColor4U& dst)
        {
            dst.set((U8)(src[0]), (U8)(src[1]), (U8)(src[2]), 255);
        }

        template<>
        inline void copyVec4<U8, LLColor4U>(U8* src, LLColor4U& dst)
        {
            dst.set(src[0], src[1], src[2], src[3]);
        }

        template<>
        inline void copyVec4<U16, U64>(U16* src, U64& dst)
        {
            U16* data = (U16*)&dst;
            data[0] = src[0];
            data[1] = src[1];
            data[2] = src[2];
            data[3] = src[3];
        }

        template<>
        inline void copyVec4<U8, U64>(U8* src, U64& dst)
        {
            U8* data = (U8*)&dst;
            data[0] = src[0];
            data[1] = src[1];
            data[2] = src[2];
            data[3] = src[3];
        }

        template<>
        inline void copyVec4<U16, LLColor4U>(U16* src, LLColor4U& dst)
        {
            dst.set((U8)(src[0]), (U8)(src[1]), (U8)(src[2]), ((U8)src[3]));
        }

        template<>
        inline void copyVec4<F32, LLColor4U>(F32* src, LLColor4U& dst)
        {
            dst.set((U8)(src[0]*255.f), (U8)(src[1]*255.f), (U8)(src[2]*255.f), (U8)(src[3]*255.f));
        }

        template<>
        inline void copyVec4<F32, LLVector4a>(F32* src, LLVector4a& dst)
        {
            dst.loadua(src);
        }

        template<>
        inline void copyVec4<U32, LLVector4a>(U32* src, LLVector4a& dst)
        {
            dst.set((F32)src[0], (F32)src[1], (F32)src[2], (F32)src[3]);
        }

        template<>
        inline void copyVec4<U16, LLVector4a>(U16* src, LLVector4a& dst)
        {
            dst.set(src[0], src[1], src[2], src[3]);
        }

        template<>
        inline void copyVec4<U8, LLVector4a>(U8* src, LLVector4a& dst)
        {
            dst.set(src[0], src[1], src[2], src[3]);
        }

        template<>
        inline void copyVec4<F32, quat>(F32* src, quat& dst)
        {
            dst.getVector4aRw().loadua(src);
        }

        template<>
        inline void copyMat4<F32, mat4>(F32* src, mat4& dst)
        {
            dst.loadu(src);
        }

        //=========================================================================================================

        // copy from src to dst, stride is the number of bytes between each element in src, count is number of elements to copy
        template<class S, class T>
        inline void copyScalar(S* src, LLStrider<T> dst, S32 stride, S32 count)
        {
            for (S32 i = 0; i < count; ++i)
            {
                copyScalar(src, *dst);
                dst++;
                src = (S*)((U8*)src + stride);
            }
        }

        // copy from src to dst, stride is the number of bytes between each element in src, count is number of elements to copy
        template<class S, class T>
        inline void copyVec2(S* src, LLStrider<T> dst, S32 stride, S32 count)
        {
            for (S32 i = 0; i < count; ++i)
            {
                copyVec2(src, *dst);
                dst++;
                src = (S*)((U8*)src + stride);
            }
        }

        // copy from src to dst, stride is the number of bytes between each element in src, count is number of elements to copy
        template<class S, class T>
        inline void copyVec3(S* src, LLStrider<T> dst, S32 stride, S32 count)
        {
            for (S32 i = 0; i < count; ++i)
            {
                copyVec3(src, *dst);
                dst++;
                src = (S*)((U8*)src + stride);
            }
        }

        // copy from src to dst, stride is the number of bytes between each element in src, count is number of elements to copy
        template<class S, class T>
        inline void copyVec4(S* src, LLStrider<T> dst, S32 stride, S32 count)
        {
            for (S32 i = 0; i < count; ++i)
            {
                copyVec4(src, *dst);
                dst++;
                src = (S*)((U8*)src + stride);
            }
        }

        // copy from src to dst, stride is the number of bytes between each element in src, count is number of elements to copy
        template<class S, class T>
        inline void copyMat2(S* src, LLStrider<T> dst, S32 stride, S32 count)
        {
            for (S32 i = 0; i < count; ++i)
            {
                copyMat2(src, *dst);
                dst++;
                src = (S*)((U8*)src + stride);
            }
        }

        // copy from src to dst, stride is the number of bytes between each element in src, count is number of elements to copy
        template<class S, class T>
        inline void copyMat3(S* src, LLStrider<T> dst, S32 stride, S32 count)
        {
            for (S32 i = 0; i < count; ++i)
            {
                copyMat3(src, *dst);
                dst++;
                src = (S*)((U8*)src + stride);
            }
        }

        // copy from src to dst, stride is the number of bytes between each element in src, count is number of elements to copy
        template<class S, class T>
        inline void copyMat4(S* src, LLStrider<T> dst, S32 stride, S32 count)
        {
            for (S32 i = 0; i < count; ++i)
            {
                copyMat4(src, *dst);
                dst++;
                src = (S*)((U8*)src + stride);
            }
        }

        template<class S, class T>
        inline void copy(Asset& asset, Accessor& accessor, const S* src, LLStrider<T>& dst, S32 byteStride)
        {
            if (accessor.mType == Accessor::Type::SCALAR)
            {
                S32 stride = byteStride == 0 ? sizeof(S) * 1 : byteStride;
                copyScalar((S*)src, dst, stride, accessor.mCount);
            }
            else if (accessor.mType == Accessor::Type::VEC2)
            {
                S32 stride = byteStride == 0 ? sizeof(S) * 2 : byteStride;
                copyVec2((S*)src, dst, stride, accessor.mCount);
            }
            else if (accessor.mType == Accessor::Type::VEC3)
            {
                S32 stride = byteStride == 0 ? sizeof(S) * 3 : byteStride;
                copyVec3((S*)src, dst, stride, accessor.mCount);
            }
            else if (accessor.mType == Accessor::Type::VEC4)
            {
                S32 stride = byteStride == 0 ? sizeof(S) * 4 : byteStride;
                copyVec4((S*)src, dst, stride, accessor.mCount);
            }
            else if (accessor.mType == Accessor::Type::MAT2)
            {
                S32 stride = byteStride == 0 ? sizeof(S) * 4 : byteStride;
                copyMat2((S*)src, dst, stride, accessor.mCount);
            }
            else if (accessor.mType == Accessor::Type::MAT3)
            {
                S32 stride = byteStride == 0 ? sizeof(S) * 9 : byteStride;
                copyMat3((S*)src, dst, stride, accessor.mCount);
            }
            else if (accessor.mType == Accessor::Type::MAT4)
            {
                S32 stride = byteStride == 0 ? sizeof(S) * 16 : byteStride;
                copyMat4((S*)src, dst, stride, accessor.mCount);
            }
            else
            {
                LL_ERRS("GLTF") << "Unsupported accessor type " << (S32)accessor.mType << LL_ENDL;
            }
        }

        // copy data from accessor to strider
        template<class T>
        inline void copy(Asset& asset, Accessor& accessor, LLStrider<T>& dst)
        {
            if (accessor.mBufferView == INVALID_INDEX
                || accessor.mBufferView >= asset.mBufferViews.size())
            {
                LL_WARNS("GLTF") << "Invalid buffer" << LL_ENDL;
                return;
            }
            const BufferView& bufferView = asset.mBufferViews[accessor.mBufferView];
            if (bufferView.mBuffer >= asset.mBuffers.size())
            {
                LL_WARNS("GLTF") << "Invalid buffer view" << LL_ENDL;
                return;
            }
            const Buffer& buffer = asset.mBuffers[bufferView.mBuffer];
            const U8* src = buffer.mData.data() + bufferView.mByteOffset + accessor.mByteOffset;

            switch (accessor.mComponentType)
            {
            case Accessor::ComponentType::FLOAT:
                copy(asset, accessor, (const F32*)src, dst, bufferView.mByteStride);
                break;
            case Accessor::ComponentType::UNSIGNED_INT:
                copy(asset, accessor, (const U32*)src, dst, bufferView.mByteStride);
                break;
            case Accessor::ComponentType::SHORT:
                copy(asset, accessor, (const S16*)src, dst, bufferView.mByteStride);
                break;
            case Accessor::ComponentType::UNSIGNED_SHORT:
                copy(asset, accessor, (const U16*)src, dst, bufferView.mByteStride);
                break;
            case Accessor::ComponentType::BYTE:
                copy(asset, accessor, (const S8*)src, dst, bufferView.mByteStride);
                break;
            case Accessor::ComponentType::UNSIGNED_BYTE:
                copy(asset, accessor, (const U8*)src, dst, bufferView.mByteStride);
                break;
            default:
                LL_ERRS("GLTF") << "Invalid component type" << LL_ENDL;
                break;
            }
        }

        // copy data from accessor to vector
        template<class T>
        inline void copy(Asset& asset, Accessor& accessor, std::vector<T>& dst)
        {
            dst.resize(accessor.mCount);
            LLStrider<T> strider = dst.data();
            copy(asset, accessor, strider);
        }


        //=========================================================================================================
        // JSON copying utilities. Reads come from a parsed simdjson document
        // (Value), writes stream JSON text through JsonWriter.
        // ========================================================================================================

        // read any JSON number into a float, mirroring numeric coercion
        inline bool to_float(const Value& src, F32& dst)
        {
            double d;
            if (src.get_double().get(d) == simdjson::SUCCESS)
            {
                dst = (F32)d;
                return true;
            }
            return false;
        }

        //====================== unspecialized base template, single value ===========================

        // to/from Value
        template<typename T>
        inline bool copy(const Value& src, T& dst)
        {
            dst = src;
            return true;
        }

        template<typename T>
        inline bool write(const T& src, JsonWriter& dst)
        {
            dst.startObject();
            src.serialize(dst);
            dst.endObject();
            return true;
        }

        template<typename T>
        inline bool copy(const Value& src, std::unordered_map<std::string, T>& dst)
        {
            simdjson::dom::object obj;
            if (src.get_object().get(obj) == simdjson::SUCCESS)
            {
                for (const auto& member : obj)
                {
                    copy<T>(member.value, dst[std::string(member.key)]);
                }
                return true;
            }
            return false;
        }

        template<typename T>
        inline bool write(const std::unordered_map<std::string, T>& src, JsonWriter& dst)
        {
            dst.startObject();
            for (const auto& [key, value] : src)
            {
                dst.key(key);
                write<T>(value, dst);
            }
            dst.endObject();
            return true;
        }

        // to/from array
        template<typename T>
        inline bool copy(const Value& src, std::vector<T>& dst)
        {
            simdjson::dom::array arr;
            if (src.get_array().get(arr) == simdjson::SUCCESS)
            {
                dst.resize(arr.size());
                size_t i = 0;
                for (const Value& v : arr)
                {
                    copy(v, dst[i++]);
                }
                return true;
            }

            return false;
        }

        template<typename T>
        inline bool write(const std::vector<T>& src, JsonWriter& dst)
        {
            dst.startArray();
            for (const T& t : src)
            {
                write(t, dst);
            }
            dst.endArray();
            return true;
        }

        // to/from object member
        template<typename T>
        inline bool copy(const simdjson::dom::object& src, string_view member, T& dst)
        {
            Value v;
            if (src[member].get(v) == simdjson::SUCCESS)
            {
                return copy(v, dst);
            }
            return false;
        }

        // always write a member to an object without checking default
        template<typename T>
        inline bool write_always(const T& src, string_view member, JsonWriter& dst)
        {
            dst.key(member);
            write(src, dst);
            return true;
        }


        // to/from extension

        // for internal use only, use copy_extensions instead
        inline bool _copy_extension(const simdjson::dom::object& extensions)
        {
            return false;
        }

        template<typename T, class... Rest>
        inline bool _copy_extension(const simdjson::dom::object& extensions, std::string_view member, T* dst, Rest... rest)
        {
            bool copied = false;
            Value v;
            if (extensions[member].get(v) == simdjson::SUCCESS)
            {
                copied = copy(v, *dst);
            }

            // copy each extension; do not short circuit on success
            bool rest_copied = _copy_extension(extensions, rest...);
            return copied || rest_copied;
        }

        // Copy all extensions from src.extensions to provided destinations
        // Usage:
        //  copy_extensions(src,
        //                  "KHR_materials_unlit", &mUnlit,
        //                  "KHR_materials_pbrSpecularGlossiness", &mPbrSpecularGlossiness);
        // returns true if any of the extensions are copied
        template<class... Types>
        inline bool copy_extensions(const Value& src, Types... args)
        {
            // extract the extensions object (don't assume it exists and verify that it is an object)
            simdjson::dom::object obj;
            if (src.get_object().get(obj) == simdjson::SUCCESS)
            {
                simdjson::dom::object ext_obj;
                if (obj["extensions"].get_object().get(ext_obj) == simdjson::SUCCESS)
                {
                    return _copy_extension(ext_obj, args...);
                }
            }

            return false;
        }

        // internal use only, use write_extensions instead
        inline bool _any_extension_present()
        {
            return false;
        }

        template<typename T, class... Rest>
        inline bool _any_extension_present(const T* src, string_view member, Rest... rest)
        {
            return src->mPresent || _any_extension_present(rest...);
        }

        inline void _write_extension(JsonWriter& dst)
        {
        }

        template<typename T, class... Rest>
        inline void _write_extension(JsonWriter& dst, const T* src, string_view member, Rest... rest)
        {
            if (src->mPresent)
            {
                dst.key(member);
                write(*src, dst);
            }
            _write_extension(dst, rest...);
        }

        // Write all extensions to dst.extensions
        // Usage:
        //  write_extensions(dst,
        //                   mUnlit, "KHR_materials_unlit",
        //                   mPbrSpecularGlossiness, "KHR_materials_pbrSpecularGlossiness");
        // returns true if any of the extensions are written
        template<class... Types>
        inline bool write_extensions(JsonWriter& dst, Types... args)
        {
            if (!_any_extension_present(args...))
            {
                return false;
            }

            dst.key("extensions");
            dst.startObject();
            _write_extension(dst, args...);
            dst.endObject();

            return true;
        }

        // conditionally write a member to an object if the member
        // is not the default value
        template<typename T>
        inline bool write(const T& src, string_view member, JsonWriter& dst, const T& default_value = T())
        {
            if (src != default_value)
            {
                return write_always(src, member, dst);
            }
            return false;
        }

        template<typename T>
        inline bool write(const std::unordered_map<std::string, T>& src, string_view member, JsonWriter& dst, const std::unordered_map<std::string, T>& default_value = std::unordered_map<std::string, T>())
        {
            if (!src.empty())
            {
                dst.key(member);
                write(src, dst);
                return true;
            }
            return false;
        }

        template<typename T>
        inline bool write(const std::vector<T>& src, string_view member, JsonWriter& dst, const std::vector<T>& deafault_value = std::vector<T>())
        {
            if (!src.empty())
            {
                dst.key(member);
                write(src, dst);
                return true;
            }
            return false;
        }

        template<typename T>
        inline bool copy(const Value& src, string_view member, T& dst)
        {
            simdjson::dom::object obj;
            if (src.get_object().get(obj) == simdjson::SUCCESS)
            {
                return copy(obj, member, dst);
            }

            return false;
        }

        // Accessor::ComponentType
        template<>
        inline bool copy(const Value& src, Accessor::ComponentType& dst)
        {
            int64_t i;
            if (src.get_int64().get(i) == simdjson::SUCCESS)
            {
                dst = (Accessor::ComponentType)i;
                return true;
            }
            return false;
        }

        template<>
        inline bool write(const Accessor::ComponentType& src, JsonWriter& dst)
        {
            dst.value((S32)src);
            return true;
        }

        //Primitive::Mode
        template<>
        inline bool copy(const Value& src, Primitive::Mode& dst)
        {
            int64_t i;
            if (src.get_int64().get(i) == simdjson::SUCCESS)
            {
                dst = (Primitive::Mode)i;
                return true;
            }
            return false;
        }

        template<>
        inline bool write(const Primitive::Mode& src, JsonWriter& dst)
        {
            dst.value((S32)src);
            return true;
        }

        // count floats from a JSON array into dst, or false leaving dst alone
        template<U32 count>
        inline bool copy_floats(const Value& src, F32* dst)
        {
            simdjson::dom::array arr;
            if (src.get_array().get(arr) == simdjson::SUCCESS && arr.size() == count)
            {
                // populate a temporary local in case we hit an error in the
                // middle of the array (don't partially write the destination)
                F32 t[count];
                for (U32 i = 0; i < count; ++i)
                {
                    if (!to_float(arr.at(i).value_unsafe(), t[i]))
                    {
                        return false;
                    }
                }
                for (U32 i = 0; i < count; ++i)
                {
                    dst[i] = t[i];
                }
                return true;
            }
            return false;
        }

        inline void write_floats(const F32* src, U32 count, JsonWriter& dst)
        {
            dst.startArray();
            for (U32 i = 0; i < count; ++i)
            {
                dst.value(src[i]);
            }
            dst.endArray();
        }

        // vec4
        template<>
        inline bool copy(const Value& src, vec4& dst)
        {
            F32 v[4];
            if (copy_floats<4>(src, v))
            {
                dst.loadua(v);
                return true;
            }
            return false;
        }

        template<>
        inline bool write(const vec4& src, JsonWriter& dst)
        {
            write_floats(src.getF32ptr(), 4, dst);
            return true;
        }

        // quat
        template<>
        inline bool copy(const Value& src, quat& dst)
        {
            F32 q[4];
            if (copy_floats<4>(src, q))
            {
                dst.getVector4aRw().loadua(q);
                return true;
            }
            return false;
        }

        template<>
        inline bool write(const quat& src, JsonWriter& dst)
        {
            write_floats(src.getVector4a().getF32ptr(), 4, dst);
            return true;
        }


        // vec3
        template<>
        inline bool copy(const Value& src, vec3& dst)
        {
            return copy_floats<3>(src, dst.mV);
        }

        template<>
        inline bool write(const vec3& src, JsonWriter& dst)
        {
            write_floats(src.mV, 3, dst);
            return true;
        }

        // vec2
        template<>
        inline bool copy(const Value& src, vec2& dst)
        {
            return copy_floats<2>(src, dst.mV);
        }

        template<>
        inline bool write(const vec2& src, JsonWriter& dst)
        {
            write_floats(src.mV, 2, dst);
            return true;
        }

        // bool
        template<>
        inline bool copy(const Value& src, bool& dst)
        {
            bool b;
            if (src.get_bool().get(b) == simdjson::SUCCESS)
            {
                dst = b;
                return true;
            }
            return false;
        }

        template<>
        inline bool write(const bool& src, JsonWriter& dst)
        {
            dst.value(src);
            return true;
        }

        // F32
        template<>
        inline bool copy(const Value& src, F32& dst)
        {
            return to_float(src, dst);
        }

        template<>
        inline bool write(const F32& src, JsonWriter& dst)
        {
            dst.value(src);
            return true;
        }


        // U32
        template<>
        inline bool copy(const Value& src, U32& dst)
        {
            int64_t i;
            if (src.get_int64().get(i) == simdjson::SUCCESS)
            {
                dst = (U32)i;
                return true;
            }
            return false;
        }

        template<>
        inline bool write(const U32& src, JsonWriter& dst)
        {
            dst.value(src);
            return true;
        }

        // F64
        template<>
        inline bool copy(const Value& src, F64& dst)
        {
            double d;
            if (src.get_double().get(d) == simdjson::SUCCESS)
            {
                dst = d;
                return true;
            }
            return false;
        }

        template<>
        inline bool write(const F64& src, JsonWriter& dst)
        {
            dst.value(src);
            return true;
        }

        // Accessor::Type
        template<>
        inline bool copy(const Value& src, Accessor::Type& dst)
        {
            std::string_view sv;
            if (src.get_string().get(sv) == simdjson::SUCCESS)
            {
                dst = gltf_type_to_enum(std::string(sv));
                return true;
            }
            return false;
        }

        template<>
        inline bool write(const Accessor::Type& src, JsonWriter& dst)
        {
            dst.value(enum_to_gltf_type(src));
            return true;
        }

        // S32
        template<>
        inline bool copy(const Value& src, S32& dst)
        {
            int64_t i;
            if (src.get_int64().get(i) == simdjson::SUCCESS)
            {
                dst = (S32)i;
                return true;
            }
            return false;
        }

        template<>
        inline bool write(const S32& src, JsonWriter& dst)
        {
            dst.value(src);
            return true;
        }


        // std::string
        template<>
        inline bool copy(const Value& src, std::string& dst)
        {
            std::string_view sv;
            if (src.get_string().get(sv) == simdjson::SUCCESS)
            {
                dst = std::string(sv);
                return true;
            }
            return false;
        }

        template<>
        inline bool write(const std::string& src, JsonWriter& dst)
        {
            dst.value(src);
            return true;
        }

        // mat4
        template<>
        inline bool copy(const Value& src, mat4& dst)
        {
            F32 m[16];
            if (copy_floats<16>(src, m))
            {
                dst.loadu(m);
                return true;
            }
            return false;
        }

        template<>
        inline bool write(const mat4& src, JsonWriter& dst)
        {
            write_floats(src.getF32ptr(), 16, dst);
            return true;
        }

        // Material::AlphaMode
        template<>
        inline bool copy(const Value& src, Material::AlphaMode& dst)
        {
            std::string_view sv;
            if (src.get_string().get(sv) == simdjson::SUCCESS)
            {
                dst = gltf_alpha_mode_to_enum(std::string(sv));
                return true;
            }
            return false;
        }

        template<>
        inline bool write(const Material::AlphaMode& src, JsonWriter& dst)
        {
            dst.value(enum_to_gltf_alpha_mode(src));
            return true;
        }

        //
        // ========================================================================================================

    }
}




