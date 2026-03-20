vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO velopack/velopack
    REF f115f70a68caaee74c9fcd8eaa372b6db20433e3
    SHA512 614dfdd8368035eff2e7733c33954a15059cf81f0aa6d5b4ed717f679ab58379d8d122eb1e84d3752b4d14411367a554628c5cb6c1a15ab2f57ce8fccd2092c6
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
