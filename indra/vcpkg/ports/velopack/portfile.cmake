vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO velopack/velopack
    REF ${VERSION}
    SHA512 c12703a623555adc56c0e8f7ea70e789caa616d2d6f2c5a53eda7bf55794feee125b6a97817e17114ccf19ca1b43d377f70b5f417b7c9d2befa9f381c57f9d53
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
    if(VCPKG_OSX_ARCHITECTURES MATCHES "arm64")
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
    else()
        vcpkg_execute_required_process(
            COMMAND ${RUSTUP_EXECUTABLE} target add x86_64-apple-darwin
            WORKING_DIRECTORY "${SOURCE_PATH}"
            LOGNAME rustup-${TARGET_TRIPLET}-dbg
        )

        vcpkg_execute_build_process(
            COMMAND ${CARGO_EXECUTABLE} build --target x86_64-apple-darwin --release -p velopack_libc
            WORKING_DIRECTORY "${SOURCE_PATH}"
            LOGNAME cargo-${TARGET_TRIPLET}-dbg
        )

        file(INSTALL "${SOURCE_PATH}/target/x86_64-apple-darwin/release/libvelopack_libc.a" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
    endif()
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
