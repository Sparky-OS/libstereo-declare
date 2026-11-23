/* SPDX-License-Identifier: MIT */
/* ORDER 767: a caller that set the library up on a queue of its own destroys that queue after
 * stereo_declare_wayland_finish(), and the compositor may send an output event at any later time.
 * Nothing of the library may stay attached to the destroyed queue: libwayland warns at the
 * destruction ("destroyed while proxies still attached") and aborts the program at the next event
 * for such an object ("Tried to add event to destroyed queue"). The scenario runs in a child, so
 * an abort is reported here instead of ending the test. */
#include <stereo-declare.h>
#include <wayland-client.h>

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif

struct wl_display *shim_start(unsigned manager_version);
void shim_add_output(void);
void shim_pump(void);
void shim_pump_queue(struct wl_event_queue *queue);
unsigned shim_output_binds(void);
void shim_send_output_mode(void);
void shim_stop(void);

enum { CLEAN = 0, STILL_ATTACHED = 2, NO_DISPLAY = 10, NO_INIT = 11, NO_OUTPUT = 12 };

static int still_attached;

static void client_log(const char *format, va_list args)
{
    char message[2048];
    vsnprintf(message, sizeof(message), format, args);
    fputs(message, stderr);
    if (strstr(message, "still attached")) {
        still_attached = 1;
    }
}

static int scenario(void)
{
    wl_log_set_handler_client(client_log);
    struct wl_display *display = shim_start(6);
    if (!display) {
        return NO_DISPLAY;
    }
    shim_add_output();
    shim_pump();
    struct wl_event_queue *queue = wl_display_create_queue(display);
    if (!queue || stereo_declare_wayland_init(display, queue) != 0) {
        return NO_INIT;
    }
    shim_pump_queue(queue);
    /* From 1.12.2 the library binds no output on the caller's connection (row 835: the compositor names every output a
     * client holds in wl_surface.enter, and a program's listener met one it never bound). It learns the outputs on a
     * connection of its own to the compositor's address; this caller is connected through a socket pair, which has
     * none, so the answer is an error and the width is left alone. */
    double width = 0;
    const int answer = stereo_depth_wayland_output_width(&width);
    if (shim_output_binds() != 0 || answer != -ENOTCONN || width != 0) {
        fprintf(stderr, "the library bound %u outputs on the caller's connection, answered %d, width %g\n",
                shim_output_binds(), answer, width);
        return NO_OUTPUT;
    }
    stereo_declare_wayland_finish();
    wl_event_queue_destroy(queue);
    /* The compositor changes the output's mode after the caller is done, as a real session can. */
    shim_send_output_mode();
    shim_pump();
    shim_stop();
    return still_attached ? STILL_ATTACHED : CLEAN;
}

int main(void)
{
    fflush(NULL);
    pid_t child = fork();
    if (child < 0) {
        perror("fork");
        return 1;
    }
    if (child == 0) {
#ifdef __linux__
        /* The abort this test catches on an older library is expected: it must not reach the system's crash handler,
         * which records a crash even under a core size limit of 0 when the kernel pipes core dumps to it. */
        prctl(PR_SET_DUMPABLE, 0);
#endif
        _exit(scenario());
    }
    int status = 0;
    if (waitpid(child, &status, 0) != child) {
        perror("waitpid");
        return 1;
    }
    if (WIFSIGNALED(status)) {
        fprintf(stderr, "FAIL: the program died of signal %d (%s) at an output event after its queue was destroyed\n",
                WTERMSIG(status), strsignal(WTERMSIG(status)));
        return 1;
    }
    switch (WEXITSTATUS(status)) {
    case CLEAN:
        puts("PASS: finish, the queue's destruction and a later output event are clean");
        return 0;
    case STILL_ATTACHED:
        fputs("FAIL: an object of the library was still attached to the destroyed queue\n", stderr);
        return 1;
    default:
        fprintf(stderr, "FAIL: the scenario stopped before finish (step %d)\n", WEXITSTATUS(status));
        return 1;
    }
}
