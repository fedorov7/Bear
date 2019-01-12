/*  Copyright (C) 2012-2018 by László Nagy
    This file is part of Bear.

    Bear is a tool to generate compilation database for clang tooling.

    Bear is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Bear is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * This file implements a shared library. This library can be pre-loaded by
 * the dynamic linker of the Operating System. It implements a few function
 * related to process creation. By pre-load this library the executed process
 * uses these functions instead of those from the standard library.
 *
 * The idea here is to hijack the process creation methods: do not execute
 * the requested file, but execute another one. The another process is a
 * supervisor process, which executes the requested file, but it also
 * reports the lifecycle related events, like start, stop or signal received.
 *
 * Implementation details:
 *
 * - It's written in C++ 14.
 * - It's using symbols only from the `libc` and `libdl`.
 * - Memory handling:
 *    - It does not allocates heap memory. (no malloc, no new)
 *    - It allocates static memory and uses the stack.
 * - Error handling:
 *    - Any error is fatal.
 *    - Errors are reported on `stderr` only if it was requested.
 */

#include "config.h"

#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <atomic>

#include <dlfcn.h>

#include "libexec_a/Array.h"
#include "libexec_a/Resolver.h"
#include "libexec_a/Session.h"
#include "libexec_a/Environment.h"
#include "libexec_a/Storage.h"
#include "libexec_a/Executor.h"


namespace {

    size_t va_length(va_list &args) {
        size_t arg_count = 0;
        while (va_arg(args, const char *) != nullptr)
            ++arg_count;
        return arg_count;
    };

    void va_copy_n(va_list &args, char *argv[], size_t const argc) {
        for (size_t idx = 0; idx <= argc; ++idx)
            argv[idx] = va_arg(args, char *);
    };

    void *dynamic_linker(char const *const name) {
        return dlsym(RTLD_NEXT, name);
    }
}


/**
 * Library static data
 *
 * Will be initialized, when the library loaded into memory.
 */
namespace {

    std::atomic<bool> LOADED(false);
    ear::Session SESSION;

    constexpr size_t BUFFER_SIZE = 16 * 1024;
    char BUFFER[BUFFER_SIZE];

    ear::Resolver RESOLVER(&dynamic_linker);
}


/**
 * Library entry point.
 *
 * The first method to call after the library is loaded into memory.
 */
extern "C" void on_load() __attribute__((constructor));
extern "C" void on_load() {
    // Test whether on_load was called already.
    if (LOADED.exchange(true))
        return;

    const auto environment = ear::environment::current();
    SESSION = ear::Session::from(environment);

    ear::Storage storage(BUFFER, BUFFER + BUFFER_SIZE);
    SESSION.persist(storage);

    SESSION.write_message("on_load");
}

/**
 * Library exit point.
 *
 * The last method which needs to be called when the library is unloaded.
 */
extern "C" void on_unload() __attribute__((destructor));
extern "C" void on_unload() {
    // Test whether on_unload was called already.
    if (not LOADED.exchange(false))
        return;

    SESSION.write_message("on_unload");
}


extern "C"
int execve(const char *path, char *const argv[], char *const envp[]) {
    SESSION.write_message("execve");

    return ear::Executor(SESSION, RESOLVER).execve(path, argv, envp);
}


extern "C"
int execv(const char *path, char *const argv[]) {
    SESSION.write_message("execv");

    auto envp = const_cast<char *const *>(ear::environment::current());
    return ear::Executor(SESSION, RESOLVER).execve(path, argv, envp);
}


extern "C"
int execvpe(const char *file, char *const argv[], char *const envp[]) {
    SESSION.write_message("execvpe");

    return ear::Executor(SESSION, RESOLVER).execvpe(file, argv, envp);
}


extern "C"
int execvp(const char *file, char *const argv[]) {
    SESSION.write_message("execvp");

    auto envp = const_cast<char *const *>(ear::environment::current());
    return ear::Executor(SESSION, RESOLVER).execvpe(file, argv, envp);
}


extern "C"
int execvP(const char *file, const char *search_path, char *const argv[]) {
    SESSION.write_message("execvP");

    auto envp = const_cast<char *const *>(ear::environment::current());
    return ear::Executor(SESSION, RESOLVER).execvP(file, search_path, argv, envp);
}


extern "C"
int exect(const char *path, char *const argv[], char *const envp[]) {
    SESSION.write_message("exect");

    return ear::Executor(SESSION, RESOLVER).execve(path, argv, envp);
}


extern "C"
int execl(const char *path, const char *arg, ...) {
    SESSION.write_message("execl");

    // Count the number of arguments.
    va_list ap;
    va_start(ap, arg);
    const size_t argc = va_length(ap);
    va_end(ap);
    // Copy the arguments to the stack.
    va_start(ap, arg);
    char *argv[argc + 2];
    argv[0] = const_cast<char *>(path);
    va_copy_n(ap, &argv[1], argc);
    va_end(ap);

    auto envp = const_cast<char *const *>(ear::environment::current());
    return ear::Executor(SESSION, RESOLVER).execve(path, argv, envp);
}


extern "C"
int execlp(const char *file, const char *arg, ...) {
    SESSION.write_message("execlp");

    // Count the number of arguments.
    va_list ap;
    va_start(ap, arg);
    const size_t argc = va_length(ap);
    va_end(ap);
    // Copy the arguments to the stack.
    va_start(ap, arg);
    char *argv[argc + 2];
    argv[0] = const_cast<char *>(file);
    va_copy_n(ap, &argv[1], argc);
    va_end(ap);

    auto envp = const_cast<char *const *>(ear::environment::current());
    return ear::Executor(SESSION, RESOLVER).execvpe(file, argv, envp);
}


// int execle(const char *path, const char *arg, ..., char * const envp[]);
extern "C"
int execle(const char *path, const char *arg, ...) {
    SESSION.write_message("execle");

    // Count the number of arguments.
    va_list ap;
    va_start(ap, arg);
    const size_t argc = va_length(ap);
    va_end(ap);
    // Copy the arguments to the stack.
    va_start(ap, arg);
    char *argv[argc + 2];
    argv[0] = const_cast<char *>(path);
    va_copy_n(ap, &argv[1], argc);
    char **envp = va_arg(ap, char **);
    va_end(ap);

    return ear::Executor(SESSION, RESOLVER).execve(path, argv, envp);
}


extern "C"
int posix_spawn(pid_t *pid, const char *path,
                const posix_spawn_file_actions_t *file_actions,
                const posix_spawnattr_t *attrp,
                char *const argv[], char *const envp[]) {
    SESSION.write_message("posix_spawn");

    return ear::Executor(SESSION, RESOLVER).posix_spawn(pid, path, file_actions, attrp, argv, envp);
}


extern "C"
int posix_spawnp(pid_t *pid, const char *file,
                 const posix_spawn_file_actions_t *file_actions,
                 const posix_spawnattr_t *attrp,
                 char *const argv[], char *const envp[]) {
    SESSION.write_message("posix_spawnp");

    return ear::Executor(SESSION, RESOLVER).posix_spawnp(pid, file, file_actions, attrp, argv, envp);
}

//extern "C"
//FILE *popen(const char *command, const char *type) {
//}

//extern "C"
//int execveat(int dirfd,
//             const char *pathname,
//             char *const argv[],
//             char *const envp[],
//             int flags) {
//}

//extern "C"
//int fexecve(int fd, char *const argv[], char *const envp[]) {
//}
