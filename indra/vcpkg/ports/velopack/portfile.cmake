vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO velopack/velopack
    REF c245055e3e1ff91a0109e28841b2375513d49fd0
    SHA512 aa16ded442be5c21bf4e1081aece07c3653ea32be6f9fc7c36d0def1c27c33cca94e55ed5f84db3a0f4f713cf147b09f572b2c3e20c1077ecfc18ea97b7fa941
    HEAD_REF main
)

if(VCPKG_TARGET_IS_WINDOWS)
    set(USER_HOME "$ENV{USERPROFILE}")
else()
    set(USER_HOME "$ENV{HOME}")
endif()

if(NOT DEFINED $ENV{CARGO_HOME})
    set(CARGO_HOME "${USER_HOME}/.cargo")
else()
    set(CARGO_HOME "$ENV{CARGO_HOME}")
endif()

find_program(CARGO_EXECUTABLE cargo
    HINTS "${CARGO_HOME}"
    PATH_SUFFIXES "bin")

find_program(RUSTUP_EXECUTABLE rustup
    HINTS "${CARGO_HOME}"
    PATH_SUFFIXES "bin")

if(VCPKG_TARGET_IS_WINDOWS)
    vcpkg_execute_required_process(
        COMMAND ${RUSTUP_EXECUTABLE} target add x86_64-pc-windows-msvc
        WORKING_DIRECTORY "${SOURCE_PATH}"
        LOGNAME rustup-${TARGET_TRIPLET}-dbg
    )

    vcpkg_execute_build_process(
        COMMAND ${CARGO_EXECUTABLE} build --target x86_64-pc-windows-msvc --release -p velopack_libc
        WORKING_DIRECTORY "${SOURCE_PATH}"
        LOGNAME cargo-${TARGET_TRIPLET}-dbg
    )

    file(INSTALL "${SOURCE_PATH}/target/x86_64-pc-windows-msvc/release/velopack_libc.dll" DESTINATION "${CURRENT_PACKAGES_DIR}/bin")
    file(INSTALL "${SOURCE_PATH}/target/x86_64-pc-windows-msvc/release/velopack_libc.dll.lib" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
elseif(VCPKG_TARGET_IS_OSX)
    vcpkg_execute_required_process(
        COMMAND ${RUSTUP_EXECUTABLE} target add aarch64-apple-darwin
        WORKING_DIRECTORY "${SOURCE_PATH}"
        LOGNAME rustup-${TARGET_TRIPLET}-dbg
    )

    vcpkg_execute_build_process(
        COMMAND ${CARGO_EXECUTABLE} build --target aarch64-apple-darwin --release -p velopack_libc
        WORKING_DIRECTORY "${SOURCE_PATH}"
        LOGNAME cargo-${TARGET_TRIPLET}-dbg
    )

    file(INSTALL "${SOURCE_PATH}/target/aarch64-apple-darwin/release/libvelopack_libc.a" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
elseif(VCPKG_TARGET_IS_LINUX)
    vcpkg_execute_required_process(
        COMMAND ${RUSTUP_EXECUTABLE} target add x86_64-unknown-linux-gnu
        WORKING_DIRECTORY "${SOURCE_PATH}"
        LOGNAME rustup-${TARGET_TRIPLET}-dbg
    )

    vcpkg_execute_build_process(
        COMMAND ${CARGO_EXECUTABLE} build --target x86_64-unknown-linux-gnu --release -p velopack_libc
        WORKING_DIRECTORY "${SOURCE_PATH}"
        LOGNAME cargo-${TARGET_TRIPLET}-dbg
    )

    file(INSTALL "${SOURCE_PATH}/target/x86_64-unknown-linux-gnu/release/libvelopack_libc.a" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
endif()

file(INSTALL "${SOURCE_PATH}/src/lib-cpp/include/Velopack.h" DESTINATION "${CURRENT_PACKAGES_DIR}/include/velopack")
file(INSTALL "${SOURCE_PATH}/src/lib-cpp/include/Velopack.hpp" DESTINATION "${CURRENT_PACKAGES_DIR}/include/velopack")

file(INSTALL "${SOURCE_PATH}/LICENSE" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
